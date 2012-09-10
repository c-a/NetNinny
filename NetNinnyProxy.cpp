#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <iostream>
#include <sstream>
#include <string>
using namespace std;

#include "NetNinnyProxy.h"

#define BLOCK_SIZE 512

static const char* filter_words[] =
{
    "SpongeBob",
    "BritneySpears",
    "Paris Hilton",
    "Norrköping",
    0
};

static const char* error1_redirect =
    "HTTP/1.1 301 Moved Permanently\r\n"
    "Location: http://www.ida.liu.se/~TDTS04/labs/2011/ass2/error1.html\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

NetNinnyBuffer::NetNinnyBuffer(size_t block_size) :
    m_block_size(block_size),
    m_size(0),
    m_index(0) {}

char*
NetNinnyBuffer::reserveData(size_t& size)
{
    size_t reserved_size = m_block_size * m_blocks.size();

    if (m_size < reserved_size)
    {
        size = reserved_size - m_size;
        return m_blocks.back() + (m_block_size - size);
    }
    else if (m_size == reserved_size)
    {
        char* block = (char*)malloc(m_block_size);
        if (!block)
            throw bad_alloc();

        m_blocks.push_back(block);
        size = m_block_size;
        return block;
    }
    else
        assert(!"Should not be here");
}

void
NetNinnyBuffer::dataWritten(size_t size)
{
    size_t new_size = m_size + size;
    assert(new_size <= m_block_size * m_blocks.size());

    m_size = new_size;
}

char
NetNinnyBuffer::getChar(size_t index)
{
    assert(index < m_block_size * m_blocks.size());

    const char* block = m_blocks[index / m_block_size];
    return block[index % m_block_size];
}

bool
NetNinnyBuffer::readLine(string& line)
{
    for (; m_index < m_size; ++m_index)
    {
        char c = getChar(m_index);
        
        if (c == '\n' && !line.empty() && (*line.rbegin()) == '\r')
        {
            line.append(1, c);
            m_index++;
            return true;
        }

        line.append(1, c);
    }

    return false;
}

void
NetNinnyBuffer::seek(size_t index)
{
    assert(index < m_block_size * m_blocks.size());

    m_index = index;
}

char*
NetNinnyBuffer::getBlock(size_t index, size_t& block_size)
{
    assert(index < m_blocks.size());

    if (index == m_blocks.size() - 1)
        block_size = m_block_size * m_blocks.size() - m_size;
    else
        block_size = m_block_size;

    return m_blocks[index];
}

NetNinnyBuffer::~NetNinnyBuffer()
{
    for(vector<char*>::iterator it = m_blocks.begin();
        it != m_blocks.end(); ++it)
        delete (*it);
}

// get sockaddr, IPv4 or IPv6:
static void*
get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

NetNinnyProxy::NetNinnyProxy(int sockfd)
    : client_socket(sockfd),
      server_socket(-1)
{
}

bool
NetNinnyProxy::readRequest(NetNinnyBuffer& buffer)
{
    while (true)
    {
        char* data;
        size_t data_size;

        data = buffer.reserveData(data_size);
        if (!data)
            throw bad_alloc();

        // Timeout after 15 seconds
        alarm(15);
        ssize_t ret = recv(client_socket, data, data_size, 0);
        // Reset timeout
        alarm(0);
        if (ret == -1)
        {
            if (errno == EINTR)
                printf("No data from client in 15 seconds, closing connection.\n");
            else
                perror("recv");

            return false;
        }
        else if (ret == 0)
        {
            fprintf(stderr, "Client closed the connection before complete request was received.\n");
            return false;
        }                
        else
        {
            buffer.dataWritten(ret);
            size_t size = buffer.getSize();
            if (size >= 4)
            {
                if (buffer.getChar(size - 4) == '\r' && buffer.getChar(size - 3) == '\n' && 
                    buffer.getChar(size - 2) == '\r' &&  buffer.getChar(size - 1) == '\n') 
                    return true;
            }
        }
    }
}

void
NetNinnyProxy::readResponse(NetNinnyBuffer& buffer)
{
    while (true)
    {
        char* data;
        size_t data_size;

        data = buffer.reserveData(data_size);

        ssize_t ret = recv(server_socket, data, data_size, 0);
        if (ret == -1)
        {
            perror("recv");
            throw "Failed to read response (recv failed)";
        }
        else if (ret == 0)
        {
            cout << "Finished" << endl;
            close(server_socket);
            server_socket = -1;
            return;
        }
        else
            buffer.dataWritten(ret);
    }
}

static void
buildNewRequest(NetNinnyBuffer& buffer, string& new_request, bool& keep_alive)
{
    keep_alive = false;
    static const char* connection_header = "Connection: Close\r\n";
    string line;

    new_request.reserve(buffer.getSize()+ strlen(connection_header));

    if (!buffer.readLine(line))
        throw "Failed to get HTTP start-line";

    new_request.append(line);
    
    // Read header fields
    while (buffer.readLine(line))
    {
        static const char* CONNECTION = "connection:";
        static const char* PROXY_CONNECTION = "proxy-connection:";

        const char* cline = line.c_str();

        if (!strncasecmp(cline, CONNECTION, strlen(CONNECTION)) ||
            !strncasecmp(cline, PROXY_CONNECTION, strlen(PROXY_CONNECTION)))
        {
            const char* value = strchr(cline, ':') + 1;
            while (*value == ' ') ++value;

            if (!strncasecmp(value, "keep-alive", strlen("keep-alive")))
                keep_alive = true;

            continue;
        }

        if (!line.empty() && line[0] != '\r')
        {
            new_request.append(line);
        }
    }

    new_request.append(connection_header);
    new_request.append("\r\n");
}

static void
sendMessage(int socket, const char* data, size_t size)
{
    size_t sent = 0;
    while (sent < size)
    {
        ssize_t ret = send(socket, data + sent, size - sent, 0);
        if (ret == -1)
        {
            perror("send");
            throw "Failed to send message";
        }
        sent += ret;
    }
}

bool
NetNinnyProxy::connectToServer(const char* address)
{
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(address, "80", &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return false;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((server_socket = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        if (connect(server_socket, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_socket);
            perror("connect");
            continue;
        }

        break;
    }

    if (!p)
        return false;

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
    printf("Connected to %s\n", s);

    freeaddrinfo(servinfo); // all done with this structure

    return true;
}

void
NetNinnyProxy::handleRequest(bool& keep_alive)
{
    NetNinnyBuffer buffer(BLOCK_SIZE);

    if (!readRequest(buffer))
        throw "Failed to read request";

    string line;
    char* cline;

    if (!buffer.readLine(line))
        throw "Failed to get HTTP start-line";
    cline = (char*)malloc(line.size() + 1);
    if (!cline)
        throw bad_alloc();
    strcpy(cline, line.c_str());

    cout << "Got request: " << line << endl;

    const char* request_type = strtok(cline, " ");
    if (strcmp(request_type, "GET"))
        throw "Not GET request";

    char* address = strtok(NULL, " ");        
    if (!address)
        throw "No address specified in GET request";

    if (strncmp(address, "http://", strlen("http://")))
        throw "The specified address was not an absolute http URI";

    address += strlen("http://");
    char* address_end = strchr(address, '/');
    if (address_end)
        *address_end = '\0';

    // Do the URL filtering
    for (const char** word = filter_words; *word; ++word)
    {
        if (strstr(address, *word))
        {
            sendMessage(client_socket, error1_redirect, strlen(error1_redirect));
            return;
        }
    }

    cout << address << endl;
    if (!connectToServer(address))
        throw "Failed to connect to server";

    // Send the request to the server
    string new_request;
    buffer.seek(0);
    buildNewRequest(buffer, new_request, keep_alive);
    sendMessage(server_socket, new_request.c_str(), new_request.size());

    // Read the response from the server
    NetNinnyBuffer response(BLOCK_SIZE);
    readResponse(response);

    // Send the response back to the client
    for (size_t i = 0; i < response.getNumBlocks(); i++)
    {
        size_t block_size;
        char *block = response.getBlock(i, block_size);
        sendMessage(client_socket, block, block_size);
    }
}

int
NetNinnyProxy::run()
{
    while (true)
    {
        bool keep_alive;
        try {
            handleRequest(keep_alive);
        }
        catch(const char* e_string) {
            cout << e_string << endl;
            return EXIT_FAILURE;
        }
        catch(...) {
            cout << "Got unknown exception" << endl;
            return EXIT_FAILURE;
        }
        if (!keep_alive)
            break;
    }

    return EXIT_SUCCESS;
}

NetNinnyProxy::~NetNinnyProxy()
{
    if (client_socket != -1)
        close(client_socket);
    if (server_socket != -1)
        close(server_socket);
}

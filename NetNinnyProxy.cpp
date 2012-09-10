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

#define RECV_SIZE 512

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

NetNinnyBuffer::NetNinnyBuffer() :
    m_data(0),
    m_reserved_size(0),
    m_size(0) {}

NetNinnyBuffer::NetNinnyBuffer(const NetNinnyBuffer& buffer)
{
    m_data = (char*)malloc(buffer.m_reserved_size);
    if (!m_data)
        throw bad_alloc();
    memcpy(m_data, buffer.m_data, buffer.m_size);

    m_reserved_size = buffer.m_reserved_size;
    m_size = buffer.m_size;
}

char*
NetNinnyBuffer::reserveData(size_t size)
{
    if (!m_data)
    {
        m_data = (char*)malloc(size);
        if (!m_data)
            return 0;
        m_reserved_size = size;
    }
    else if (m_size + size > m_reserved_size)
    {
        size_t new_size = max(m_size + size, m_reserved_size * 2);
        void* tmp_data = realloc(m_data, new_size);
        if (!tmp_data)
            return 0;

        m_data = (char*)tmp_data;
        m_reserved_size = new_size;
    }

    return m_data + m_size;
}

void
NetNinnyBuffer::dataWritten(size_t size)
{
    size_t new_size = m_size + size;
    assert(new_size <= m_reserved_size);

    m_size = new_size;
}

NetNinnyBuffer::~NetNinnyBuffer()
{
    free(m_data);
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

        data = buffer.reserveData(RECV_SIZE);
        if (!data)
            throw bad_alloc();

        // Timeout after 15 seconds
        alarm(15);
        ssize_t ret = recv(client_socket, data, RECV_SIZE, 0);
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
            if (buffer.getSize() >= 4)
            {
                if (!memcmp(buffer.getData() + buffer.getSize() - 4, "\r\n\r\n", 4))
                    return true;
            }
        }
    }
}

void
NetNinnyProxy::readResponse(list<NetNinnyBuffer>& response)
{
    while (true)
    {
        NetNinnyBuffer* buffer = 0;
        char* data;

        if (!response.empty())
            buffer = &response.back();

        if (!buffer || buffer->getSize() == buffer->getReservedSize())
        {
            response.push_back(NetNinnyBuffer());
            buffer = &response.back();
            data = buffer->reserveData(RECV_SIZE);
            if (!data)
                throw bad_alloc();
        }

        data = buffer->getData() + buffer->getSize();

        ssize_t ret = recv(server_socket, data,
                           buffer->getReservedSize() - buffer->getSize(), 0);
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
            buffer->dataWritten(ret);
    }
}

static void
buildNewRequest(NetNinnyBuffer& buffer, string& new_request, bool& keep_alive)
{
    keep_alive = false;
    static const char* connection_header = "Connection: Close\r\n";
    char line[256];

    new_request.reserve(buffer.getSize()+ strlen(connection_header));

    istringstream iss(buffer.getData());
    iss.exceptions(istream::badbit);

    try {
        iss.getline(line, 256);
    }
    catch (istream::failure& e) {
        throw "Failed to get HTTP start-line";
    }

    new_request.append(line);
    new_request.append("\n");
    
    // Read header fields
    try {
        while (iss.good())
        {
            static const char* CONNECTION = "connection:";
            static const char* PROXY_CONNECTION = "proxy-connection:";

            iss.getline(line, 256);
            if (!strncasecmp(line, CONNECTION, strlen(CONNECTION)) ||
                !strncasecmp(line, PROXY_CONNECTION, strlen(PROXY_CONNECTION)))
            {
                const char* value = strchr(line, ':') + 1;
                while (*value == ' ') ++value;

                if (!strncasecmp(value, "keep-alive", strlen("keep-alive")))
                    keep_alive = true;

                continue;
            }

            if (line[0] != '\0' && line[0] != '\r')
            {
                new_request.append(line);
                new_request.append("\n");
            }
        }
    }
    catch (istream::failure& e) {
        throw "Failed to get HTTP header field";
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
    NetNinnyBuffer buffer;

    if (!readRequest(buffer))
        throw "Failed to read request";

    istringstream iss(buffer.getData());
    iss.exceptions(istream::badbit);
    char line[256];

    try {
        iss.getline(line, 256);
    }
    catch (istream::failure& e) {
        throw "Failed to get HTTP start-line";
    }

    cout << "Got request: " << line << endl;

    const char* request_type = strtok(line, " ");
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
    buildNewRequest(buffer, new_request, keep_alive);
    sendMessage(server_socket, new_request.c_str(), new_request.size());

    // Read the response from the server
    list<NetNinnyBuffer> response;
    readResponse(response);

    // Send the response back to the client
    for (list<NetNinnyBuffer>::iterator it = response.begin();
         it != response.end(); ++it)
    {
        NetNinnyBuffer& buffer = *it;
        sendMessage(client_socket, buffer.getData(), buffer.getSize());
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

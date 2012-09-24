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
#include <cctype>
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

static const char* error2_redirect =
    "HTTP/1.1 301 Moved Permanently\r\n"
    "Location: http://www.ida.liu.se/~TDTS04/labs/2011/ass2/error2.html\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

#ifndef _GNU_SOURCE
static const char*
strcasestr(const char* haystack, const char* needle)
{
    for (; *haystack; ++haystack)
    {
        const char* h = haystack;
        const char* n = needle;
        for (; *n && *h && tolower(*n) == tolower(*h); n++, h++);

        if (!*n)
            return haystack;
    }

    return NULL;
}
#endif

NetNinnyBuffer::NetNinnyBuffer(size_t block_size) :
    m_block_size(block_size),
    m_size(0),
    m_index(0) {}

/**
 * Reserve a data block to write into.
 * 
 * @param size The size of the data block.
 * @return A data block to write into.
 */
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
        char* block = new char[m_block_size];

        m_blocks.push_back(block);
        size = m_block_size;
        return block;
    }
    else
        assert(!"Should not be here");
}

/**
 * Notify the buffer that @size data was written.
 * 
 * @param size The number of bytes that was written into the buffer.
 */
void
NetNinnyBuffer::dataWritten(size_t size)
{
    size_t new_size = m_size + size;
    assert(new_size <= m_block_size * m_blocks.size());

    m_size = new_size;
}

/**
 * Get the character at @index.
 * 
 * @param index The position of the character to get.
 * @return The character.
 */
char
NetNinnyBuffer::operator[](size_t index) const
{
    assert(index < m_block_size * m_blocks.size());

    const char* block = m_blocks[index / m_block_size];
    return block[index % m_block_size];
}

/**
 * Read a line from the buffer and put it in @line.
 * 
 * @param line A string where the line will be put.
 * @return true if a complete line was read or false otherwise.
 */
bool
NetNinnyBuffer::readLine(string& line)
{
    line.clear();

    for (; m_index < m_size; ++m_index)
    {
        char c = (*this)[m_index];
        
        if (c == '\n' && !line.empty() && (*line.rbegin()) == '\r')
        {
            line.append(1, c);
            ++m_index;
            return true;
        }

        line.append(1, c);
    }

    return false;
}

/**
 * Get the data block at @index.
 * 
 * @param index The index of the data block to get
 * @param block_size The size of the block returned.
 * @return The data block.
 */
char*
NetNinnyBuffer::getBlock(size_t index, size_t& block_size)
{
    assert(index < m_blocks.size());

    if (index == m_blocks.size() - 1)
        block_size = m_size - m_block_size * (m_blocks.size() - 1);
    else
        block_size = m_block_size;

    return m_blocks[index];
}

NetNinnyBuffer::~NetNinnyBuffer()
{
    for(vector<char*>::iterator it = m_blocks.begin();
        it != m_blocks.end(); ++it)
        delete[] (*it);
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

/**
 * Read a request from the client into buffer.
 * 
 * @param a NetNinnyBuffer in which the request should be stored.
 * @return true if a complete request was received, false otherwise.
 */
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
                if (buffer[size - 4] == '\r' && buffer[size - 3] == '\n' && 
                    buffer[size - 2] == '\r' &&  buffer[size - 1] == '\n') 
                    return true;
            }
        }
    }
}

/**
 * Read a response header from the server into buffer.
 * 
 * @param buffer a NetNinnyBuffer in which the response should be stored.
 */
void
NetNinnyProxy::readResponseHeader(NetNinnyBuffer& buffer)
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
            close(server_socket);
            server_socket = -1;
            return;
        }
        else
        {
            size_t start_index = buffer.getSize();
            if (start_index >= 3)
                start_index -= 3;
            else
                start_index = 0;

            buffer.dataWritten(ret);

            for (; start_index < buffer.getSize() - 4; ++start_index)
            {
                if (buffer[start_index] == '\r' && buffer[start_index + 1] == '\n' &&
                    buffer[start_index] == '\r' && buffer[start_index + 1] == '\n')
                    return;
            }
        }
    }
}

/**
 * Read a response from the server into buffer.
 * 
 * @param buffer a NetNinnyBuffer in which the response should be stored.
 */
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
            close(server_socket);
            server_socket = -1;
            return;
        }
        else
        {
            buffer.dataWritten(ret);
        }
    }
}

/**
 * Send a message to @socket.
 * 
 * @param socket The socket which should be used to send the message.
 * @param data A character array of data to send.
 * @param size The number of bytes in @data.
 */
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

/**
 * Send the start of the response found in @buffer and stream
 * the rest from the server.
 *
 * @param buffer The start of the response.
 */
void
NetNinnyProxy::streamResponse(NetNinnyBuffer& buffer)
{
    // Send the response back to the client
    for (size_t i = 0; i < buffer.getNumBlocks(); i++)
    {
        size_t block_size;
        char *block = buffer.getBlock(i, block_size);
        sendMessage(client_socket, block, block_size);
    }

    while (true)
    {
        char buffer[512];

        ssize_t ret = recv(server_socket, buffer, 512, 0);
        if (ret == -1)
        {
            perror("recv");
            throw "Failed to read response (recv failed)";
        }
        else if (ret == 0)
        {
            close(server_socket);
            server_socket = -1;
            return;
        }
        else
            sendMessage(client_socket, buffer, ret);
    }
}

/**
 * Build a new request to be sent to the real server from the request in @request.
 * 
 * @param request A NetNinnyBuffer containing the headers of the request received
 *        from the client.
 * @param request_line The request-line that should be used for the new request.
 * @param (out) new_request The new request that was created.
 * @param (out) keep_alive True if request contained a "Connection: Keep-Alive"
 *        header.
 * @param (out) host A string containing the host that the request should be
 * sent to.
 */
static void
buildNewRequest(NetNinnyBuffer& request, string& request_line,
                string& new_request, bool& keep_alive, string& host)
{
    keep_alive = false;
    static const char* connection_header = "Connection: Close\r\n";
    string line;

    new_request.reserve(request.getSize() + strlen(connection_header));

    new_request.append(request_line);
    
    // Read header fields
    while (request.readLine(line))
    {
        static const char* CONNECTION = "connection:";
        static const char* PROXY_CONNECTION = "proxy-connection:";
        static const char* HOST = "host:";
        static const char* ACCEPT_ENCODING = "accept-encoding:";

        const char* cline = line.c_str();

        if (line[0] == '\r' && line[1] == '\n')
            break;

        if (!strncasecmp(cline, CONNECTION, strlen(CONNECTION)) ||
            !strncasecmp(cline, PROXY_CONNECTION, strlen(PROXY_CONNECTION)))
        {
            const char* value = strchr(cline, ':') + 1;
            while (*value == ' ') ++value;

            if (!strncasecmp(value, "keep-alive", strlen("keep-alive")))
                keep_alive = true;

            continue;
        }
        else if (!strncasecmp(cline, HOST, strlen(HOST)))
        {
            const char* value = cline + strlen(HOST);
            while (*value == ' ') ++value;

            const char* value_end = strchr(value, '\r');
            while (*value_end == ' ') --value_end;
            host.assign(value, value_end - value);
        }
        else if (!strncasecmp(cline, ACCEPT_ENCODING, strlen(ACCEPT_ENCODING)))
            continue;

        new_request.append(line);
    }

    new_request.append(connection_header);
    new_request.append("\r\n");
}

/**
 * Connect to the server at @host.
 * 
 * @param host A string with the hostname of the server to connect to.
 * @return true if the connection was succesful, false otherwise.
 */
bool
NetNinnyProxy::connectToServer(string& host)
{
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host.c_str(), "80", &hints, &servinfo)) != 0)
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

/**
 * @return true if the response is one that can be filtered and therefore need to be buffered.
 */
static bool
isFilterableResponse(NetNinnyBuffer& buffer)
{
    string line;

    if (!buffer.readLine(line))
        return false;

    // read headers
    while (buffer.readLine(line))
    {
        const char* CONTENT_TYPE = "content-type:";
        const char* CONTENT_ENCODING = "content-encoding:";
        
        // end of headers
        if (line[0] == '\r' && line[1] == '\n')
            break;

        const char* cline = line.c_str();
        for (; *cline == ' '; ++cline);

        if (!strncasecmp(cline, CONTENT_TYPE, strlen(CONTENT_TYPE)))
        {
            const char* content_type = cline + strlen(CONTENT_TYPE);
            for (; *content_type == ' '; ++content_type);
            if (strncasecmp(content_type, "text", strlen("text")))
                return false;
        }
        else if (!strncasecmp(cline, CONTENT_ENCODING, strlen(CONTENT_ENCODING)))
            return false;
    }

    return true;
}

/**
 * Filter the http response contained in @buffer.
 * 
 * @param buffer A NetNinnyBuffer containing the response to filter.
 * @return true if the response contained forbidden content or false otherwise.
 */
bool
NetNinnyProxy::filterResponse(NetNinnyBuffer& buffer)
{
    string line;

    // Search for the forbidden strings in the content.
    for (size_t i = buffer.getIndex(); i < buffer.getSize(); ++i)
    {
        for (const char** word = filter_words; *word; ++word)
        {
            size_t j = i;
            const char* w = *word;
            while (j < buffer.getSize() && *w != '\0')
            {
                if (toupper(buffer[j]) != toupper(*w))
                    break;

                ++j;
                ++w;
            }

            if (*w == '\0')
                return true;
        }
    }

    return false;
}

/**
 * Read a request from the client and handle it.
 * 
 * @param keep_alive true if the connection should be kept alive or false
 * otherwise.
 */
void
NetNinnyProxy::handleRequest(bool& keep_alive)
{
    NetNinnyBuffer buffer(BLOCK_SIZE);

    if (!readRequest(buffer))
        throw "Failed to read request";

    string line;
    const char* cline;

    if (!buffer.readLine(line))
        throw "Failed to get HTTP start-line";
    cline = line.c_str();

    cout << "Got request: " << line << endl;

    if (strncmp(cline, "GET", 3))
        throw "Not GET request";

    // extract the path from the request
    const char* cpath = strstr(cline, " ");
    if (!cpath)
        throw "No path specified in GET request";
    while (*cpath == ' ') cpath++; // skip extra whitespace

    // skip hostname if it's provided
    if (!strncmp(cpath, "http://", strlen("http://")))
    {
        cpath += strlen("http://");
        cpath = strstr(cpath, "/");
        if (!cpath)
            throw "Invalid path provided in GET request";
    }
    const char* cpath_end = strstr(cpath, " ");
    if (!cpath_end)
        throw "Invalid GET request";
    
    string path(cpath, cpath_end - cpath);

    // Do the path filtering
    for (const char** word = filter_words; *word; ++word)
    {
        if (strcasestr(path.c_str(), *word))
        {
            cout << "URL was filtered\n";
            sendMessage(client_socket, error1_redirect, strlen(error1_redirect));
            return;
        }
    }

    // Build the new request-line
    string request_line("GET ");
    request_line.append(path);
    request_line.append(cpath_end);

    // Build the new request
    string new_request, host;
    buildNewRequest(buffer, request_line, new_request, keep_alive, host);
    if (host.empty())
        throw "No host specified in the request";
    cout << "Host: " << host << endl;

    if (!connectToServer(host))
        throw "Failed to connect to server";

    sendMessage(server_socket, new_request.c_str(), new_request.size());

    // Read the response from the server
    NetNinnyBuffer response(BLOCK_SIZE);
    readResponseHeader(response);

    if (isFilterableResponse(response))
    {
        readResponse(response);

        if (filterResponse(response))
        {
            cout << "Content was filtered\n";
            sendMessage(client_socket, error2_redirect, strlen(error2_redirect));
        }
        else
        {
            // Send the response back to the client
            for (size_t i = 0; i < response.getNumBlocks(); i++)
            {
                size_t block_size;
                char *block = response.getBlock(i, block_size);
                sendMessage(client_socket, block, block_size);
            }
        }
    }
    else
        streamResponse(response);
}

/**
 * Run the proxy.
 * 
 * @return An integer with the exit code.
 */
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
            fprintf(stderr, "%s\n", e_string);
            return EXIT_FAILURE;
        }
        catch(...) {
            fprintf(stderr, "Got unknown exception");
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

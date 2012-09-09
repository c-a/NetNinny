#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

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

static const char* test_response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 25\r\n"
    "\r\n"
    "NetNinny ate your content";

NetNinnyBuffer::NetNinnyBuffer() :
    m_data(0),
    m_reserved_size(0),
    m_size(0) {}

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

NetNinnyProxy::NetNinnyProxy(int sockfd)
: sockfd(sockfd)
{
}

bool
NetNinnyProxy::readRequest(NetNinnyBuffer& buffer)
{
    while (true)
    {
        char* data;

        data = buffer.reserveData(RECV_SIZE);

        // Timeout after 15 seconds
        alarm(15);
        ssize_t ret = recv(sockfd, data, RECV_SIZE, 0);
        // Reset timeout
        alarm(0);
        if (ret == -1)
        {
            if (errno == EINTR)
                printf("No data from client in 15 seconds, closing connection");
            else
                perror("recv");

            return false;
        }
        else if (ret == 0)
        {
            printf("client closed the connection");
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

void
NetNinnyProxy::sendResponse(const char* data, size_t size)
{
    size_t sent = 0;
    while (sent < size)
    {
        ssize_t ret = send(sockfd, data + sent, size - sent, 0);
        if (ret == -1)
        {
            perror("send");
            throw "Failed to send response";
        }
        sent += ret;
    }
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

    const char* address = strtok(NULL, " ");
    if (!address)
        throw "No address specified in GET request";

    // Do the URL filtering
    for (const char** word = filter_words; *word; ++word)
    {
        if (strstr(address, *word))
        {
            sendResponse(error1_redirect, strlen(error1_redirect));
            return;
        }
    }

    string new_request;
    buildNewRequest(buffer, new_request, keep_alive);

    cout << new_request << endl;
    cout << "Keep alive: " << keep_alive << endl;

    sendResponse(test_response, strlen(test_response));
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
    close(sockfd);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

NetNinnyBuffer::NetNinnyBuffer()
: m_data(0),
m_reserved_size(0),
m_size(0) {}

bool
NetNinnyBuffer::reserve(size_t size)
{
    if (!m_data)
    {
        m_data = (char*)malloc(size);
        if (!m_data)
            return false;
        m_reserved_size = size;
    }
    else if (size > m_reserved_size)
    {
        void* tmp_data = realloc(m_data, m_reserved_size * 2);
        if (!tmp_data)
            return false;

        m_data = (char*)tmp_data;
        m_reserved_size *= 2;
    }

    return true;
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
    if (!buffer.reserve(RECV_SIZE))
        return false;

    while (true)
    {
        if (!buffer.reserve(buffer.getSize() + RECV_SIZE))
            return false;

        ssize_t ret = recv(sockfd, buffer.getData() + buffer.getSize(), RECV_SIZE, 0);
        if (ret == -1)
        {
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
            buffer.setSize(buffer.getSize() + ret);
            if (buffer.getSize() >= 4)
            {
                if (memcmp(buffer.getData() + buffer.getSize() - 4, "\r\n\r\n", 4))
                    return 0;
            }
        }
    }
}

static void
readHeaders(istream& is, string& request)
{
    // Read header fields
    try {
        while (!is.eof())
        {
            static const char* CONNECTION = "Connection:";
            char line[256];

            is.getline(line, 256);
            if (!strncmp(line, CONNECTION, strlen(CONNECTION)))
                continue;

            request.append(line);
            request.append("\r\n");
        }
    }
    catch (istream::failure& e) {
        throw "Failed to get HTTP header field";
    }
}

void
NetNinnyProxy::run()
{
    NetNinnyBuffer buffer;

    if (!readRequest(buffer))
        throw "Failed to read request";

    istringstream iss(buffer.getData());
    iss.exceptions(istream::failbit  | istream::badbit);
    char line[256];

    try {
        iss.getline(line, 256);
    }
    catch (istream::failure& e) {
        throw "Failed to get HTTP start-line";
    }

    if (!strncmp(line, "GET", 3))
        throw "Not GET request";

    const char* connection_header = "Connection: Keep-alive\r\n";

    string new_request;
    new_request.reserve(buffer.getSize()+ strlen(connection_header));
    new_request.append(line);
    new_request.append("\r\n");

    readHeaders(iss, new_request);
    new_request.append(connection_header);
    new_request.append("\r\n");

    cout << new_request << endl;

    if (send(sockfd, "Hello, world!", 13, 0) == -1)
    {
        perror("send");
        throw "Failed to send response";
    }
}

NetNinnyProxy::~NetNinnyProxy()
{
    close(sockfd);
}

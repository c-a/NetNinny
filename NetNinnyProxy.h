/*
 * NetNinnyProxy.h
 *
 *  Created on: 7 sep 2012
 *      Author: carin003
 */

#ifndef NETNINNYPROXY_H_
#define NETNINNYPROXY_H_

#include <assert.h>

#include <list>

class NetNinnyBuffer
{
private:
    char* m_data;
    size_t m_reserved_size;
    size_t m_size;

public:
    NetNinnyBuffer();
    NetNinnyBuffer(const NetNinnyBuffer& buffer);
    ~NetNinnyBuffer();

    char* getData() { return m_data; }
    size_t getSize() { return m_size; }
    size_t getReservedSize() { return m_reserved_size; }

    char* reserveData(size_t size);
    void dataWritten(size_t size);
};

class NetNinnyProxy
{
private:
    int client_socket, server_socket;

    bool readRequest(NetNinnyBuffer& buffer);
    void readResponse(std::list<NetNinnyBuffer>& response);

    bool connectToServer(const char* address);

    void handleRequest(bool& keep_alive);

public:
    NetNinnyProxy(int sockfd);

    int run();

    virtual ~NetNinnyProxy();
};

#endif /* NETNINNYPROXY_H_ */

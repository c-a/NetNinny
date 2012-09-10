/*
 * NetNinnyProxy.h
 *
 *  Created on: 7 sep 2012
 *      Author: carin003
 */

#ifndef NETNINNYPROXY_H_
#define NETNINNYPROXY_H_

#include <assert.h>

#include <string>
#include <vector>

class NetNinnyBuffer
{
private:
    std::vector<char*> m_blocks;
    size_t m_block_size;

    size_t m_size, m_index;

    NetNinnyBuffer(const NetNinnyBuffer& buffer);

public:
    NetNinnyBuffer(size_t block_size);
    ~NetNinnyBuffer();

    size_t getSize() { return m_size; }
    size_t getNumBlocks() { return m_size / m_block_size; }

    char* reserveData(size_t& size);
    void dataWritten(size_t size);

    char getChar(size_t index);
    bool readLine(string& line);

    void seek(size_t index);

    char* getBlock(size_t index, size_t &block_ssize);
};

class NetNinnyProxy
{
private:
    int client_socket, server_socket;

    bool readRequest(NetNinnyBuffer& buffer);
    void readResponse(NetNinnyBuffer& buffer);

    bool connectToServer(const char* address);

    void handleRequest(bool& keep_alive);

public:
    NetNinnyProxy(int sockfd);

    int run();

    virtual ~NetNinnyProxy();
};

#endif /* NETNINNYPROXY_H_ */

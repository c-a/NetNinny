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

/*
 * NetNinnyBuffer
 * 
 * Container class for the requests and responses
 * that dynamically increases in size if needed.
 * 
 * Can output the data char by char or in lines.
 * 
 */
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
    size_t getNumBlocks() { return m_blocks.size(); }
    size_t getIndex() { return m_index; }

    char* reserveData(size_t& size);
    void dataWritten(size_t size);

    char operator[](size_t index) const;
    bool readLine(string& line);

    char* getBlock(size_t index, size_t &block_ssize);
};

/*
 * NetNinnyProxy
 * 
 * Maintains one connection per instance and filters the
 * requests and responses during the connection session.
 * 
 */
class NetNinnyProxy
{
private:
    int client_socket, server_socket;

    bool readRequest(NetNinnyBuffer& buffer);
    void readResponse(NetNinnyBuffer& buffer);

    bool connectToServer(string& host);
    bool filterResponse(NetNinnyBuffer& buffer);

    void handleRequest(bool& keep_alive);

public:
    NetNinnyProxy(int sockfd);

    int run();

    virtual ~NetNinnyProxy();
};

#endif /* NETNINNYPROXY_H_ */

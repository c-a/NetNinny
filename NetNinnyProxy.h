/*
 * NetNinnyProxy.h
 *
 *  Created on: 7 sep 2012
 *      Author: carin003
 */

#ifndef NETNINNYPROXY_H_
#define NETNINNYPROXY_H_

#include <assert.h>

class NetNinnyBuffer
{
private:
	char* m_data;
	size_t m_reserved_size;
	size_t m_size;

public:
	NetNinnyBuffer();
	~NetNinnyBuffer();

	char* getData() { return m_data; }

	size_t getSize() { return m_size; }
	size_t getReservedSize() { return m_reserved_size; }

	void setSize(size_t size) { assert(size <= m_reserved_size); m_size = size; }

	bool reserve(size_t size);
};

class NetNinnyProxy
{
private:
	int sockfd;

	bool readRequest(NetNinnyBuffer& buffer);

public:
	NetNinnyProxy(int sockfd);

	void run();

	virtual ~NetNinnyProxy();
};

#endif /* NETNINNYPROXY_H_ */

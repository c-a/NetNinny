#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

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

int
NetNinnyProxy::run()
{
	NetNinnyBuffer buffer;

	if (!readRequest(buffer))
		return 0;

	if (send(sockfd, "Hello, world!", 13, 0) == -1)
		perror("send");

	return 0;
}

NetNinnyProxy::~NetNinnyProxy()
{
	close(sockfd);
}

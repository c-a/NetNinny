/*
 * NetNinnyProxy.h
 *
 *  Created on: 7 sep 2012
 *      Author: carin003
 */

#ifndef NETNINNYPROXY_H_
#define NETNINNYPROXY_H_

class NetNinnyProxy
{
private:
	int sockfd;

public:
	NetNinnyProxy(int sockfd);

	int run();

	virtual ~NetNinnyProxy();
};

#endif /* NETNINNYPROXY_H_ */

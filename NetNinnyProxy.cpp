/*
 * NetNinnyProxy.cpp
 *
 *  Created on: 7 sep 2012
 *      Author: carin003
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "NetNinnyProxy.h"

NetNinnyProxy::NetNinnyProxy(int sockfd)
	: sockfd(sockfd)
{
}

int
NetNinnyProxy::run()
{
	if (send(sockfd, "Hello, world!", 13, 0) == -1)
		perror("send");

	return 0;
}

NetNinnyProxy::~NetNinnyProxy() {
	close(sockfd);
}

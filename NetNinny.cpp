//============================================================================
// Name        : NetNinny.cpp
// Author      : Carl-Anton Ingmarsson
// Version     :
// Copyright   : 
// Description : Hello World in C, Ansi-style
//============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

class NetNinny
{
public:
	void run();
};

void
NetNinny::run()
{
	puts("Hello World!!!");
}

int main(void) {

	NetNinny ninny;
	ninny.run();

	return EXIT_SUCCESS;
}

#ifndef CONNECTION_MANAGER_H_
#define CONNECTION_MANAGER_H_

#include <stdbool.h>

#define MAX_PINGFAIL_OFFLINE 10

void ConnectionManager();
bool ping(char *address, int port);
bool isOnline();
void pingThread();

#endif
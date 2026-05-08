#ifndef SERVER_STUB_H
#define SERVER_STUB_H

#include "server_functions.h"

/**
 * server_dispatch
 * Parses the incoming request line from the client, extracts the 
 * procedure code and arguments, and routes the request to the 
 * appropriate handler function.
 * 
 * @param cfd  The client file descriptor for the socket.
 * @param line The raw string received from the client.
 */
void server_dispatch(int cfd, char *line);

#endif /* SERVER_STUB_H */
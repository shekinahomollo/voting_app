/*
 * evoting_server.c
 *
 * Server for the Electronic Voting System.
 * Listens on EVOTING_PORT (TCP), accepts one client at a time, reads
 * request lines, dispatches to the appropriate handler, and writes
 * response lines back.
 *
 * No rpcgen-generated files are used.
 *
 * Build:
 *   gcc -Wall -o evoting_server evoting_server.c
 *
 * Usage:
 *   ./evoting_server
 */

#include "evoting_common.h"
#include "server_stub.h"

/* =======================================================================
 * main – set up listening socket, accept clients, dispatch requests
 * ===================================================================== */
int main(void)
{
    int listenfd, connfd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    int opt = 1;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); exit(1); }

    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port        = htons(EVOTING_PORT);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(listenfd, 5) < 0) {
        perror("listen"); exit(1);
    }

    printf("evoting_server listening on port %d\n", EVOTING_PORT);

    while (1) {
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &cli_len);
        if (connfd < 0) { perror("accept"); continue; }

        printf("Client connected: %s\n", inet_ntoa(cli_addr.sin_addr));

        /* Serve this client until it disconnects */
        while (1) {
            char line[MAX_LINE];
            int n = recv_line(connfd, line, sizeof(line));
            if (n < 0) break;   /* client disconnected */

            server_dispatch(connfd, line);
        }

        close(connfd);
        printf("Client disconnected.\n");
    }

    close(listenfd);
    return 0;
}

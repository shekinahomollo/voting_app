/*
 * tcp_server_async.c
 * ─────────────────────────────────────────────────────────────────
 * Concurrent Connection-Oriented Voting Server — ASYNC I/O variant
 *
 * Architecture:
 *   • Single process, single thread.
 *   • Uses select() to multiplex the listening socket and all active
 *     client connections simultaneously.
 *   • Each connection has a state machine (ClientState) that tracks
 *     how many bytes have been received for the current message.
 *   • No blocking: if a full message has not yet arrived, the client
 *     slot stays in the fd_set and is revisited next iteration.
 *   • Vote store needs no locking (single-threaded).
 *
 * Build:
 *   gcc -Wall -O2 -o tcp_server_async tcp_server_async.c
 *
 * Usage:
 *   ./tcp_server_async [port]   (default port 9001)
 * ─────────────────────────────────────────────────────────────────
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "voting_protocol.h"

/* ── client connection state ───────────────────────────────────── */
#define MAX_BUF 512

typedef struct {
    int                active;
    int                fd;
    struct sockaddr_in addr;
    uint8_t            buf[MAX_BUF];
    int                bytes_in;     /* bytes received so far */
    int                expected;     /* bytes needed for current msg */
} ClientState;

/* Determine expected message size from first byte */
static int expected_size(uint8_t opcode) {
    switch (opcode) {
        case MSG_VOTE_REQ:   return sizeof(VoteRequest);
        case MSG_TALLY_REQ:  return sizeof(TallyRequest);
        case MSG_LIST_REQ:   return sizeof(ListRequest);
        case MSG_QUIT:       return sizeof(QuitMessage);
        default:             return 1;
    }
}

/* ── send helpers ──────────────────────────────────────────────── */
static void send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return;
        p += n; len -= n;
    }
}

/* ── process a fully-received message ─────────────────────────── */
static void process_message(ClientState *cs, VoteStore *vs) {
    uint8_t opcode = cs->buf[0];
    char clistr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cs->addr.sin_addr, clistr, sizeof(clistr));

    if (opcode == MSG_VOTE_REQ) {
        VoteRequest  *req  = (VoteRequest *)cs->buf;
        VoteResponse  resp;
        resp.voter_id = req->voter_id;

        int rc = cast_vote(vs, req->voter_id, req->candidate_index);
        if (rc == 1) {
            resp.opcode = MSG_VOTE_ACK;
            snprintf(resp.message, sizeof(resp.message),
                     "Vote for %s recorded!", CANDIDATE_NAMES[req->candidate_index]);
            log_msg("ASYNC", "fd%d: Voter %u → %s\n",
                    cs->fd, req->voter_id, CANDIDATE_NAMES[req->candidate_index]);
        } else if (rc == 0) {
            resp.opcode = MSG_VOTE_ERR;
            snprintf(resp.message, sizeof(resp.message), "Already voted.");
        } else {
            resp.opcode = MSG_VOTE_ERR;
            snprintf(resp.message, sizeof(resp.message), "Invalid candidate.");
        }
        send_all(cs->fd, &resp, sizeof(resp));

    } else if (opcode == MSG_TALLY_REQ) {
        TallyResponse tr;
        fill_tally(vs, &tr);
        send_all(cs->fd, &tr, sizeof(tr));
        log_msg("ASYNC", "fd%d: Tally sent to %s\n", cs->fd, clistr);

    } else if (opcode == MSG_LIST_REQ) {
        ListResponse lr;
        fill_list(&lr);
        send_all(cs->fd, &lr, sizeof(lr));

    } else if (opcode == MSG_QUIT) {
        log_msg("ASYNC", "fd%d: Client %s QUIT\n", cs->fd, clistr);
        close(cs->fd);
        cs->active = 0;
        return;
    }

    /* Reset for next message */
    cs->bytes_in = 0;
    cs->expected = 0;
}

/* ── main ──────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : TCP_PORT;

    VoteStore vs;
    memset(&vs, 0, sizeof(vs));

    ClientState clients[FD_SETSIZE];
    memset(clients, 0, sizeof(clients));
    int max_fd = -1;

    /* Listening socket */
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Non-blocking */
    fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port        = htons(port);

    if (bind(listenfd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listenfd, BACKLOG) < 0) { perror("listen"); exit(1); }
    max_fd = listenfd;

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  TCP VOTING SERVER — ASYNC I/O (select)      ║\n");
    printf("║  Listening on port %-5d  (single process)   ║\n", port);
    printf("╚══════════════════════════════════════════════╝\n\n");

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listenfd, &rfds);

        /* Add all active client fds */
        for (int i = 0; i < FD_SETSIZE; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > max_fd) max_fd = clients[i].fd;
            }
        }

        struct timeval tv = {1, 0};
        int rc = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            perror("select"); break;
        }
        if (rc == 0) continue;   /* timeout */

        /* ── New connection ─────────────────────────────────── */
        if (FD_ISSET(listenfd, &rfds)) {
            struct sockaddr_in cli;
            socklen_t clilen = sizeof(cli);
            int connfd = accept(listenfd, (struct sockaddr *)&cli, &clilen);
            if (connfd >= 0) {
                fcntl(connfd, F_SETFL,
                      fcntl(connfd, F_GETFL, 0) | O_NONBLOCK);

                /* Find free slot */
                int slot = -1;
                for (int i = 0; i < FD_SETSIZE; i++) {
                    if (!clients[i].active) { slot = i; break; }
                }
                if (slot < 0) {
                    log_msg("ASYNC", "Max clients reached — rejecting\n");
                    close(connfd);
                } else {
                    clients[slot].active   = 1;
                    clients[slot].fd       = connfd;
                    clients[slot].addr     = cli;
                    clients[slot].bytes_in = 0;
                    clients[slot].expected = 0;
                    if (connfd > max_fd) max_fd = connfd;
                    log_msg("ASYNC", "New connection fd%d from %s:%d\n",
                            connfd, inet_ntoa(cli.sin_addr),
                            ntohs(cli.sin_port));
                }
            }
        }

        /* ── Existing connections ────────────────────────────── */
        for (int i = 0; i < FD_SETSIZE; i++) {
            if (!clients[i].active) continue;
            ClientState *cs = &clients[i];

            if (!FD_ISSET(cs->fd, &rfds)) continue;

            /* Need to know expected size: read at least 1 byte */
            if (cs->expected == 0) {
                ssize_t n = recv(cs->fd, cs->buf, 1, 0);
                if (n <= 0) {
                    log_msg("ASYNC", "fd%d disconnected\n", cs->fd);
                    close(cs->fd);
                    cs->active = 0;
                    continue;
                }
                cs->bytes_in = 1;
                cs->expected = expected_size(cs->buf[0]);
            }

            /* Read remaining bytes */
            if (cs->bytes_in < cs->expected) {
                ssize_t n = recv(cs->fd,
                                 cs->buf + cs->bytes_in,
                                 cs->expected - cs->bytes_in, 0);
                if (n <= 0) {
                    log_msg("ASYNC", "fd%d disconnected mid-message\n", cs->fd);
                    close(cs->fd);
                    cs->active = 0;
                    continue;
                }
                cs->bytes_in += n;
            }

            /* Full message received? */
            if (cs->bytes_in >= cs->expected) {
                process_message(cs, &vs);
            }
        }
    }
    return 0;
}

/*
 * tcp_server_thread.c
 * ─────────────────────────────────────────────────────────────────
 * Concurrent Connection-Oriented Voting Server — THREAD variant
 *
 * Architecture:
 *   • Main thread accepts TCP connections.
 *   • For each connection a detached pthread is spawned.
 *   • The thread serves the client for the full session lifetime.
 *   • Vote store protected by pthread_mutex.
 *   • Thread-safe logging via a separate log_mutex.
 *
 * Build:
 *   gcc -Wall -O2 -o tcp_server_thread tcp_server_thread.c -lpthread
 *
 * Usage:
 *   ./tcp_server_thread [port]   (default port 9001)
 * ─────────────────────────────────────────────────────────────────
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "voting_protocol.h"

/* ── global state ──────────────────────────────────────────────── */
static VoteStore       g_store;
static pthread_mutex_t g_mutex   = PTHREAD_MUTEX_INITIALIZER;
static int             g_listenfd = -1;

/* ── per-connection context ────────────────────────────────────── */
typedef struct {
    int                connfd;
    struct sockaddr_in cli;
} ConnCtx;

/* ── send / recv helpers ───────────────────────────────────────── */
static int send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return -1;
        p += n; len -= n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len) {
    char *p = buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0) return -1;
        p += n; len -= n;
    }
    return 0;
}

/* ── session thread ────────────────────────────────────────────── */
static void *session_thread(void *arg) {
    ConnCtx *ctx = (ConnCtx *)arg;
    int connfd   = ctx->connfd;
    char clistr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ctx->cli.sin_addr, clistr, sizeof(clistr));
    int cliport = ntohs(ctx->cli.sin_port);
    free(ctx);

    log_msg("THREAD", "TID %lu serving %s:%d\n",
            (unsigned long)pthread_self(), clistr, cliport);

    while (1) {
        uint8_t opcode;
        /* Peek at opcode without consuming */
        ssize_t peeked = recv(connfd, &opcode, 1, MSG_PEEK);
        if (peeked <= 0) break;   /* disconnect or error */

        if (opcode == MSG_VOTE_REQ) {
            VoteRequest req;
            if (recv_all(connfd, &req, sizeof(req)) < 0) break;

            VoteResponse resp;
            resp.voter_id = req.voter_id;

            pthread_mutex_lock(&g_mutex);
            int rc = cast_vote(&g_store, req.voter_id, req.candidate_index);
            pthread_mutex_unlock(&g_mutex);

            if (rc == 1) {
                resp.opcode = MSG_VOTE_ACK;
                snprintf(resp.message, sizeof(resp.message),
                         "Vote for %s recorded!", CANDIDATE_NAMES[req.candidate_index]);
                log_msg("THREAD", "TID %lu: Voter %u → %s\n",
                        (unsigned long)pthread_self(), req.voter_id,
                        CANDIDATE_NAMES[req.candidate_index]);
            } else if (rc == 0) {
                resp.opcode = MSG_VOTE_ERR;
                snprintf(resp.message, sizeof(resp.message), "Already voted.");
            } else {
                resp.opcode = MSG_VOTE_ERR;
                snprintf(resp.message, sizeof(resp.message), "Invalid candidate.");
            }
            if (send_all(connfd, &resp, sizeof(resp)) < 0) break;

        } else if (opcode == MSG_TALLY_REQ) {
            TallyRequest tr_req;
            if (recv_all(connfd, &tr_req, sizeof(tr_req)) < 0) break;

            TallyResponse tr;
            pthread_mutex_lock(&g_mutex);
            fill_tally(&g_store, &tr);
            pthread_mutex_unlock(&g_mutex);

            if (send_all(connfd, &tr, sizeof(tr)) < 0) break;
            log_msg("THREAD", "TID %lu: Tally sent to %s:%d\n",
                    (unsigned long)pthread_self(), clistr, cliport);

        } else if (opcode == MSG_LIST_REQ) {
            ListRequest lr_req;
            if (recv_all(connfd, &lr_req, sizeof(lr_req)) < 0) break;
            ListResponse lr;
            fill_list(&lr);
            if (send_all(connfd, &lr, sizeof(lr)) < 0) break;

        } else if (opcode == MSG_QUIT) {
            log_msg("THREAD", "TID %lu: %s QUIT\n",
                    (unsigned long)pthread_self(), clistr);
            break;

        } else {
            log_msg("THREAD", "Unknown opcode 0x%02x — closing\n", opcode);
            break;
        }
    }
    close(connfd);
    log_msg("THREAD", "TID %lu: Session %s:%d ended\n",
            (unsigned long)pthread_self(), clistr, cliport);
    return NULL;
}

/* ── signal handler ────────────────────────────────────────────── */
static void cleanup(int sig) {
    (void)sig;
    close(g_listenfd);
    printf("\n[SERVER] TCP Thread server shutting down.\n");
    exit(0);
}

/* ── main ──────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : TCP_PORT;

    memset(&g_store, 0, sizeof(g_store));

    g_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenfd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(g_listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port        = htons(port);

    if (bind(g_listenfd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(g_listenfd, BACKLOG) < 0) { perror("listen"); exit(1); }

    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  TCP VOTING SERVER — THREAD-CONCURRENT       ║\n");
    printf("║  Listening on port %-5d                     ║\n", port);
    printf("╚══════════════════════════════════════════════╝\n\n");

    while (1) {
        ConnCtx *ctx = malloc(sizeof(ConnCtx));
        if (!ctx) { perror("malloc"); continue; }

        socklen_t clilen = sizeof(ctx->cli);
        ctx->connfd = accept(g_listenfd, (struct sockaddr *)&ctx->cli, &clilen);
        if (ctx->connfd < 0) {
            free(ctx);
            if (errno == EINTR) continue;
            perror("accept"); continue;
        }

        log_msg("MAIN", "Connection from %s:%d — spawning thread\n",
                inet_ntoa(ctx->cli.sin_addr), ntohs(ctx->cli.sin_port));

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&tid, &attr, session_thread, ctx) != 0) {
            perror("pthread_create");
            close(ctx->connfd);
            free(ctx);
        }
        pthread_attr_destroy(&attr);
    }
    return 0;
}

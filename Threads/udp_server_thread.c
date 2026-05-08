/*
 * udp_server_thread.c
 * ─────────────────────────────────────────────────────────────────
 * Concurrent Connectionless Voting Server — THREAD variant
 *
 * Architecture:
 *   • Main thread listens on a UDP socket.
 *   • For every datagram a new POSIX thread is spawned.
 *   • The thread handles the request and exits.
 *   • Vote store is protected by a pthread_mutex.
 *   • Threads are detached so no explicit join is needed.
 *
 * Build:
 *   gcc -Wall -O2 -o udp_server_thread udp_server_thread.c -lpthread
 *
 * Usage:
 *   ./udp_server_thread [port]   (default port 9000)
 * ─────────────────────────────────────────────────────────────────
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "voting_protocol.h"

/* ── global state ──────────────────────────────────────────────── */
static VoteStore        g_store;
static pthread_mutex_t  g_mutex = PTHREAD_MUTEX_INITIALIZER;
static int              g_sockfd = -1;
static volatile int     g_running = 1;

/* ── per-request context passed to each thread ─────────────────── */
typedef struct {
    int                sockfd;
    struct sockaddr_in cli;
    socklen_t          clilen;
    uint8_t            buf[512];
    ssize_t            len;
} RequestCtx;

/* ── thread worker ─────────────────────────────────────────────── */
static void *worker_thread(void *arg) {
    RequestCtx *ctx = (RequestCtx *)arg;

    uint8_t opcode = ctx->buf[0];

    if (opcode == MSG_VOTE_REQ && ctx->len >= (ssize_t)sizeof(VoteRequest)) {
        VoteRequest  *req  = (VoteRequest *)ctx->buf;
        VoteResponse  resp;
        resp.voter_id = req->voter_id;

        pthread_mutex_lock(&g_mutex);
        int rc = cast_vote(&g_store, req->voter_id, req->candidate_index);
        pthread_mutex_unlock(&g_mutex);

        if (rc == 1) {
            resp.opcode = MSG_VOTE_ACK;
            snprintf(resp.message, sizeof(resp.message),
                     "Vote for %s recorded!", CANDIDATE_NAMES[req->candidate_index]);
            log_msg("THREAD", "TID %lu: Voter %u → %s\n",
                    (unsigned long)pthread_self(),
                    req->voter_id, CANDIDATE_NAMES[req->candidate_index]);
        } else if (rc == 0) {
            resp.opcode = MSG_VOTE_ERR;
            snprintf(resp.message, sizeof(resp.message),
                     "Voter %u already voted.", req->voter_id);
        } else {
            resp.opcode = MSG_VOTE_ERR;
            snprintf(resp.message, sizeof(resp.message),
                     "Invalid candidate index.");
        }
        sendto(ctx->sockfd, &resp, sizeof(resp), 0,
               (struct sockaddr *)&ctx->cli, ctx->clilen);

    } else if (opcode == MSG_TALLY_REQ) {
        TallyResponse tr;
        pthread_mutex_lock(&g_mutex);
        fill_tally(&g_store, &tr);
        pthread_mutex_unlock(&g_mutex);

        sendto(ctx->sockfd, &tr, sizeof(tr), 0,
               (struct sockaddr *)&ctx->cli, ctx->clilen);
        log_msg("THREAD", "TID %lu: Tally sent to %s:%d\n",
                (unsigned long)pthread_self(),
                inet_ntoa(ctx->cli.sin_addr), ntohs(ctx->cli.sin_port));

    } else if (opcode == MSG_LIST_REQ) {
        ListResponse lr;
        fill_list(&lr);
        sendto(ctx->sockfd, &lr, sizeof(lr), 0,
               (struct sockaddr *)&ctx->cli, ctx->clilen);

    } else {
        log_msg("THREAD", "Unknown opcode 0x%02x\n", opcode);
    }

    free(ctx);
    return NULL;
}

/* ── signal handler ────────────────────────────────────────────── */
static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
    if (g_sockfd >= 0) close(g_sockfd);
    printf("\n[SERVER] Shutting down threads server.\n");
    exit(0);
}

/* ── main ──────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : UDP_PORT;

    memset(&g_store, 0, sizeof(g_store));

    g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sockfd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(g_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port        = htons(port);

    if (bind(g_sockfd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("bind"); exit(1);
    }

    signal(SIGINT,  handle_sigint);
    signal(SIGTERM, handle_sigint);

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  UDP VOTING SERVER — THREAD-CONCURRENT       ║\n");
    printf("║  Listening on port %-5d                     ║\n", port);
    printf("╚══════════════════════════════════════════════╝\n\n");

    while (g_running) {
        RequestCtx *ctx = malloc(sizeof(RequestCtx));
        if (!ctx) { perror("malloc"); continue; }

        ctx->sockfd = g_sockfd;
        ctx->clilen = sizeof(ctx->cli);

        ctx->len = recvfrom(g_sockfd, ctx->buf, sizeof(ctx->buf), 0,
                            (struct sockaddr *)&ctx->cli, &ctx->clilen);
        if (ctx->len < 0) {
            free(ctx);
            if (errno == EINTR) continue;
            perror("recvfrom"); continue;
        }

        log_msg("MAIN", "Datagram from %s:%d — spawning thread\n",
                inet_ntoa(ctx->cli.sin_addr), ntohs(ctx->cli.sin_port));

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&tid, &attr, worker_thread, ctx) != 0) {
            perror("pthread_create");
            free(ctx);
        }
        pthread_attr_destroy(&attr);
    }
    return 0;
}

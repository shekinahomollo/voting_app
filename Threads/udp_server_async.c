/*
 * udp_server_async.c
 * ─────────────────────────────────────────────────────────────────
 * Concurrent Connectionless Voting Server — ASYNC I/O variant
 *
 * Architecture:
 *   • Single process, single thread — no fork, no pthread.
 *   • Uses a select()-based event loop (non-blocking socket).
 *   • Simulates async concurrency: work queue holds pending jobs;
 *     each iteration of the loop drains one job so I/O is never
 *     blocked.
 *   • Vote store needs no locking (single-threaded).
 *
 * Build:
 *   gcc -Wall -O2 -o udp_server_async udp_server_async.c
 *
 * Usage:
 *   ./udp_server_async [port]   (default port 9000)
 * ─────────────────────────────────────────────────────────────────
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "voting_protocol.h"

/* ── work-queue item ───────────────────────────────────────────── */
#define QUEUE_SIZE 256

typedef struct {
    struct sockaddr_in cli;
    socklen_t          clilen;
    uint8_t            buf[512];
    ssize_t            len;
} WorkItem;

typedef struct {
    WorkItem items[QUEUE_SIZE];
    int      head, tail, count;
} WorkQueue;

static void wq_push(WorkQueue *q, WorkItem *w) {
    if (q->count >= QUEUE_SIZE) {
        log_msg("ASYNC", "Queue full — dropping request\n");
        return;
    }
    q->items[q->tail] = *w;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
}

static int wq_pop(WorkQueue *q, WorkItem *w) {
    if (q->count == 0) return 0;
    *w = q->items[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    return 1;
}

/* ── global state ──────────────────────────────────────────────── */
static VoteStore  g_store;
static int        g_sockfd = -1;
static WorkQueue  g_queue;

/* ── process one work item ─────────────────────────────────────── */
static void process_item(WorkItem *w) {
    if (w->len < 1) return;
    uint8_t opcode = w->buf[0];

    if (opcode == MSG_VOTE_REQ && w->len >= (ssize_t)sizeof(VoteRequest)) {
        VoteRequest  *req  = (VoteRequest *)w->buf;
        VoteResponse  resp;
        resp.voter_id = req->voter_id;

        int rc = cast_vote(&g_store, req->voter_id, req->candidate_index);
        if (rc == 1) {
            resp.opcode = MSG_VOTE_ACK;
            snprintf(resp.message, sizeof(resp.message),
                     "Vote for %s recorded!", CANDIDATE_NAMES[req->candidate_index]);
            log_msg("ASYNC", "Voter %u voted for %s\n",
                    req->voter_id, CANDIDATE_NAMES[req->candidate_index]);
        } else if (rc == 0) {
            resp.opcode = MSG_VOTE_ERR;
            snprintf(resp.message, sizeof(resp.message),
                     "Voter %u already voted.", req->voter_id);
        } else {
            resp.opcode = MSG_VOTE_ERR;
            snprintf(resp.message, sizeof(resp.message), "Invalid candidate.");
        }
        sendto(g_sockfd, &resp, sizeof(resp), 0,
               (struct sockaddr *)&w->cli, w->clilen);

    } else if (opcode == MSG_TALLY_REQ) {
        TallyResponse tr;
        fill_tally(&g_store, &tr);
        sendto(g_sockfd, &tr, sizeof(tr), 0,
               (struct sockaddr *)&w->cli, w->clilen);
        log_msg("ASYNC", "Tally sent to %s:%d\n",
                inet_ntoa(w->cli.sin_addr), ntohs(w->cli.sin_port));

    } else if (opcode == MSG_LIST_REQ) {
        ListResponse lr;
        fill_list(&lr);
        sendto(g_sockfd, &lr, sizeof(lr), 0,
               (struct sockaddr *)&w->cli, w->clilen);

    } else {
        log_msg("ASYNC", "Unknown opcode 0x%02x\n", opcode);
    }
}

/* ── signal handler ────────────────────────────────────────────── */
static void cleanup(int sig) {
    (void)sig;
    close(g_sockfd);
    printf("\n[SERVER] Async server shutting down.\n");
    exit(0);
}

/* ── main ──────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : UDP_PORT;

    memset(&g_store, 0, sizeof(g_store));
    memset(&g_queue, 0, sizeof(g_queue));

    g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sockfd < 0) { perror("socket"); exit(1); }

    /* Make socket non-blocking */
    int flags = fcntl(g_sockfd, F_GETFL, 0);
    fcntl(g_sockfd, F_SETFL, flags | O_NONBLOCK);

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

    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  UDP VOTING SERVER — ASYNC I/O (select)      ║\n");
    printf("║  Listening on port %-5d  (single process)   ║\n", port);
    printf("╚══════════════════════════════════════════════╝\n\n");

    /* ── event loop ──────────────────────────────────────────── */
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_sockfd, &rfds);

        /* Short timeout so we can drain the work queue */
        struct timeval tv = {0, 5000};  /* 5 ms */
        int rc = select(g_sockfd + 1, &rfds, NULL, NULL, &tv);

        if (rc < 0) {
            if (errno == EINTR) continue;
            perror("select"); break;
        }

        /* ── I/O phase: enqueue all available datagrams ─────── */
        if (rc > 0 && FD_ISSET(g_sockfd, &rfds)) {
            WorkItem w;
            while (1) {
                w.clilen = sizeof(w.cli);
                w.len = recvfrom(g_sockfd, w.buf, sizeof(w.buf), 0,
                                 (struct sockaddr *)&w.cli, &w.clilen);
                if (w.len < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    perror("recvfrom"); break;
                }
                log_msg("ASYNC", "Queued datagram from %s:%d\n",
                        inet_ntoa(w.cli.sin_addr), ntohs(w.cli.sin_port));
                wq_push(&g_queue, &w);
            }
        }

        /* ── processing phase: handle one work item per cycle ── */
        WorkItem job;
        if (wq_pop(&g_queue, &job)) {
            process_item(&job);
        }
    }
    return 0;
}

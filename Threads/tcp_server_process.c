/*
 * tcp_server_process.c
 * ─────────────────────────────────────────────────────────────────
 * Concurrent Connection-Oriented Voting Server — PROCESS variant
 *
 * Architecture:
 *   • Main process accepts TCP connections.
 *   • For each accepted connection it fork()s a child.
 *   • The child serves the client over a full session (multiple
 *     requests until MSG_QUIT or connection close).
 *   • Vote store lives in POSIX shared memory (same as UDP variant).
 *   • SIGCHLD reaps zombie children.
 *
 * Build:
 *   gcc -Wall -O2 -o tcp_server_process tcp_server_process.c -lrt
 *
 * Usage:
 *   ./tcp_server_process [port]   (default port 9001)
 * ─────────────────────────────────────────────────────────────────
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <semaphore.h>
#include "voting_protocol.h"

#define SHM_NAME "/voting_tcp_shm"

typedef struct {
    VoteStore store;
    sem_t     lock;
} SharedData;

static SharedData *shdata  = NULL;
static int         listenfd = -1;

/* ── helpers: send / recv exact bytes over TCP ─────────────────── */
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

/* ── session handler (runs in child) ───────────────────────────── */
static void serve_client(int connfd, struct sockaddr_in *cli) {
    char clistr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli->sin_addr, clistr, sizeof(clistr));

    log_msg("CHILD", "PID %d serving %s:%d\n",
            getpid(), clistr, ntohs(cli->sin_port));

    while (1) {
        /* Read opcode first */
        uint8_t opcode;
        if (recv(connfd, &opcode, 1, MSG_PEEK) <= 0) break;

        if (opcode == MSG_VOTE_REQ) {
            VoteRequest req;
            if (recv_all(connfd, &req, sizeof(req)) < 0) break;

            VoteResponse resp;
            resp.voter_id = req.voter_id;

            sem_wait(&shdata->lock);
            int rc = cast_vote(&shdata->store, req.voter_id, req.candidate_index);
            sem_post(&shdata->lock);

            if (rc == 1) {
                resp.opcode = MSG_VOTE_ACK;
                snprintf(resp.message, sizeof(resp.message),
                         "Vote for %s recorded!", CANDIDATE_NAMES[req.candidate_index]);
                log_msg("CHILD", "PID %d: Voter %u → %s\n",
                        getpid(), req.voter_id, CANDIDATE_NAMES[req.candidate_index]);
            } else if (rc == 0) {
                resp.opcode = MSG_VOTE_ERR;
                snprintf(resp.message, sizeof(resp.message),
                         "Already voted.");
            } else {
                resp.opcode = MSG_VOTE_ERR;
                snprintf(resp.message, sizeof(resp.message),
                         "Invalid candidate.");
            }
            if (send_all(connfd, &resp, sizeof(resp)) < 0) break;

        } else if (opcode == MSG_TALLY_REQ) {
            TallyRequest tr_req;
            if (recv_all(connfd, &tr_req, sizeof(tr_req)) < 0) break;

            TallyResponse tr;
            sem_wait(&shdata->lock);
            fill_tally(&shdata->store, &tr);
            sem_post(&shdata->lock);
            if (send_all(connfd, &tr, sizeof(tr)) < 0) break;
            log_msg("CHILD", "PID %d: Tally sent to %s\n", getpid(), clistr);

        } else if (opcode == MSG_LIST_REQ) {
            ListRequest lr_req;
            if (recv_all(connfd, &lr_req, sizeof(lr_req)) < 0) break;

            ListResponse lr;
            fill_list(&lr);
            if (send_all(connfd, &lr, sizeof(lr)) < 0) break;

        } else if (opcode == MSG_QUIT) {
            log_msg("CHILD", "PID %d: Client %s disconnected (QUIT)\n",
                    getpid(), clistr);
            break;

        } else {
            log_msg("CHILD", "PID %d: Unknown opcode 0x%02x — closing\n",
                    getpid(), opcode);
            break;
        }
    }
    close(connfd);
    log_msg("CHILD", "PID %d: Session ended for %s\n", getpid(), clistr);
}

/* ── signals ───────────────────────────────────────────────────── */
static void reap_children(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void cleanup(int sig) {
    (void)sig;
    if (listenfd >= 0) close(listenfd);
    shm_unlink(SHM_NAME);
    printf("\n[SERVER] TCP Process server shutting down.\n");
    exit(0);
}

/* ── main ──────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : TCP_PORT;

    /* Shared memory */
    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (shm_fd < 0) { perror("shm_open"); exit(1); }
    ftruncate(shm_fd, sizeof(SharedData));
    shdata = mmap(NULL, sizeof(SharedData),
                  PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shdata == MAP_FAILED) { perror("mmap"); exit(1); }
    memset(shdata, 0, sizeof(*shdata));
    sem_init(&shdata->lock, 1, 1);

    /* Socket */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port        = htons(port);

    if (bind(listenfd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listenfd, BACKLOG) < 0) { perror("listen"); exit(1); }

    signal(SIGCHLD, reap_children);
    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  TCP VOTING SERVER — PROCESS-CONCURRENT      ║\n");
    printf("║  Listening on port %-5d                     ║\n", port);
    printf("║  PID: %-38d║\n", getpid());
    printf("╚══════════════════════════════════════════════╝\n\n");

    while (1) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int connfd = accept(listenfd, (struct sockaddr *)&cli, &clilen);
        if (connfd < 0) {
            if (errno == EINTR) continue;
            perror("accept"); continue;
        }

        log_msg("MAIN", "Connection from %s:%d — forking\n",
                inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); close(connfd); continue; }

        if (pid == 0) {
            /* child: close the listening socket, serve client */
            close(listenfd);
            serve_client(connfd, &cli);
            exit(0);
        }
        /* parent: close the connected socket, loop back */
        close(connfd);
    }
    return 0;
}

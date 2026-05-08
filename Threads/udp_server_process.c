/*
 * udp_server_process.c
 * ─────────────────────────────────────────────────────────────────
 * Concurrent Connectionless Voting Server — PROCESS variant
 *
 * Architecture:
 *   • Main process listens on a UDP socket.
 *   • For every datagram received it fork()s a worker child.
 *   • The child handles the request, writes result to a shared
 *     memory segment, then exits.
 *   • SIGCHLD is caught to reap zombies.
 *   • Vote store lives in POSIX shared memory so all child
 *     processes see a consistent view.
 *
 * Build:
 *   gcc -Wall -O2 -o udp_server_process udp_server_process.c -lrt
 *
 * Usage:
 *   ./udp_server_process [port]   (default port 9000)
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

#define SHM_NAME  "/voting_shm"
#define SEM_NAME  "/voting_sem"

/* Shared-memory layout */
typedef struct {
    VoteStore  store;
    sem_t      lock;       /* protects store */
} SharedData;

static SharedData  *shdata = NULL;
static int          sockfd  = -1;

/* ── signal handlers ────────────────────────────────────────────── */
static void reap_children(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void cleanup(int sig) {
    (void)sig;
    if (sockfd >= 0) close(sockfd);
    shm_unlink(SHM_NAME);
    sem_destroy(&shdata->lock);
    printf("\n[SERVER] Shutting down.\n");
    exit(0);
}

/* ── worker: called in child process ────────────────────────────── */
static void handle_request(int fd,
                            struct sockaddr_in *cli,
                            socklen_t clilen,
                            uint8_t *buf, ssize_t len)
{
    if (len < 1) return;

    uint8_t opcode = buf[0];

    if (opcode == MSG_VOTE_REQ && len >= (ssize_t)sizeof(VoteRequest)) {
        VoteRequest *req = (VoteRequest *)buf;
        VoteResponse resp;
        resp.voter_id = req->voter_id;

        sem_wait(&shdata->lock);
        int rc = cast_vote(&shdata->store, req->voter_id, req->candidate_index);
        sem_post(&shdata->lock);

        if (rc == 1) {
            resp.opcode = MSG_VOTE_ACK;
            snprintf(resp.message, sizeof(resp.message),
                     "Vote for %s recorded!", CANDIDATE_NAMES[req->candidate_index]);
            log_msg("WORKER", "PID %d: Voter %u voted for %s\n",
                    getpid(), req->voter_id,
                    CANDIDATE_NAMES[req->candidate_index]);
        } else if (rc == 0) {
            resp.opcode = MSG_VOTE_ERR;
            snprintf(resp.message, sizeof(resp.message),
                     "Voter %u has already voted.", req->voter_id);
        } else {
            resp.opcode = MSG_VOTE_ERR;
            snprintf(resp.message, sizeof(resp.message),
                     "Invalid candidate index %u.", req->candidate_index);
        }
        sendto(fd, &resp, sizeof(resp), 0,
               (struct sockaddr *)cli, clilen);

    } else if (opcode == MSG_TALLY_REQ) {
        TallyResponse tr;

        sem_wait(&shdata->lock);
        fill_tally(&shdata->store, &tr);
        sem_post(&shdata->lock);

        sendto(fd, &tr, sizeof(tr), 0,
               (struct sockaddr *)cli, clilen);
        log_msg("WORKER", "PID %d: Tally sent to %s:%d\n",
                getpid(), inet_ntoa(cli->sin_addr), ntohs(cli->sin_port));

    } else if (opcode == MSG_LIST_REQ) {
        ListResponse lr;
        fill_list(&lr);
        sendto(fd, &lr, sizeof(lr), 0,
               (struct sockaddr *)cli, clilen);

    } else {
        log_msg("WORKER", "PID %d: Unknown opcode 0x%02x\n", getpid(), opcode);
    }
}

/* ── main ─────────────────────────────────────────────────────────*/
int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : UDP_PORT;

    /* ── shared memory ─────────────────────────────────────────── */
    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (shm_fd < 0) { perror("shm_open"); exit(1); }
    ftruncate(shm_fd, sizeof(SharedData));
    shdata = (SharedData *)mmap(NULL, sizeof(SharedData),
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED, shm_fd, 0);
    if (shdata == MAP_FAILED) { perror("mmap"); exit(1); }
    memset(shdata, 0, sizeof(*shdata));

    /* POSIX unnamed semaphore shared between processes */
    if (sem_init(&shdata->lock, 1 /*pshared*/, 1) < 0) {
        perror("sem_init"); exit(1);
    }

    /* ── socket ────────────────────────────────────────────────── */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("bind"); exit(1);
    }

    /* ── signals ───────────────────────────────────────────────── */
    signal(SIGCHLD, reap_children);
    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  UDP VOTING SERVER — PROCESS-CONCURRENT      ║\n");
    printf("║  Listening on port %-5d                     ║\n", port);
    printf("║  PID: %-38d║\n", getpid());
    printf("╚══════════════════════════════════════════════╝\n\n");

    /* ── main receive loop ─────────────────────────────────────── */
    uint8_t buf[512];
    struct sockaddr_in cli;
    socklen_t clilen;

    while (1) {
        clilen = sizeof(cli);
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&cli, &clilen);
        if (n < 0) {
            if (errno == EINTR) continue;   /* interrupted by SIGCHLD */
            perror("recvfrom"); continue;
        }

        log_msg("MAIN", "Datagram from %s:%d (%zd bytes) — forking\n",
                inet_ntoa(cli.sin_addr), ntohs(cli.sin_port), n);

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); continue; }

        if (pid == 0) {
            /* ── child ─────────────────────────────────────────── */
            handle_request(sockfd, &cli, clilen, buf, n);
            exit(0);
        }
        /* parent loops back immediately */
    }
    return 0;
}

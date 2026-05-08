/*
 * tcp_client.c
 * ─────────────────────────────────────────────────────────────────
 * Voting Client — CONNECTION-ORIENTED (TCP)
 *
 * Compatible with:
 *   tcp_server_process.c
 *   tcp_server_thread.c
 *   tcp_server_async.c
 *
 * The TCP client establishes ONE persistent connection to the server
 * and can send multiple requests before disconnecting.  This
 * demonstrates the stateful nature of TCP (vs. the stateless UDP).
 *
 * Build:
 *   gcc -Wall -O2 -o tcp_client tcp_client.c
 *
 * Usage:
 *   ./tcp_client <server_ip> [port]
 * ─────────────────────────────────────────────────────────────────
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "voting_protocol.h"

static int g_connfd = -1;

/* ── reliable send / receive ────────────────────────────────────── */
static int tcp_send(const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(g_connfd, p, len, 0);
        if (n <= 0) { perror("send"); return -1; }
        p += n; len -= n;
    }
    return 0;
}

static int tcp_recv(void *buf, size_t len) {
    char *p = buf;
    while (len > 0) {
        ssize_t n = recv(g_connfd, p, len, 0);
        if (n <= 0) {
            if (n == 0) fprintf(stderr, "Server closed connection.\n");
            else        perror("recv");
            return -1;
        }
        p += n; len -= n;
    }
    return 0;
}

/* ── actions ────────────────────────────────────────────────────── */
static void do_list(void) {
    ListRequest req = {MSG_LIST_REQ};
    if (tcp_send(&req, sizeof(req)) < 0) return;

    ListResponse resp;
    if (tcp_recv(&resp, sizeof(resp)) < 0) return;
    if (resp.opcode != MSG_LIST_RESP) {
        printf("Unexpected response 0x%02x\n", resp.opcode); return;
    }
    printf("\n── Candidates ──────────────────────────────\n");
    for (int i = 0; i < resp.count; i++)
        printf("  [%d] %s\n", i, resp.names[i]);
    printf("────────────────────────────────────────────\n\n");
}

static void do_vote(uint32_t voter_id, const char *voter_name) {
    do_list();

    printf("Enter candidate number (0-%d): ", NUM_CANDIDATES - 1);
    fflush(stdout);

    int choice;
    if (scanf("%d", &choice) != 1 || choice < 0 || choice >= NUM_CANDIDATES) {
        printf("Invalid choice.\n"); return;
    }

    VoteRequest req;
    req.opcode          = MSG_VOTE_REQ;
    req.voter_id        = voter_id;
    req.candidate_index = (uint8_t)choice;
    strncpy(req.voter_name, voter_name, sizeof(req.voter_name) - 1);

    if (tcp_send(&req, sizeof(req)) < 0) return;

    VoteResponse resp;
    if (tcp_recv(&resp, sizeof(resp)) < 0) return;

    if (resp.opcode == MSG_VOTE_ACK)
        printf("\n✓ SUCCESS: %s\n\n", resp.message);
    else
        printf("\n✗ ERROR: %s\n\n", resp.message);
}

static void do_tally(void) {
    TallyRequest req = {MSG_TALLY_REQ};
    if (tcp_send(&req, sizeof(req)) < 0) return;

    TallyResponse resp;
    if (tcp_recv(&resp, sizeof(resp)) < 0) return;
    if (resp.opcode != MSG_TALLY_RESP) {
        printf("Unexpected response.\n"); return;
    }
    print_tally(&resp);
}

static void do_quit(void) {
    QuitMessage qm = {MSG_QUIT};
    tcp_send(&qm, sizeof(qm));
    close(g_connfd);
    g_connfd = -1;
}

/* ── menu ───────────────────────────────────────────────────────── */
static void print_menu(void) {
    printf("┌──────────────────────────────────┐\n");
    printf("│   TCP VOTING CLIENT              │\n");
    printf("├──────────────────────────────────┤\n");
    printf("│  1. List Candidates              │\n");
    printf("│  2. Cast My Vote                 │\n");
    printf("│  3. View Current Tally           │\n");
    printf("│  4. Quit                         │\n");
    printf("└──────────────────────────────────┘\n");
    printf("Choice: ");
    fflush(stdout);
}

/* ── main ───────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip> [port]\n", argv[0]);
        exit(1);
    }

    int port = (argc > 2) ? atoi(argv[2]) : TCP_PORT;
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    uint32_t voter_id = (uint32_t)(rand() % 100000) + 1;

    char voter_name[32];
    printf("Enter your name: ");
    fflush(stdout);
    if (fgets(voter_name, sizeof(voter_name), stdin) == NULL)
        strcpy(voter_name, "Voter");
    voter_name[strcspn(voter_name, "\n")] = '\0';

    /* Connect to server */
    g_connfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_connfd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(port);
    if (inet_pton(AF_INET, argv[1], &srv.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP: %s\n", argv[1]);
        exit(1);
    }

    if (connect(g_connfd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("connect"); exit(1);
    }

    printf("\n[TCP CLIENT] Voter: %s  ID: %u  Connected to %s:%d\n\n",
           voter_name, voter_id, argv[1], port);

    int choice;
    while (g_connfd >= 0) {
        print_menu();
        if (scanf("%d", &choice) != 1) { getchar(); continue; }

        switch (choice) {
            case 1: do_list();                           break;
            case 2: do_vote(voter_id, voter_name);       break;
            case 3: do_tally();                          break;
            case 4: do_quit(); printf("Goodbye!\n");     return 0;
            default: printf("Invalid option.\n");
        }
    }
    return 0;
}

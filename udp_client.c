/*
 * udp_client.c
 * ─────────────────────────────────────────────────────────────────
 * Voting Client — CONNECTIONLESS (UDP)
 *
 * Compatible with:
 *   udp_server_process.c
 *   udp_server_thread.c
 *   udp_server_async.c
 *
 * Build:
 *   gcc -Wall -O2 -o udp_client udp_client.c
 *
 * Usage:
 *   ./udp_client <server_ip> [port]
 * ─────────────────────────────────────────────────────────────────
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "voting_protocol.h"

#define TIMEOUT_SEC 5

static int g_sockfd;
static struct sockaddr_in g_srv;
static socklen_t g_srvlen;

/* ── send and receive with timeout ────────────────────────────── */
static ssize_t udp_exchange(const void *send_buf, size_t send_len,
                             void *recv_buf, size_t recv_len)
{
    if (sendto(g_sockfd, send_buf, send_len, 0,
               (struct sockaddr *)&g_srv, g_srvlen) < 0) {
        perror("sendto"); return -1;
    }

    /* Use select() for receive timeout */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(g_sockfd, &rfds);
    struct timeval tv = {TIMEOUT_SEC, 0};

    int rc = select(g_sockfd + 1, &rfds, NULL, NULL, &tv);
    if (rc <= 0) {
        fprintf(stderr, "Timeout waiting for server response.\n");
        return -1;
    }

    return recvfrom(g_sockfd, recv_buf, recv_len, 0, NULL, NULL);
}

/* ── action: list candidates ───────────────────────────────────── */
static void do_list(void) {
    ListRequest req = {MSG_LIST_REQ};
    ListResponse resp;

    if (udp_exchange(&req, sizeof(req), &resp, sizeof(resp)) < 0) return;
    if (resp.opcode != MSG_LIST_RESP) {
        printf("Unexpected response 0x%02x\n", resp.opcode); return;
    }
    printf("\n── Candidates ──────────────────────────────\n");
    for (int i = 0; i < resp.count; i++)
        printf("  [%d] %s\n", i, resp.names[i]);
    printf("────────────────────────────────────────────\n\n");
}

/* ── action: cast vote ─────────────────────────────────────────── */
static void do_vote(uint32_t voter_id, const char *voter_name) {
    /* List candidates first */
    do_list();

    printf("Enter candidate number (0-%d): ", NUM_CANDIDATES - 1);
    fflush(stdout);

    int choice;
    if (scanf("%d", &choice) != 1 || choice < 0 || choice >= NUM_CANDIDATES) {
        printf("Invalid choice.\n"); return;
    }

    VoteRequest req;
    req.opcode           = MSG_VOTE_REQ;
    req.voter_id         = voter_id;
    req.candidate_index  = (uint8_t)choice;
    strncpy(req.voter_name, voter_name, sizeof(req.voter_name) - 1);

    VoteResponse resp;
    if (udp_exchange(&req, sizeof(req), &resp, sizeof(resp)) < 0) return;

    if (resp.opcode == MSG_VOTE_ACK)
        printf("\n✓ SUCCESS: %s\n\n", resp.message);
    else
        printf("\n✗ ERROR: %s\n\n", resp.message);
}

/* ── action: get tally ─────────────────────────────────────────── */
static void do_tally(void) {
    TallyRequest req = {MSG_TALLY_REQ};
    TallyResponse resp;

    if (udp_exchange(&req, sizeof(req), &resp, sizeof(resp)) < 0) return;
    if (resp.opcode != MSG_TALLY_RESP) {
        printf("Unexpected response.\n"); return;
    }
    print_tally(&resp);
}

/* ── menu ──────────────────────────────────────────────────────── */
static void print_menu(void) {
    printf("┌──────────────────────────────────┐\n");
    printf("│   UDP VOTING CLIENT              │\n");
    printf("├──────────────────────────────────┤\n");
    printf("│  1. List Candidates              │\n");
    printf("│  2. Cast My Vote                 │\n");
    printf("│  3. View Current Tally           │\n");
    printf("│  4. Quit                         │\n");
    printf("└──────────────────────────────────┘\n");
    printf("Choice: ");
    fflush(stdout);
}

/* ── main ──────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip> [port]\n", argv[0]);
        exit(1);
    }

    int port = (argc > 2) ? atoi(argv[2]) : UDP_PORT;
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    uint32_t voter_id = (uint32_t)(rand() % 100000) + 1;

    char voter_name[32];
    printf("Enter your name: ");
    fflush(stdout);
    if (fgets(voter_name, sizeof(voter_name), stdin) == NULL)
        strcpy(voter_name, "Voter");
    voter_name[strcspn(voter_name, "\n")] = '\0';

    /* Socket setup */
    g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sockfd < 0) { perror("socket"); exit(1); }

    memset(&g_srv, 0, sizeof(g_srv));
    g_srv.sin_family = AF_INET;
    g_srv.sin_port   = htons(port);
    if (inet_pton(AF_INET, argv[1], &g_srv.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP: %s\n", argv[1]);
        exit(1);
    }
    g_srvlen = sizeof(g_srv);

    printf("\n[UDP CLIENT] Voter: %s  ID: %u  Server: %s:%d\n\n",
           voter_name, voter_id, argv[1], port);

    int choice;
    while (1) {
        print_menu();
        if (scanf("%d", &choice) != 1) { getchar(); continue; }

        switch (choice) {
            case 1: do_list();                           break;
            case 2: do_vote(voter_id, voter_name);       break;
            case 3: do_tally();                          break;
            case 4: printf("Goodbye!\n"); close(g_sockfd); exit(0);
            default: printf("Invalid option.\n");
        }
    }
    return 0;
}

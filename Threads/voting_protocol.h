#ifndef VOTING_PROTOCOL_H
#define VOTING_PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <stdint.h>
#include <signal.h>

/* ─────────────────────────────────────────────
   PORTS & ADDRESSES
   ───────────────────────────────────────────── */
#define UDP_PORT        9000   /* Connectionless (UDP) */
#define TCP_PORT        9001   /* Connection-Oriented (TCP) */
#define BACKLOG         50     /* TCP listen queue */
#define MAX_CLIENTS     100

/* ─────────────────────────────────────────────
   CANDIDATES
   ───────────────────────────────────────────── */
#define NUM_CANDIDATES  5

static const char *CANDIDATE_NAMES[NUM_CANDIDATES] = {
    "Alice Johnson",
    "Bob Martinez",
    "Carol Nguyen",
    "David Osei",
    "Eva Petrov"
};

/* ─────────────────────────────────────────────
   MESSAGE TYPES (1 byte opcode)
   ───────────────────────────────────────────── */
#define MSG_VOTE_REQ    0x01   /* Client → Server: cast vote  */
#define MSG_VOTE_ACK    0x02   /* Server → Client: vote ok     */
#define MSG_VOTE_ERR    0x03   /* Server → Client: error       */
#define MSG_TALLY_REQ   0x04   /* Client → Server: get results */
#define MSG_TALLY_RESP  0x05   /* Server → Client: results     */
#define MSG_LIST_REQ    0x06   /* Client → Server: list cands  */
#define MSG_LIST_RESP   0x07   /* Server → Client: candidate list */
#define MSG_QUIT        0xFF   /* Client → Server: disconnect  */

/* ─────────────────────────────────────────────
   WIRE STRUCTURES  (packed for byte-exact I/O)
   ───────────────────────────────────────────── */
#pragma pack(push, 1)

typedef struct {
    uint8_t  opcode;             /* MSG_VOTE_REQ          */
    uint32_t voter_id;           /* unique voter ID        */
    uint8_t  candidate_index;    /* 0 … NUM_CANDIDATES-1  */
    char     voter_name[32];     /* null-terminated        */
} VoteRequest;

typedef struct {
    uint8_t  opcode;             /* MSG_VOTE_ACK / ERR    */
    uint32_t voter_id;
    char     message[64];
} VoteResponse;

typedef struct {
    uint8_t  opcode;             /* MSG_TALLY_REQ         */
} TallyRequest;

typedef struct {
    uint8_t  opcode;             /* MSG_TALLY_RESP        */
    uint32_t votes[NUM_CANDIDATES];
    uint32_t total_votes;
    char     winner[32];
} TallyResponse;

typedef struct {
    uint8_t opcode;              /* MSG_LIST_REQ          */
} ListRequest;

typedef struct {
    uint8_t opcode;              /* MSG_LIST_RESP         */
    uint8_t count;
    char    names[NUM_CANDIDATES][32];
} ListResponse;

typedef struct {
    uint8_t opcode;              /* MSG_QUIT              */
} QuitMessage;

#pragma pack(pop)

/* ─────────────────────────────────────────────
   SHARED VOTE STORE  (used inside server only)
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t votes[NUM_CANDIDATES];
    uint32_t voter_ids[10000];   /* simple duplicate check */
    int      voter_count;
    int      total_votes;
} VoteStore;

/* ─────────────────────────────────────────────
   UTILITY HELPERS
   ───────────────────────────────────────────── */
static inline void log_msg(const char *tag, const char *fmt, ...) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char tbuf[20];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_info);

    va_list ap;
    va_start(ap, fmt);
    printf("[%s][%s] ", tbuf, tag);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* Check if voter already voted */
static inline int already_voted(VoteStore *vs, uint32_t voter_id) {
    for (int i = 0; i < vs->voter_count; i++)
        if (vs->voter_ids[i] == voter_id) return 1;
    return 0;
}

/* Register a vote; returns 1 on success, 0 on duplicate */
static inline int cast_vote(VoteStore *vs, uint32_t voter_id, uint8_t cand) {
    if (cand >= NUM_CANDIDATES) return -1;
    if (already_voted(vs, voter_id)) return 0;
    vs->voter_ids[vs->voter_count++] = voter_id;
    vs->votes[cand]++;
    vs->total_votes++;
    return 1;
}

/* Fill a TallyResponse from the store */
static inline void fill_tally(VoteStore *vs, TallyResponse *tr) {
    tr->opcode = MSG_TALLY_RESP;
    tr->total_votes = vs->total_votes;
    int winner = 0;
    for (int i = 0; i < NUM_CANDIDATES; i++) {
        tr->votes[i] = vs->votes[i];
        if (vs->votes[i] > vs->votes[winner]) winner = i;
    }
    strncpy(tr->winner,
            vs->total_votes ? CANDIDATE_NAMES[winner] : "No votes yet",
            sizeof(tr->winner) - 1);
}

/* Fill a ListResponse */
static inline void fill_list(ListResponse *lr) {
    lr->opcode = MSG_LIST_RESP;
    lr->count  = NUM_CANDIDATES;
    for (int i = 0; i < NUM_CANDIDATES; i++)
        strncpy(lr->names[i], CANDIDATE_NAMES[i], 31);
}

/* Pretty-print a tally on the client side */
static inline void print_tally(const TallyResponse *tr) {
    printf("\n╔══════════════════════════════════════╗\n");
    printf("║         VOTING RESULTS               ║\n");
    printf("╠══════════════════════════════════════╣\n");
    for (int i = 0; i < NUM_CANDIDATES; i++) {
        int pct = tr->total_votes
                  ? (int)(tr->votes[i] * 100 / tr->total_votes)
                  : 0;
        char bar[21] = {0};
        int filled = pct / 5;
        for (int j = 0; j < 20; j++)
            bar[j] = (j < filled) ? '#' : '-';
        printf("║ %-16s [%s] %3d%% (%u)\n",
               CANDIDATE_NAMES[i], bar, pct, tr->votes[i]);
    }
    printf("╠══════════════════════════════════════╣\n");
    printf("║ Total votes : %-24u║\n", tr->total_votes);
    printf("║ Current lead: %-24s║\n", tr->winner);
    printf("╚══════════════════════════════════════╝\n\n");
}

#endif /* VOTING_PROTOCOL_H */

/*
 * evoting_common.h
 *
 * Shared definitions for the hand-rolled RPC evoting application.
 *
 * Transport: TCP on EVOTING_PORT.
 *
 * Wire protocol (text-based, no XDR):
 *   Every message is a newline-terminated line of pipe-delimited fields.
 *   The first field is always the procedure code (an integer).
 *
 *   Request:  "<PROC>|field1|field2|...\n"
 *   Response: "<STATUS>|field1|field2|...\n"
 *             where STATUS is 0 (error/false) or 1 (ok/true) for simple
 *             calls, or a specific integer for VERIFY_VOTER.
 *             Multi-record responses use multiple lines terminated by
 *             a final "END\n" sentinel.
 *
 * Procedure codes:
 *   1  ADMIN_EXISTS        req: "1\n"
 *                          res: "0\n" or "1\n"
 *
 *   2  CREATE_ADMIN        req: "2|name|regNo|password\n"
 *                          res: "0\n" or "1\n"
 *
 *   3  VERIFY_ADMIN        req: "3|regNo|password\n"
 *                          res: "0\n" or "1\n"
 *
 *   4  ADD_POSITION        req: "4|positionName\n"
 *                          res: "0\n" or "1\n"
 *
 *   5  REGISTER_VOTER      req: "5|name|regNo|password\n"
 *                          res: "0\n" or "1\n"
 *
 *   6  REGISTER_CONTESTANT req: "6|name|regNo|position\n"
 *                          res: "0\n" or "1\n"
 *
 *   7  GET_POSITIONS       req: "7\n"
 *                          res: one "positionName\n" per position,
 *                               then "END\n"
 *
 *   8  GET_CONTESTANTS     req: "8\n"
 *                          res: one "name|regNo|position|votes\n" per
 *                               contestant, then "END\n"
 *
 *   9  VERIFY_VOTER        req: "9|regNo|password\n"
 *                          res: "0\n" not found / bad creds
 *                               "1\n" ok, not yet voted
 *                               "2\n" ok, already voted
 *
 *  10  CAST_VOTE           req: "10|regNo|password|choices\n"
 *                               choices = pipe-delimited contestant regNos,
 *                               one slot per position, empty = skip
 *                               e.g. "REG001||REG007"
 *                          res: "0\n" or "1\n"
 *
 *  11  TALLY_VOTES         req: "11\n"
 *                          res: "COUNTS\n"
 *                               one "name (regNo) for position - N votes\n"
 *                               "WINNERS\n"
 *                               one "name (regNo) for position\n" per winner
 *                               "END\n"
 */

#ifndef EVOTING_COMMON_H
#define EVOTING_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* -----------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------- */
#define EVOTING_PORT   9876
#define MAX_NAME       50
#define MAX_REGNO      20
#define MAX_PASSWORD   20
#define MAX_POSITION   30
#define MAX_RECORDS    100
#define MAX_LINE       256   /* longest single wire line                  */
#define MAX_MSG        4096  /* largest single outbound message           */

/* -----------------------------------------------------------------------
 * Procedure codes
 * --------------------------------------------------------------------- */
#define PROC_ADMIN_EXISTS        1
#define PROC_CREATE_ADMIN        2
#define PROC_VERIFY_ADMIN        3
#define PROC_ADD_POSITION        4
#define PROC_REGISTER_VOTER      5
#define PROC_REGISTER_CONTESTANT 6
#define PROC_GET_POSITIONS       7
#define PROC_GET_CONTESTANTS     8
#define PROC_VERIFY_VOTER        9
#define PROC_CAST_VOTE          10
#define PROC_TALLY_VOTES        11

/* -----------------------------------------------------------------------
 * Shared data structures (used on both sides)
 * --------------------------------------------------------------------- */
typedef struct {
    char name[MAX_NAME];
    char regNo[MAX_REGNO];
    char password[MAX_PASSWORD];
} admin_t;

typedef struct {
    char name[MAX_NAME];
    char regNo[MAX_REGNO];
    char password[MAX_PASSWORD];
    int  voted;
} voter_t;

typedef struct {
    char name[MAX_NAME];
    char regNo[MAX_REGNO];
    char position[MAX_POSITION];
    int  votes;
} contestant_t;

/* -----------------------------------------------------------------------
 * send_line / recv_line
 *
 * send_line: writes a complete "\n"-terminated string to a socket fd.
 * recv_line: reads characters from fd into buf (max len-1) up to '\n'
 *            or EOF; null-terminates, strips the trailing '\n'.
 *            Returns number of characters placed in buf (>=0) or -1 on
 *            connection close / error.
 * --------------------------------------------------------------------- */
static inline int send_line(int fd, const char *msg)
{
    size_t len = strlen(msg);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, msg + sent, len - sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static inline int recv_line(int fd, char *buf, int len)
{
    int i = 0;
    while (i < len - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return -1;
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

#endif /* EVOTING_COMMON_H */

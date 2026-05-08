#ifndef CLIENT_STUB_H
#define CLIENT_STUB_H

#include "evoting_common.h"

/* -----------------------------------------------------------------------
 * EXTERNAL GLOBALS
 * --------------------------------------------------------------------- */
extern int g_sock; // Socket descriptor defined in the .c file

/* -----------------------------------------------------------------------
 * REMOTE PROCEDURE STUBS
 * --------------------------------------------------------------------- */

/** Returns 1 if an admin record exists, 0 if not, -1 on error. */
int stub_admin_exists(void);

/** Returns 1 on success, 0 on failure, -1 on error. */
int stub_create_admin(const admin_t *a);

/** Returns 1 if credentials match, 0 otherwise, -1 on error. */
int stub_verify_admin(const char *regNo, const char *password);

/** Returns 1 on success, 0 on failure, -1 on error. */
int stub_add_position(const char *position);

/** Returns 1 on success, 0 on failure, -1 on error. */
int stub_register_voter(const voter_t *v);

/** Returns 1 on success, 0 on failure, -1 on error. */
int stub_register_contestant(const contestant_t *c);

/** 
 * Fills positions buffer with names from server. 
 * Returns number of positions or -1. 
 */
int stub_get_positions(char positions[][MAX_POSITION], int maxPos);

/** 
 * Fills contestants array with records from server. 
 * Returns number of contestants or -1. 
 */
int stub_get_contestants(contestant_t contestants[], int maxCon);

/** Returns 0=bad creds, 1=ok, 2=already voted, -1=error. */
int stub_verify_voter(const char *regNo, const char *password);

/** Returns 1 on success, 0 on failure, -1 on error. */
int stub_cast_vote(const char *regNo, const char *password, const char *choices);

/** Receives and prints results. Returns 0 on success, -1 on error. */
int stub_tally_votes(void);

/* Helper functions for networking (should be implemented in client_stub.c) */
int send_line(int sock, const char *msg);
int recv_line(int sock, char *buf, int size);

#endif /* CLIENT_STUB_H */
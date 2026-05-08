#ifndef SERVER_FUNCTIONS_H
#define SERVER_FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * PROTOTYPES FOR HANDLER FUNCTIONS
 * These correspond to the "procs" called by the server main loop.
 * --------------------------------------------------------------------- */

/** proc 1: Check if an admin record exists */
void handle_admin_exists(int cfd);

/** proc 2: Create the initial admin record */
void handle_create_admin(int cfd, char *args);

/** proc 3: Verify admin credentials */
void handle_verify_admin(int cfd, char *args);

/** proc 4: Add a new elective position */
void handle_add_position(int cfd, char *args);

/** proc 5: Register a new voter */
void handle_register_voter(int cfd, char *args);

/** proc 6: Register a contestant for a position */
void handle_register_contestant(int cfd, char *args);

/** proc 7: Send list of all positions */
void handle_get_positions(int cfd);

/** proc 8: Send list of all contestants and their current counts */
void handle_get_contestants(int cfd);

/** proc 9: Verify voter credentials and check if they have already voted */
void handle_verify_voter(int cfd, char *args);

/** proc 10: Process a voter's ballot and update files */
void handle_cast_vote(int cfd, char *args);

/** proc 11: Compute final tallies and determine winners */
void handle_tally_votes(int cfd);

#endif /* SERVER_FUNCTIONS_H */
#include "evoting_common.h"

/* -----------------------------------------------------------------------
 * File names used for persistent storage
 * --------------------------------------------------------------------- */
#define FILE_ADMIN       "admin.txt"
#define FILE_VOTERS      "voters.txt"
#define FILE_CONTESTANTS "contestants.txt"
#define FILE_POSITIONS   "positions.txt"

/* =======================================================================
 * HANDLER IMPLEMENTATIONS
 * Each handler reads its arguments from the pre-split args string,
 * performs the operation, and writes a response back to cfd.
 * ===================================================================== */

/* -----------------------------------------------------------------------
 * handle_admin_exists  (proc 1)
 * Response: "1\n" if admin.txt has a record, "0\n" otherwise.
 * --------------------------------------------------------------------- */
void handle_admin_exists(int cfd)
{
    FILE *f = fopen(FILE_ADMIN, "r");
    if (f == NULL) {
        send_line(cfd, "0\n");
        return;
    }

    char line[MAX_LINE];
    if (fgets(line, sizeof(line), f) != NULL) {
        send_line(cfd, "1\n");
    } else {
        send_line(cfd, "0\n");
    }

    fclose(f);
}

/* -----------------------------------------------------------------------
 * handle_create_admin  (proc 2)
 * args: "name|regNo|password"
 * Response: "1\n" on success, "0\n" on failure.
 * --------------------------------------------------------------------- */
void handle_create_admin(int cfd, char *args)
{
    if (args == NULL) { send_line(cfd, "0\n"); return; }

    char *saveptr = NULL;
    char *name     = strtok_r(args, "|", &saveptr);
    char *regNo    = strtok_r(NULL, "|", &saveptr);
    char *password = strtok_r(NULL, "|", &saveptr);

    if (!name || !regNo || !password) { send_line(cfd, "0\n"); return; }

    FILE *f = fopen(FILE_ADMIN, "w");
    if (f == NULL) { send_line(cfd, "0\n"); return; }

    fprintf(f, "%s|%s|%s\n", name, regNo, password);
    fclose(f);
    send_line(cfd, "1\n");
}

/* -----------------------------------------------------------------------
 * handle_verify_admin  (proc 3)
 * args: "regNo|password"
 * Response: "1\n" if match, "0\n" otherwise.
 * --------------------------------------------------------------------- */
void handle_verify_admin(int cfd, char *args)
{
    if (args == NULL) { send_line(cfd, "0\n"); return; }

    char *saveptr = NULL;
    char *inRegNo    = strtok_r(args, "|", &saveptr);
    char *inPassword = strtok_r(NULL, "|", &saveptr);

    if (!inRegNo || !inPassword) { send_line(cfd, "0\n"); return; }

    FILE *f = fopen(FILE_ADMIN, "r");
    if (f == NULL) { send_line(cfd, "0\n"); return; }

    char line[MAX_LINE];
    int found = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\n")] = 0;
        char *sp = NULL;
        strtok_r(line, "|", &sp);          /* name  – skip */
        char *regNo    = strtok_r(NULL, "|", &sp);
        char *password = strtok_r(NULL, "|", &sp);

        if (regNo && password &&
            strcmp(regNo, inRegNo) == 0 &&
            strcmp(password, inPassword) == 0) {
            found = 1;
            break;
        }
    }

    fclose(f);
    send_line(cfd, found ? "1\n" : "0\n");
}

/* -----------------------------------------------------------------------
 * handle_add_position  (proc 4)
 * args: "positionName"
 * Response: "1\n" on success, "0\n" on failure.
 * --------------------------------------------------------------------- */
void handle_add_position(int cfd, char *args)
{
    if (args == NULL || strlen(args) == 0) { send_line(cfd, "0\n"); return; }

    FILE *f = fopen(FILE_POSITIONS, "a");
    if (f == NULL) { send_line(cfd, "0\n"); return; }

    fprintf(f, "%s\n", args);
    fclose(f);
    send_line(cfd, "1\n");
}

/* -----------------------------------------------------------------------
 * handle_register_voter  (proc 5)
 * args: "name|regNo|password"
 * Response: "1\n" on success, "0\n" on failure.
 * --------------------------------------------------------------------- */
void handle_register_voter(int cfd, char *args)
{
    if (args == NULL) { send_line(cfd, "0\n"); return; }

    char *saveptr = NULL;
    char *name     = strtok_r(args, "|", &saveptr);
    char *regNo    = strtok_r(NULL, "|", &saveptr);
    char *password = strtok_r(NULL, "|", &saveptr);

    if (!name || !regNo || !password) { send_line(cfd, "0\n"); return; }

    FILE *f = fopen(FILE_VOTERS, "a");
    if (f == NULL) { send_line(cfd, "0\n"); return; }

    fprintf(f, "%s|%s|%s|0\n", name, regNo, password);
    fclose(f);
    send_line(cfd, "1\n");
}

/* -----------------------------------------------------------------------
 * handle_register_contestant  (proc 6)
 * args: "name|regNo|position"
 * Response: "1\n" on success, "0\n" on failure.
 * --------------------------------------------------------------------- */
void handle_register_contestant(int cfd, char *args)
{
    if (args == NULL) { send_line(cfd, "0\n"); return; }

    char *saveptr = NULL;
    char *name     = strtok_r(args, "|", &saveptr);
    char *regNo    = strtok_r(NULL, "|", &saveptr);
    char *position = strtok_r(NULL, "|", &saveptr);

    if (!name || !regNo || !position) { send_line(cfd, "0\n"); return; }

    FILE *f = fopen(FILE_CONTESTANTS, "a");
    if (f == NULL) { send_line(cfd, "0\n"); return; }

    fprintf(f, "%s|%s|%s|0\n", name, regNo, position);
    fclose(f);
    send_line(cfd, "1\n");
}

/* -----------------------------------------------------------------------
 * handle_get_positions  (proc 7)
 * Response: one "positionName\n" per position, then "END\n".
 * --------------------------------------------------------------------- */
void handle_get_positions(int cfd)
{
    FILE *f = fopen(FILE_POSITIONS, "r");
    if (f == NULL) {
        send_line(cfd, "END\n");
        return;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;
        char msg[MAX_LINE + 2];
        snprintf(msg, sizeof(msg), "%s\n", line);
        send_line(cfd, msg);
    }

    fclose(f);
    send_line(cfd, "END\n");
}

/* -----------------------------------------------------------------------
 * handle_get_contestants  (proc 8)
 * Response: one "name|regNo|position|votes\n" per contestant, then "END\n".
 * --------------------------------------------------------------------- */
void handle_get_contestants(int cfd)
{
    FILE *f = fopen(FILE_CONTESTANTS, "r");
    if (f == NULL) {
        send_line(cfd, "END\n");
        return;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;
        char msg[MAX_LINE + 2];
        snprintf(msg, sizeof(msg), "%s\n", line);
        send_line(cfd, msg);
    }

    fclose(f);
    send_line(cfd, "END\n");
}

/* -----------------------------------------------------------------------
 * handle_verify_voter  (proc 9)
 * args: "regNo|password"
 * Response: "0\n" bad creds, "1\n" ok/not voted, "2\n" ok/already voted.
 * --------------------------------------------------------------------- */
void handle_verify_voter(int cfd, char *args)
{
    if (args == NULL) { send_line(cfd, "0\n"); return; }

    char *saveptr = NULL;
    char *inRegNo    = strtok_r(args, "|", &saveptr);
    char *inPassword = strtok_r(NULL, "|", &saveptr);

    if (!inRegNo || !inPassword) { send_line(cfd, "0\n"); return; }

    FILE *f = fopen(FILE_VOTERS, "r");
    if (f == NULL) { send_line(cfd, "0\n"); return; }

    char line[MAX_LINE];
    int found = 0, voted = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\n")] = 0;
        char *sp = NULL;
        strtok_r(line, "|", &sp);               /* name  – skip */
        char *regNo    = strtok_r(NULL, "|", &sp);
        char *password = strtok_r(NULL, "|", &sp);
        char *votedStr = strtok_r(NULL, "|", &sp);

        if (regNo && password &&
            strcmp(regNo, inRegNo) == 0 &&
            strcmp(password, inPassword) == 0) {
            found = 1;
            voted = (votedStr && atoi(votedStr) == 1) ? 1 : 0;
            break;
        }
    }

    fclose(f);

    if (!found)  send_line(cfd, "0\n");
    else if (voted) send_line(cfd, "2\n");
    else         send_line(cfd, "1\n");
}

/* -----------------------------------------------------------------------
 * handle_cast_vote  (proc 10)
 * args: "regNo|password|choices"
 *       choices = pipe-delimited contestant regNos, one per position slot,
 *                 empty slot = skip.  e.g. "REG001||REG007"
 *
 * Steps:
 *   1. Load all contestants into memory.
 *   2. Split choices on '|' and increment matching vote counts.
 *   3. Rewrite contestants.txt with updated counts.
 *   4. Rewrite voters.txt marking this voter as having voted.
 *
 * Response: "1\n" on success, "0\n" on failure.
 * --------------------------------------------------------------------- */
void handle_cast_vote(int cfd, char *args)
{
    if (args == NULL) { send_line(cfd, "0\n"); return; }

    char *saveptr = NULL;
    char *inRegNo    = strtok_r(args, "|", &saveptr);
    char *inPassword = strtok_r(NULL, "|", &saveptr);
    /* Everything remaining is the choices string (may contain '|') */
    char *choices    = strtok_r(NULL, "", &saveptr);

    if (!inRegNo || !inPassword) { send_line(cfd, "0\n"); return; }
    if (choices == NULL) choices = "";

    /* --- Load contestants into memory -------------------------------- */
    char  c_names[MAX_RECORDS][MAX_NAME];
    char  c_regnos[MAX_RECORDS][MAX_REGNO];
    char  c_positions[MAX_RECORDS][MAX_POSITION];
    int   c_votes[MAX_RECORDS];
    int   c_count = 0;

    FILE *cf = fopen(FILE_CONTESTANTS, "r");
    if (cf == NULL) { send_line(cfd, "0\n"); return; }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), cf) != NULL && c_count < MAX_RECORDS) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;

        char *sp = NULL;
        char *name     = strtok_r(line, "|", &sp);
        char *regNo    = strtok_r(NULL, "|", &sp);
        char *position = strtok_r(NULL, "|", &sp);
        char *voteStr  = strtok_r(NULL, "|", &sp);

        if (!name || !regNo || !position) continue;

        strncpy(c_names[c_count],     name,     MAX_NAME     - 1); c_names[c_count][MAX_NAME-1]         = '\0';
        strncpy(c_regnos[c_count],    regNo,    MAX_REGNO    - 1); c_regnos[c_count][MAX_REGNO-1]       = '\0';
        strncpy(c_positions[c_count], position, MAX_POSITION - 1); c_positions[c_count][MAX_POSITION-1] = '\0';
        c_votes[c_count] = voteStr ? atoi(voteStr) : 0;
        c_count++;
    }
    fclose(cf);

    /* --- Apply votes from choices string ----------------------------- */
    /*
     * strtok_r silently skips empty tokens; we need to handle empty
     * slots ourselves.  Walk through choices character by character,
     * extracting each '|'-delimited token (including empty ones).
     */
    char choicesCopy[MAX_RECORDS * (MAX_REGNO + 1) + 1];
    strncpy(choicesCopy, choices, sizeof(choicesCopy) - 1);
    choicesCopy[sizeof(choicesCopy) - 1] = '\0';

    char *p = choicesCopy;
    while (p != NULL) {
        char *sep = strchr(p, '|');
        if (sep != NULL) *sep = '\0';   /* null-terminate this token */

        /* p now points to a single token (possibly empty) */
        if (strlen(p) > 0) {
            for (int j = 0; j < c_count; j++) {
                if (strcmp(c_regnos[j], p) == 0) {
                    c_votes[j]++;
                    break;
                }
            }
        }

        p = (sep != NULL) ? sep + 1 : NULL;
    }

    /* --- Rewrite contestants.txt ------------------------------------- */
    cf = fopen(FILE_CONTESTANTS, "w");
    if (cf == NULL) { send_line(cfd, "0\n"); return; }

    for (int j = 0; j < c_count; j++) {
        fprintf(cf, "%s|%s|%s|%d\n",
                c_names[j], c_regnos[j], c_positions[j], c_votes[j]);
    }
    fclose(cf);

    /* --- Rewrite voters.txt marking this voter ----------------------- */
    char  v_names[MAX_RECORDS][MAX_NAME];
    char  v_regnos[MAX_RECORDS][MAX_REGNO];
    char  v_passwords[MAX_RECORDS][MAX_PASSWORD];
    int   v_voted[MAX_RECORDS];
    int   v_count = 0;

    FILE *vf = fopen(FILE_VOTERS, "r");
    if (vf != NULL) {
        while (fgets(line, sizeof(line), vf) != NULL && v_count < MAX_RECORDS) {
            line[strcspn(line, "\n")] = 0;
            if (strlen(line) == 0) continue;

            char *sp = NULL;
            char *name     = strtok_r(line, "|", &sp);
            char *regNo    = strtok_r(NULL, "|", &sp);
            char *password = strtok_r(NULL, "|", &sp);
            char *votedStr = strtok_r(NULL, "|", &sp);

            if (!name || !regNo || !password) continue;

            strncpy(v_names[v_count],     name,     MAX_NAME     - 1); v_names[v_count][MAX_NAME-1]         = '\0';
            strncpy(v_regnos[v_count],    regNo,    MAX_REGNO    - 1); v_regnos[v_count][MAX_REGNO-1]       = '\0';
            strncpy(v_passwords[v_count], password, MAX_PASSWORD - 1); v_passwords[v_count][MAX_PASSWORD-1] = '\0';
            v_voted[v_count] = votedStr ? atoi(votedStr) : 0;

            /* Mark this voter */
            if (strcmp(v_regnos[v_count], inRegNo) == 0 &&
                strcmp(v_passwords[v_count], inPassword) == 0) {
                v_voted[v_count] = 1;
            }

            v_count++;
        }
        fclose(vf);

        vf = fopen(FILE_VOTERS, "w");
        if (vf != NULL) {
            for (int i = 0; i < v_count; i++) {
                fprintf(vf, "%s|%s|%s|%d\n",
                        v_names[i], v_regnos[i], v_passwords[i], v_voted[i]);
            }
            fclose(vf);
        }
    }

    send_line(cfd, "1\n");
}

/* -----------------------------------------------------------------------
 * handle_tally_votes  (proc 11)
 *
 * Reads contestants.txt, computes vote counts, finds the overall maximum,
 * and sends:
 *   "COUNTS\n"
 *   "name (regNo) for position - N votes\n"  (one per contestant)
 *   "WINNERS\n"
 *   "name (regNo) for position\n"             (those matching max votes)
 *   "END\n"
 *
 * If no contestants exist, sends "EMPTY\n" then "END\n".
 * --------------------------------------------------------------------- */
void handle_tally_votes(int cfd)
{
    char  c_names[MAX_RECORDS][MAX_NAME];
    char  c_regnos[MAX_RECORDS][MAX_REGNO];
    char  c_positions[MAX_RECORDS][MAX_POSITION];
    int   c_votes[MAX_RECORDS];
    int   c_count = 0;
    int   maxVotes = 0;

    FILE *f = fopen(FILE_CONTESTANTS, "r");
    if (f == NULL) {
        send_line(cfd, "EMPTY\n");
        send_line(cfd, "END\n");
        return;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f) != NULL && c_count < MAX_RECORDS) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;

        char *sp = NULL;
        char *name     = strtok_r(line, "|", &sp);
        char *regNo    = strtok_r(NULL, "|", &sp);
        char *position = strtok_r(NULL, "|", &sp);
        char *voteStr  = strtok_r(NULL, "|", &sp);

        if (!name || !regNo || !position) continue;

        strncpy(c_names[c_count],     name,     MAX_NAME     - 1); c_names[c_count][MAX_NAME-1]         = '\0';
        strncpy(c_regnos[c_count],    regNo,    MAX_REGNO    - 1); c_regnos[c_count][MAX_REGNO-1]       = '\0';
        strncpy(c_positions[c_count], position, MAX_POSITION - 1); c_positions[c_count][MAX_POSITION-1] = '\0';
        c_votes[c_count] = voteStr ? atoi(voteStr) : 0;

        if (c_votes[c_count] > maxVotes) maxVotes = c_votes[c_count];

        c_count++;
    }
    fclose(f);

    if (c_count == 0) {
        send_line(cfd, "EMPTY\n");
        send_line(cfd, "END\n");
        return;
    }

    /* Send vote counts */
    send_line(cfd, "COUNTS\n");
    for (int i = 0; i < c_count; i++) {
        char msg[MAX_LINE + 2];
        snprintf(msg, sizeof(msg), "%s (%s) for %s - %d votes\n",
                 c_names[i], c_regnos[i], c_positions[i], c_votes[i]);
        send_line(cfd, msg);
    }

    /* Send winners */
    send_line(cfd, "WINNERS\n");
    for (int i = 0; i < c_count; i++) {
        if (c_votes[i] == maxVotes) {
            char msg[MAX_LINE + 2];
            snprintf(msg, sizeof(msg), "%s (%s) for %s\n",
                     c_names[i], c_regnos[i], c_positions[i]);
            send_line(cfd, msg);
        }
    }

    send_line(cfd, "END\n");
}

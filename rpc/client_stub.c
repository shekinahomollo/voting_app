/* Each stub mirrors a remote procedure:
 *   1. Formats a request line and sends it with send_line().
 *   2. Reads the response line(s) with recv_line().
 *   3. Parses the plain text and returns the result.
 */

/* Returns 1 if an admin record exists on the server, 0 if not, -1 on error.
*/

#include "client_stub.h"

int stub_admin_exists(void)
{
    char buf[MAX_LINE];
    if (send_line(g_sock, "1\n") < 0) return -1;
    if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* Returns 1 on success, 0 on failure, -1 on error.
*/
int stub_create_admin(const admin_t *a)
{
    char msg[MAX_MSG];
    char buf[MAX_LINE];
    snprintf(msg, sizeof(msg), "2|%s|%s|%s\n", a->name, a->regNo, a->password);
    if (send_line(g_sock, msg) < 0) return -1;
    if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* Returns 1 if credentials match, 0 otherwise, -1 on error.
*/
int stub_verify_admin(const char *regNo, const char *password)
{
    char msg[MAX_MSG];
    char buf[MAX_LINE];
    snprintf(msg, sizeof(msg), "3|%s|%s\n", regNo, password);
    if (send_line(g_sock, msg) < 0) return -1;
    if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* Returns 1 on success, 0 on failure, -1 on error.
*/
int stub_add_position(const char *position)
{
    char msg[MAX_MSG];
    char buf[MAX_LINE];
    snprintf(msg, sizeof(msg), "4|%s\n", position);
    if (send_line(g_sock, msg) < 0) return -1;
    if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* Returns 1 on success, 0 on failure, -1 on error.
*/
int stub_register_voter(const voter_t *v)
{
    char msg[MAX_MSG];
    char buf[MAX_LINE];
    snprintf(msg, sizeof(msg), "5|%s|%s|%s\n", v->name, v->regNo, v->password);
    if (send_line(g_sock, msg) < 0) return -1;
    if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* Returns 1 on success, 0 on failure, -1 on error.
*/
int stub_register_contestant(const contestant_t *c)
{
    char msg[MAX_MSG];
    char buf[MAX_LINE];
    snprintf(msg, sizeof(msg), "6|%s|%s|%s\n", c->name, c->regNo, c->position);
    if (send_line(g_sock, msg) < 0) return -1;
    if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* Fills positions[][MAX_POSITION] with names returned by the server.
 * Returns the number of positions read, or -1 on error.
*/
int stub_get_positions(char positions[][MAX_POSITION], int maxPos)
{
    char buf[MAX_LINE];
    int count = 0;

    if (send_line(g_sock, "7\n") < 0) return -1;

    while (1) {
        if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
        if (strcmp(buf, "END") == 0) break;
        if (count < maxPos) {
            strncpy(positions[count], buf, MAX_POSITION - 1);
            positions[count][MAX_POSITION - 1] = '\0';
            count++;
        }
    }
    return count;
}

/* Fills contestants[] with records returned by the server.
 * Returns the number of contestants read, or -1 on error.
*/
int stub_get_contestants(contestant_t contestants[], int maxCon)
{
    char buf[MAX_LINE];
    int count = 0;

    if (send_line(g_sock, "8\n") < 0) return -1;

    while (1) {
        if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
        if (strcmp(buf, "END") == 0) break;

        if (count < maxCon) {
            contestant_t *c = &contestants[count];
            char *tok;

            tok = strtok(buf, "|");
            strncpy(c->name, tok ? tok : "", MAX_NAME - 1);
            c->name[MAX_NAME - 1] = '\0';

            tok = strtok(NULL, "|");
            strncpy(c->regNo, tok ? tok : "", MAX_REGNO - 1);
            c->regNo[MAX_REGNO - 1] = '\0';

            tok = strtok(NULL, "|");
            strncpy(c->position, tok ? tok : "", MAX_POSITION - 1);
            c->position[MAX_POSITION - 1] = '\0';

            tok = strtok(NULL, "|");
            c->votes = tok ? atoi(tok) : 0;

            count++;
        }
    }
    return count;
}

/* Returns 0 = bad creds, 1 = ok/not voted, 2 = ok/already voted, -1 = error.
*/
int stub_verify_voter(const char *regNo, const char *password)
{
    char msg[MAX_MSG];
    char buf[MAX_LINE];
    snprintf(msg, sizeof(msg), "9|%s|%s\n", regNo, password);
    if (send_line(g_sock, msg) < 0) return -1;
    if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* choices: pipe-delimited contestant regNos, one per position slot,
 *          empty slot = skip that position (e.g. "REG001||REG007").
 * Returns 1 on success, 0 on failure, -1 on error.
*/
int stub_cast_vote(const char *regNo, const char *password,
                          const char *choices)
{
    char msg[MAX_MSG];
    char buf[MAX_LINE];
    snprintf(msg, sizeof(msg), "10|%s|%s|%s\n", regNo, password, choices);
    if (send_line(g_sock, msg) < 0) return -1;
    if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/* Receives and prints results directly.
 * Server sends: "COUNTS\n" lines... "WINNERS\n" lines... "END\n"
 * Returns 0 on success, -1 on error.
*/
int stub_tally_votes(void)
{
    char buf[MAX_LINE];

    if (send_line(g_sock, "11\n") < 0) return -1;

    while (1) {
        if (recv_line(g_sock, buf, sizeof(buf)) < 0) return -1;

        if (strcmp(buf, "END") == 0) break;

        if (strcmp(buf, "COUNTS") == 0) {
            printf("\nVote Counts:\n");
        } else if (strcmp(buf, "WINNERS") == 0) {
            printf("\nWinner(s):\n");
        } else if (strcmp(buf, "EMPTY") == 0) {
            printf("No contestants found.\n");
            return 0;
        } else {
            printf("%s\n", buf);
        }
    }
    return 0;
}
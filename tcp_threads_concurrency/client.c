/* =========================================================
 * client.c  —  Voting system TCP client
 * ========================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT        9090
#define MAX_PAYLOAD 2048

/* ================= PROTOCOL (Matches server.c) ================= */

typedef enum { SVR_DISPLAY = 1, SVR_PROMPT = 2, CLI_INPUT = 3 } MsgType;

typedef enum {
    REQ_REGISTER_VOTER    = 10,
    REQ_VERIFY_ADMIN      = 20,
    REQ_CAST_VOTE         = 30,
    REQ_MANAGE_POSITIONS  = 40,
    REQ_REG_CONTESTANT    = 50,
    REQ_TALLY_VOTES       = 60,
    REQ_VIEW_ADMIN_INFO   = 70,
    REQ_ENSURE_ADMIN      = 80,
    REQ_ADMIN_BACK        = 90,
    REQ_GET_POSITIONS     = 100,
    REQ_CREATE_ADMIN      = 101,
    REQ_VIEW_CONTESTANTS  = 102,
    REQ_MARK_VOTED        = 103
} RequestType;

typedef enum {
    RESP_SUCCESS    = 1,
    RESP_ERROR      = 2,
    RESP_DATA       = 3,
    RESP_NEED_INPUT = 4
} ResponseType;

typedef struct {
    int  type;
    int  req_type;
    int  resp_status;
    char text[MAX_PAYLOAD];
    struct { char name[50];  char regNo[20]; char password[20]; } voter_data;
    struct { char name[50];  char regNo[20]; char password[20]; } admin_data;
    struct { char name[50];  char regNo[20]; char position[30]; } contestant_data;
    char positions[1000];
    int  position_choice;
} Msg;

/* ================= LOW-LEVEL I/O ================= */

/*
 * Read exactly sizeof(Msg) bytes, retrying on EINTR.
 * Returns 1 on success, 0 on server disconnect, -1 on error.
 */
static int recv_full(int sock, Msg *out)
{
    char  *buf  = (char *)out;
    size_t total = sizeof(Msg);
    size_t done  = 0;

    while (done < total) {
        ssize_t n = recv(sock, buf + done, total - done, 0);
        if (n > 0) {
            done += (size_t)n;
        } else if (n == 0) {
            return 0;          /* clean disconnect */
        } else {
            if (errno == EINTR) continue;   /* interrupted — retry */
            return -1;
        }
    }
    return 1;
}

/*
 * Write exactly sizeof(Msg) bytes, retrying on EINTR.
 * Returns 1 on success, -1 on error.
 */
static int send_full(int sock, const Msg *req)
{
    const char *buf   = (const char *)req;
    size_t      total = sizeof(Msg);
    size_t      done  = 0;

    while (done < total) {
        ssize_t n = send(sock, buf + done, total - done, MSG_NOSIGNAL);
        if (n > 0) {
            done += (size_t)n;
        } else {
            if (errno == EINTR) continue;
            return -1;
        }
    }
    return 1;
}

/*
 * Send a request and wait for the server's response.
 * On I/O failure the returned Msg has resp_status == RESP_ERROR.
 */
static Msg send_and_receive(int sock, const Msg *req)
{
    Msg resp;
    memset(&resp, 0, sizeof(resp));
    resp.resp_status = RESP_ERROR;

    if (send_full(sock, req) < 0) {
        fprintf(stderr, "[client] Send failed: %s\n", strerror(errno));
        return resp;
    }

    int rc = recv_full(sock, &resp);
    if (rc == 0) {
        fprintf(stderr, "[client] Server disconnected.\n");
    } else if (rc < 0) {
        fprintf(stderr, "[client] Receive error: %s\n", strerror(errno));
    }
    return resp;
}

/* ================= INPUT HELPERS ================= */

/*
 * Read a trimmed line from stdin into buf[size].
 * Returns 0 if EOF/error (caller should treat as abort).
 */
static int read_line(const char *prompt, char *buf, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
        return 0;
    }
    buf[strcspn(buf, "\n")] = '\0';
    return 1;
}

/* Build a zeroed Msg with req_type already set */
static Msg make_req(int req_type)
{
    Msg m;
    memset(&m, 0, sizeof(m));
    m.req_type = req_type;
    return m;
}

/* ================= POSITION FETCH ================= */

/*
 * Fetch positions from server into positions[100][30].
 * Returns the count (0 = none or error).
 */
static int get_positions(int sock, char positions[100][30])
{
    Msg req = make_req(REQ_GET_POSITIONS);
    Msg resp = send_and_receive(sock, &req);

    if (resp.resp_status != RESP_SUCCESS) return 0;

    /* resp.text is newline-separated position names */
    char buf[sizeof(resp.text)];
    strncpy(buf, resp.text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int   count = 0;
    char *tok   = strtok(buf, "\n");
    while (tok && count < 100) {
        /* Skip blank tokens that can appear from trailing newlines */
        if (strlen(tok) > 0) {
            strncpy(positions[count], tok, 29);
            positions[count][29] = '\0';
            count++;
        }
        tok = strtok(NULL, "\n");
    }
    return count;
}

/* ================= ADMIN BOOTSTRAP ================= */

static void ensure_admin_exists(int sock)
{
    Msg req  = make_req(REQ_ENSURE_ADMIN);
    Msg resp = send_and_receive(sock, &req);

    if (resp.resp_status != RESP_NEED_INPUT) {
        /* Admin already exists — nothing to do */
        return;
    }

    printf("\n%s\n", resp.text);

    Msg create = make_req(REQ_CREATE_ADMIN);

    if (!read_line("Admin Name     : ", create.admin_data.name,     sizeof(create.admin_data.name)))     return;
    if (!read_line("Admin Reg No   : ", create.admin_data.regNo,    sizeof(create.admin_data.regNo)))    return;
    if (!read_line("Admin Password : ", create.admin_data.password, sizeof(create.admin_data.password))) return;

    if (!strlen(create.admin_data.name) || !strlen(create.admin_data.regNo) ||
        !strlen(create.admin_data.password)) {
        printf("Error: All fields are required to create an admin.\n");
        return;
    }

    Msg result = send_and_receive(sock, &create);
    printf("\n%s\n", result.text);
}

/* ================= VOTER SECTION ================= */

static void register_voter(int sock)
{
    printf("\n=== Voter Registration ===\n");

    char name[50], regNo[20], password[20];

    if (!read_line("Voter name          : ", name,     sizeof(name)))     return;
    if (!read_line("Registration number : ", regNo,    sizeof(regNo)))    return;
    if (!read_line("Password            : ", password, sizeof(password))) return;

    if (!strlen(name) || !strlen(regNo) || !strlen(password)) {
        printf("Error: All fields are required.\n");
        return;
    }
    if (strlen(password) < 4) {
        printf("Error: Password must be at least 4 characters.\n");
        return;
    }

    Msg req = make_req(REQ_REGISTER_VOTER);
    strncpy(req.voter_data.name,     name,     sizeof(req.voter_data.name)     - 1);
    strncpy(req.voter_data.regNo,    regNo,    sizeof(req.voter_data.regNo)    - 1);
    strncpy(req.voter_data.password, password, sizeof(req.voter_data.password) - 1);

    Msg resp = send_and_receive(sock, &req);
    printf("\n%s\n", resp.text);
}

static void cast_vote(int sock)
{
    printf("\n=== Cast Your Vote ===\n");

    char voter_regNo[20], voter_password[20];
    if (!read_line("Your Registration Number : ", voter_regNo,    sizeof(voter_regNo)))    return;
    if (!read_line("Your Password            : ", voter_password, sizeof(voter_password))) return;

    if (!strlen(voter_regNo) || !strlen(voter_password)) {
        printf("Error: Credentials required.\n");
        return;
    }

    char avail_pos[100][30];
    int  pos_count = get_positions(sock, avail_pos);
    if (pos_count == 0) {
        printf("No positions available for voting.\n");
        return;
    }

    int aborted = 0;

    for (int i = 0; i < pos_count && !aborted; i++) {
        printf("\n--- Voting for Position: %s ---\n", avail_pos[i]);

        /* Show contestants for this position only */
        Msg view_req = make_req(REQ_VIEW_CONTESTANTS);
        strncpy(view_req.contestant_data.position, avail_pos[i], 29);
        Msg view_resp = send_and_receive(sock, &view_req);
        printf("%s\n", view_resp.text);

        if (strstr(view_resp.text, "No contestants") != NULL) {
            printf("(Skipping — no contestants for this position.)\n");
            continue;
        }

        char contestant_regNo[20];
        if (!read_line("Contestant reg no (Enter to skip): ",
                       contestant_regNo, sizeof(contestant_regNo)))
            break;

        if (strlen(contestant_regNo) == 0) {
            printf("Skipped.\n");
            continue;
        }

        Msg vote_req = make_req(REQ_CAST_VOTE);
        strncpy(vote_req.voter_data.regNo,       voter_regNo,       sizeof(vote_req.voter_data.regNo)       - 1);
        strncpy(vote_req.voter_data.password,    voter_password,    sizeof(vote_req.voter_data.password)    - 1);
        strncpy(vote_req.contestant_data.regNo,  contestant_regNo,  sizeof(vote_req.contestant_data.regNo)  - 1);

        Msg vote_resp = send_and_receive(sock, &vote_req);
        printf("Result: %s\n", vote_resp.text);

        /* If server says voting is already fully complete, stop immediately */
        if (vote_resp.resp_status == RESP_ERROR &&
            strstr(vote_resp.text, "already completed") != NULL) {
            aborted = 1;
        }
    }

    if (!aborted) {
        /* Tell server this voter is done — only if we weren't pre-emptively stopped */
        Msg final_req = make_req(REQ_MARK_VOTED);
        strncpy(final_req.voter_data.regNo, voter_regNo, sizeof(final_req.voter_data.regNo) - 1);
        Msg final_resp = send_and_receive(sock, &final_req);
        if (final_resp.resp_status == RESP_SUCCESS)
            printf("\nVoting complete. Thank you!\n");
        else
            printf("\nNote: %s\n", final_resp.text);
    }
}

/* ================= ADMIN PANEL ================= */

static void admin_panel(int sock)
{
    printf("\n=== Admin Panel ===\n");

    char regNo[20], password[20];
    if (!read_line("Admin Reg No   : ", regNo,    sizeof(regNo)))    return;
    if (!read_line("Admin Password : ", password, sizeof(password))) return;

    Msg auth_req = make_req(REQ_VERIFY_ADMIN);
    strncpy(auth_req.admin_data.regNo,    regNo,    sizeof(auth_req.admin_data.regNo)    - 1);
    strncpy(auth_req.admin_data.password, password, sizeof(auth_req.admin_data.password) - 1);

    Msg auth_resp = send_and_receive(sock, &auth_req);
    if (auth_resp.resp_status != RESP_SUCCESS) {
        printf("\nAuthentication failed: %s\n", auth_resp.text);
        return;
    }
    printf("Welcome, admin.\n");

    int running = 1;
    while (running) {
        printf("\n--- Admin Menu ---\n");
        printf("1. Manage Positions\n");
        printf("2. Register Contestant\n");
        printf("3. View All Contestants\n");
        printf("4. Tally Votes\n");
        printf("5. View My Admin Info\n");
        printf("6. Back to Main Menu\n");

        char choice[8];
        if (!read_line("Choice: ", choice, sizeof(choice))) break;

        if (strcmp(choice, "1") == 0) {
            /* ---- Manage Positions ---- */
            printf("\n--- Manage Positions ---\n");
            printf("Enter position names one per line (blank line to finish):\n");

            char positions_text[1000] = "";
            char pos[30];
            while (1) {
                if (!read_line("Position: ", pos, sizeof(pos))) break;
                if (strlen(pos) == 0) break;
                if (strlen(positions_text) + strlen(pos) + 2 < sizeof(positions_text)) {
                    if (strlen(positions_text) > 0) strcat(positions_text, "\n");
                    strcat(positions_text, pos);
                } else {
                    printf("(Maximum positions text reached.)\n");
                    break;
                }
            }

            if (strlen(positions_text) == 0) {
                printf("No positions entered.\n");
                continue;
            }

            Msg pos_req = make_req(REQ_MANAGE_POSITIONS);
            strncpy(pos_req.positions, positions_text, sizeof(pos_req.positions) - 1);
            Msg pos_resp = send_and_receive(sock, &pos_req);
            printf("\n%s\n", pos_resp.text);

        } else if (strcmp(choice, "2") == 0) {
            /* ---- Register Contestant ---- */
            printf("\n--- Contestant Registration ---\n");

            char avail_pos[100][30];
            int  pos_count = get_positions(sock, avail_pos);
            if (pos_count == 0) {
                printf("No positions available. Create positions first.\n");
                continue;
            }

            char name[50], regno[20];
            if (!read_line("Name   : ", name,   sizeof(name)))   continue;
            if (!read_line("Reg No : ", regno,  sizeof(regno)))  continue;

            if (!strlen(name) || !strlen(regno)) {
                printf("Error: Name and Reg No are required.\n");
                continue;
            }

            printf("\nAvailable Positions:\n");
            for (int i = 0; i < pos_count; i++)
                printf("  %d. %s\n", i + 1, avail_pos[i]);

            char pos_choice_str[8];
            if (!read_line("Select position number: ", pos_choice_str, sizeof(pos_choice_str)))
                continue;
            int pos_choice = atoi(pos_choice_str);
            if (pos_choice < 1 || pos_choice > pos_count) {
                printf("Invalid position choice.\n");
                continue;
            }

            Msg cont_req = make_req(REQ_REG_CONTESTANT);
            strncpy(cont_req.contestant_data.name,     name,                    sizeof(cont_req.contestant_data.name)     - 1);
            strncpy(cont_req.contestant_data.regNo,    regno,                   sizeof(cont_req.contestant_data.regNo)    - 1);
            strncpy(cont_req.contestant_data.position, avail_pos[pos_choice-1], sizeof(cont_req.contestant_data.position) - 1);

            Msg cont_resp = send_and_receive(sock, &cont_req);
            printf("\n%s\n", cont_resp.text);

        } else if (strcmp(choice, "3") == 0) {
            /* ---- View All Contestants (no position filter) ---- */
            Msg view_req = make_req(REQ_VIEW_CONTESTANTS);
            /* Leave contestant_data.position empty → server returns all */
            Msg view_resp = send_and_receive(sock, &view_req);
            printf("\n%s\n", view_resp.text);

        } else if (strcmp(choice, "4") == 0) {
            /* ---- Tally Votes ---- */
            Msg tally_req  = make_req(REQ_TALLY_VOTES);
            Msg tally_resp = send_and_receive(sock, &tally_req);
            printf("\n%s\n", tally_resp.text);

        } else if (strcmp(choice, "5") == 0) {
            /* ---- View Admin Info ---- */
            Msg info_req = make_req(REQ_VIEW_ADMIN_INFO);
            strncpy(info_req.admin_data.regNo,    regNo,    sizeof(info_req.admin_data.regNo)    - 1);
            strncpy(info_req.admin_data.password, password, sizeof(info_req.admin_data.password) - 1);
            Msg info_resp = send_and_receive(sock, &info_req);
            printf("\n%s\n", info_resp.text);

        } else if (strcmp(choice, "6") == 0) {
            running = 0;

        } else {
            printf("Invalid choice. Please enter 1–6.\n");
        }
    }
}

/* ================= MAIN ================= */

int main(void)
{
    char server_ip[64];
    if (!read_line("Enter server IP address: ", server_ip, sizeof(server_ip))) {
        fprintf(stderr, "Failed to read IP address.\n");
        return 1;
    }
    if (strlen(server_ip) == 0) {
        fprintf(stderr, "IP address cannot be empty.\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", server_ip);
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        fprintf(stderr, "Is the server running on %s:%d?\n", server_ip, PORT);
        close(sock);
        return 1;
    }

    printf("[client] Connected to %s:%d\n\n", server_ip, PORT);

    ensure_admin_exists(sock);

    int running = 1;
    while (running) {
        printf("\n=== Main Menu ===\n");
        printf("1. Admin Panel\n");
        printf("2. Register Voter\n");
        printf("3. Cast Vote\n");
        printf("4. Exit\n");

        char choice[8];
        if (!read_line("Choice: ", choice, sizeof(choice))) break;

        if      (strcmp(choice, "1") == 0) admin_panel(sock);
        else if (strcmp(choice, "2") == 0) register_voter(sock);
        else if (strcmp(choice, "3") == 0) cast_vote(sock);
        else if (strcmp(choice, "4") == 0) { printf("Goodbye!\n"); running = 0; }
        else                               printf("Invalid choice. Please enter 1–4.\n");
    }

    close(sock);
    return 0;
}
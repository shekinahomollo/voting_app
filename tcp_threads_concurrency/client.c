#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT        9090
#define MAX_PAYLOAD 2048

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
    RESP_SUCCESS = 1,
    RESP_ERROR   = 2,
    RESP_DATA    = 3,
    RESP_NEED_INPUT = 4
} ResponseType;

typedef struct {
    int  type;
    int  req_type;
    int  resp_status;
    char text[MAX_PAYLOAD];
    struct {
        char name[50];
        char regNo[20];
        char password[20];
    } voter_data;
    struct {
        char name[50];
        char regNo[20];
        char password[20];
    } admin_data;
    struct {
        char name[50];
        char regNo[20];
        char position[30];
    } contestant_data;
    char positions[1000];
    int position_choice;
} Msg;

void ensureAdminExists(int sock);
void adminPanel(int sock);
void castVote(int sock);
void viewContestants(int sock);


static Msg receive_response(int sock)
{
    Msg m;
    memset(&m, 0, sizeof(m));
    if (recv(sock, &m, sizeof(m), 0) <= 0) {
        printf("[client] Server disconnected.\n");
        m.resp_status = RESP_ERROR;
    }
    return m;
}

static Msg send_and_receive(int sock, const Msg *req)
{
    send(sock, req, sizeof(*req), 0);
    return receive_response(sock);
}

void ensureAdminExists(int sock)
{
    Msg req;
    memset(&req, 0, sizeof(req));
    req.type = 99;
    req.req_type = REQ_ENSURE_ADMIN;
    Msg resp = send_and_receive(sock, &req);

    if (resp.resp_status == RESP_NEED_INPUT) {
        printf("\n%s\n", resp.text);

        Msg create_req;
        memset(&create_req, 0, sizeof(create_req));
        create_req.type = 99;
        create_req.req_type = REQ_CREATE_ADMIN;

        printf("Admin Name: ");
        fflush(stdout);
        if (fgets(create_req.admin_data.name, sizeof(create_req.admin_data.name),
                  stdin) == NULL)
            create_req.admin_data.name[0] = '\0';
        create_req.admin_data.name[strcspn(create_req.admin_data.name, "\n")] = '\0';

        printf("Admin Reg No: ");
        fflush(stdout);
        if (fgets(create_req.admin_data.regNo, sizeof(create_req.admin_data.regNo),
                  stdin) == NULL)
            create_req.admin_data.regNo[0] = '\0';
        create_req.admin_data.regNo[strcspn(create_req.admin_data.regNo, "\n")] = '\0';

        printf("Admin Password: ");
        fflush(stdout);
        if (fgets(create_req.admin_data.password,
                  sizeof(create_req.admin_data.password), stdin) == NULL)
            create_req.admin_data.password[0] = '\0';
        create_req.admin_data.password[strcspn(create_req.admin_data.password, "\n")] =
            '\0';

        Msg create_resp = send_and_receive(sock, &create_req);
        printf("%s\n", create_resp.text);
    }
}

static int get_positions(int sock, char positions[100][30])
{
    Msg req;
    memset(&req, 0, sizeof(req));
    req.type = 99;
    req.req_type = REQ_GET_POSITIONS;
    Msg resp = send_and_receive(sock, &req);

    if (resp.resp_status != RESP_SUCCESS) {
        return 0;
    }

    int count = 0;
    char *pos_copy = strdup(resp.text);
    char *pos = strtok(pos_copy, "\n");
    while (pos && count < 100) {
        strncpy(positions[count], pos, 29);
        positions[count][29] = '\0';
        count++;
        pos = strtok(NULL, "\n");
    }
    free(pos_copy);
    return count;
}

void viewContestants(int sock)
{
    Msg req;
    memset(&req, 0, sizeof(req));
    req.type = 99;
    req.req_type = REQ_VIEW_CONTESTANTS;
    
    send(sock, &req, sizeof(req), 0);
    Msg resp = receive_response(sock);
    printf("\n%s\n", resp.text);
}

void castVote(int sock)
{
    char voter_regNo[20], voter_password[20];
    char avail_pos[100][30];
    int pos_count;

    printf("\n=== Cast Your Vote ===\n");
    
    printf("Your Registration Number: ");
    fflush(stdout);
    if (fgets(voter_regNo, sizeof(voter_regNo), stdin) == NULL) return;
    voter_regNo[strcspn(voter_regNo, "\n")] = '\0';

    printf("Your Password: ");
    fflush(stdout);
    if (fgets(voter_password, sizeof(voter_password), stdin) == NULL) return;
    voter_password[strcspn(voter_password, "\n")] = '\0';

    pos_count = get_positions(sock, avail_pos);
    if (pos_count == 0) {
        printf("No positions available for voting.\n");
        return;
    }

    for (int i = 0; i < pos_count; i++) {
        printf("\n--- Voting for Position: %s ---\n", avail_pos[i]);
        
        Msg view_req;
        memset(&view_req, 0, sizeof(view_req));
        view_req.req_type = REQ_VIEW_CONTESTANTS;
        strncpy(view_req.contestant_data.position, avail_pos[i], 29);
        
        Msg view_resp = send_and_receive(sock, &view_req);
        printf("%s\n", view_resp.text);

        if (strstr(view_resp.text, "No contestants") != NULL) {
            continue;
        }

        printf("Enter Registration Number of candidate (or press Enter to skip): ");
        fflush(stdout);
        char contestant_regNo[20];
        if (fgets(contestant_regNo, sizeof(contestant_regNo), stdin) == NULL) break;
        contestant_regNo[strcspn(contestant_regNo, "\n")] = '\0';

        if (strlen(contestant_regNo) == 0) continue;

        Msg vote_req;
        memset(&vote_req, 0, sizeof(vote_req));
        vote_req.req_type = REQ_CAST_VOTE;
        strncpy(vote_req.voter_data.regNo, voter_regNo, 19);
        strncpy(vote_req.voter_data.password, voter_password, 19);
        strncpy(vote_req.contestant_data.regNo, contestant_regNo, 19);

        Msg vote_resp = send_and_receive(sock, &vote_req);
        printf("Result: %s\n", vote_resp.text);

        if (vote_resp.resp_status == RESP_ERROR && strstr(vote_resp.text, "already completed") != NULL) {
            return;
        }
    }

    /* Finalize voting process for this voter */
    Msg final_req;
    memset(&final_req, 0, sizeof(final_req));
    final_req.req_type = REQ_MARK_VOTED;
    strncpy(final_req.voter_data.regNo, voter_regNo, 19);
    send(sock, &final_req, sizeof(final_req), 0);
    receive_response(sock);
    
    printf("\nVoting process complete. Thank you!\n");
}

void adminPanel(int sock)
{
    printf("\n=== Admin Panel ===\n");
    printf("Admin Reg No: ");
    fflush(stdout);
    char regNo[20];
    if (fgets(regNo, sizeof(regNo), stdin) == NULL)
        regNo[0] = '\0';
    regNo[strcspn(regNo, "\n")] = '\0';

    printf("Admin Password: ");
    fflush(stdout);
    char password[20];
    if (fgets(password, sizeof(password), stdin) == NULL)
        password[0] = '\0';
    password[strcspn(password, "\n")] = '\0';

    Msg auth_req;
    memset(&auth_req, 0, sizeof(auth_req));
    auth_req.type = 99;
    auth_req.req_type = REQ_VERIFY_ADMIN;
    strncpy(auth_req.admin_data.regNo, regNo, sizeof(auth_req.admin_data.regNo) - 1);
    strncpy(auth_req.admin_data.password, password,
            sizeof(auth_req.admin_data.password) - 1);

    Msg auth_resp = send_and_receive(sock, &auth_req);

    if (auth_resp.resp_status != RESP_SUCCESS) {
        printf("\nAdmin authentication failed.\n");
        return;
    }

    printf("Admin authenticated.\n");

    int admin_running = 1;
    while (admin_running) {
        printf("\nAdmin Options:\n");
        printf("1. Manage Positions\n");
        printf("2. Register Contestant\n");
        printf("3. View Contestants\n");
        printf("4. Tally Votes\n");
        printf("5. View Admin Info\n");
        printf("6. Back to Main Menu\n");
        printf("Enter choice: ");
        fflush(stdout);

        char choice[20];
        if (fgets(choice, sizeof(choice), stdin) == NULL)
            break;
        choice[strcspn(choice, "\n")] = '\0';

        if (strcmp(choice, "1") == 0) {
            /* Manage Positions */
            printf("\n--- Manage Positions ---\n");
            printf("Enter position names (empty line to finish):\n");

            char positions_text[1000] = "";
            char pos[30];
            while (1) {
                printf("Position: ");
                fflush(stdout);
                if (fgets(pos, sizeof(pos), stdin) == NULL)
                    break;
                pos[strcspn(pos, "\n")] = '\0';
                if (strlen(pos) == 0)
                    break;
                if (strlen(positions_text) > 0)
                    strcat(positions_text, "\n");
                strcat(positions_text, pos);
            }

            Msg pos_req;
            memset(&pos_req, 0, sizeof(pos_req));
            pos_req.type = 99;
            pos_req.req_type = REQ_MANAGE_POSITIONS;
            strncpy(pos_req.positions, positions_text, sizeof(pos_req.positions) - 1);
            Msg pos_resp = send_and_receive(sock, &pos_req);
            printf("\n%s\n", pos_resp.text);

        } else if (strcmp(choice, "2") == 0) {
            /* Register Contestant */
            printf("\n--- Contestant Registration ---\n");

            char avail_pos[100][30];
            int pos_count = get_positions(sock, avail_pos);

            if (pos_count == 0) {
                printf("No positions available. Please create positions first.\n");
                continue;
            }

            printf("Name: ");
            fflush(stdout);
            char name[50];
            if (fgets(name, sizeof(name), stdin) == NULL)
                name[0] = '\0';
            name[strcspn(name, "\n")] = '\0';

            printf("Reg No: ");
            fflush(stdout);
            char regno[20];
            if (fgets(regno, sizeof(regno), stdin) == NULL)
                regno[0] = '\0';
            regno[strcspn(regno, "\n")] = '\0';

            printf("\nAvailable Positions:\n");
            for (int i = 0; i < pos_count; i++)
                printf("%d. %s\n", i + 1, avail_pos[i]);

            printf("Select position number: ");
            fflush(stdout);
            char pos_choice_str[20];
            if (fgets(pos_choice_str, sizeof(pos_choice_str), stdin) == NULL)
                pos_choice_str[0] = '\0';
            pos_choice_str[strcspn(pos_choice_str, "\n")] = '\0';
            int pos_choice = atoi(pos_choice_str);

            if (pos_choice < 1 || pos_choice > pos_count) {
                printf("Invalid position choice.\n");
                continue;
            }

            Msg cont_req;
            memset(&cont_req, 0, sizeof(cont_req));
            cont_req.type = 99;
            cont_req.req_type = REQ_REG_CONTESTANT;
            strncpy(cont_req.contestant_data.name, name,
                    sizeof(cont_req.contestant_data.name) - 1);
            strncpy(cont_req.contestant_data.regNo, regno,
                    sizeof(cont_req.contestant_data.regNo) - 1);
            strncpy(cont_req.contestant_data.position, avail_pos[pos_choice - 1],
                    sizeof(cont_req.contestant_data.position) - 1);
            Msg cont_resp = send_and_receive(sock, &cont_req);
            printf("\n%s\n", cont_resp.text);

        } else if (strcmp(choice, "3") == 0) {
            /* View Contestants */
            viewContestants(sock);

        } else if (strcmp(choice, "4") == 0) {
            /* Tally Votes */
            Msg tally_req;
            memset(&tally_req, 0, sizeof(tally_req));
            tally_req.type = 99;
            tally_req.req_type = REQ_TALLY_VOTES;
            Msg tally_resp = send_and_receive(sock, &tally_req);
            printf("\n%s\n", tally_resp.text);

        } else if (strcmp(choice, "5") == 0) {
            /* View Admin Info */
            Msg info_req;
            memset(&info_req, 0, sizeof(info_req));
            info_req.type = 99;
            info_req.req_type = REQ_VIEW_ADMIN_INFO;
            strncpy(info_req.admin_data.regNo, regNo,
                    sizeof(info_req.admin_data.regNo) - 1);
            strncpy(info_req.admin_data.password, password,
                    sizeof(info_req.admin_data.password) - 1);
            Msg info_resp = send_and_receive(sock, &info_req);
            printf("\n%s\n", info_resp.text);

        } else if (strcmp(choice, "6") == 0) {
            admin_running = 0;

        } else {
            printf("Invalid choice.\n");
        }
    }
}

int main(void)
{
    char server_ip[64];
    printf("Enter server IP address: ");
    fflush(stdout);
    if (fgets(server_ip, sizeof(server_ip), stdin) == NULL) {
        fprintf(stderr, "Failed to read IP address.\n");
        exit(1);
    }
    server_ip[strcspn(server_ip, "\r\n")] = '\0';

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", server_ip);
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        fprintf(stderr, "Make sure the TCP server is running on %s:%d\n", server_ip,
                PORT);
        exit(1);
    }

    printf("[client] Connected to %s:%d\n\n", server_ip, PORT);

    ensureAdminExists(sock);

    int running = 1;
    while (running) {
        printf("\n=== Main Menu ===\n");
        printf("1. Admin Panel\n");
        printf("2. Register Voter\n");
        printf("3. Cast Vote\n");
        printf("4. Exit\n");
        printf("Enter choice: ");
        fflush(stdout);

        char choice[20];
        if (fgets(choice, sizeof(choice), stdin) == NULL)
            break;
        choice[strcspn(choice, "\n")] = '\0';

        if (strcmp(choice, "1") == 0) {
            adminPanel(sock);

        } else if (strcmp(choice, "2") == 0) {
            printf("\n=== Voter Registration ===\n");
            char name[50], regNo[20], password[20];

            printf("Voter name: ");
            fflush(stdout);
            if (fgets(name, sizeof(name), stdin) == NULL)
                continue;
            name[strcspn(name, "\n")] = '\0';

            printf("Registration number: ");
            fflush(stdout);
            if (fgets(regNo, sizeof(regNo), stdin) == NULL)
                continue;
            regNo[strcspn(regNo, "\n")] = '\0';

            printf("Password: ");
            fflush(stdout);
            if (fgets(password, sizeof(password), stdin) == NULL)
                continue;
            password[strcspn(password, "\n")] = '\0';

            if (!strlen(name) || !strlen(regNo) || !strlen(password)) {
                printf("Error: Empty fields.\n");
                continue;
            }
            if (strlen(password) < 4) {
                printf("Error: Password must be at least 4 characters.\n");
                continue;
            }

            Msg req;
            memset(&req, 0, sizeof(req));
            req.type = 99;
            req.req_type = REQ_REGISTER_VOTER;
            strncpy(req.voter_data.name, name, sizeof(req.voter_data.name) - 1);
            strncpy(req.voter_data.regNo, regNo, sizeof(req.voter_data.regNo) - 1);
            strncpy(req.voter_data.password, password,
                    sizeof(req.voter_data.password) - 1);

            Msg resp = send_and_receive(sock, &req);
            printf("\n%s\n", resp.text);

        } else if (strcmp(choice, "3") == 0) {
            castVote(sock);

        } else if (strcmp(choice, "4") == 0) {
            printf("Goodbye!\n");
            running = 0;

        } else {
            printf("Invalid choice.\n");
        }
    }

    close(sock);
    return 0;
}

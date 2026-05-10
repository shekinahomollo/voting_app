#include "evoting_common.h"
#include "client_stub.h"

/* -----------------------------------------------------------------------
 * Connection handle – single persistent TCP connection, opened in main().
 * --------------------------------------------------------------------- */
int g_sock = -1;

/* =======================================================================
 * APPLICATION LOGIC – prompts and menus
 * ===================================================================== */

static void menu(void);
static void ensureAdminExists(void);
static void adminPanel(void);
static void managePositions(void);
static void registerVoter(void);
static void registerContestant(void);
static void castVote(void);
static void tallyVotes(void);

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    struct sockaddr_in serv_addr;
    struct hostent *server;
    int choice;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <server_host>\n", argv[0]);
        exit(1);
    }

    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr, "Error: no such host '%s'\n", argv[1]);
        exit(1);
    }

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) {
        perror("socket");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(EVOTING_PORT);

    if (connect(g_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    printf("Connected to evoting server at %s:%d\n", argv[1], EVOTING_PORT);

    ensureAdminExists();

    while (1) {
        menu();
        printf("Enter choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1: adminPanel();    break;
            case 2: registerVoter(); break;
            case 3: castVote();      break;
            case 4:
                close(g_sock);
                exit(0);
            default:
                printf("Invalid choice\n");
        }
    }
}

static void menu(void)
{
    printf("\nElectronic Voting System\n");
    printf("1. Admin Panel\n");
    printf("2. Register Voter\n");
    printf("3. Cast Vote\n");
    printf("4. Exit\n");
}

static void ensureAdminExists(void)
{
    int exists = stub_admin_exists();
    if (exists < 0) {
        fprintf(stderr, "Error communicating with server.\n");
        close(g_sock);
        exit(1);
    }

    if (exists == 0) {
        admin_t a;
        int res;

        printf("\nNo admin found. Please create an admin before using the system.\n");
        printf("\n--- Create Admin ---\n");

        printf("Enter Admin Name: ");
        fgets(a.name, sizeof(a.name), stdin);
        a.name[strcspn(a.name, "\n")] = 0;

        printf("Enter Admin Registration Number: ");
        fgets(a.regNo, sizeof(a.regNo), stdin);
        a.regNo[strcspn(a.regNo, "\n")] = 0;

        printf("Enter Admin Password: ");
        fgets(a.password, sizeof(a.password), stdin);
        a.password[strcspn(a.password, "\n")] = 0;

        res = stub_create_admin(&a);
        if (res <= 0) {
            fprintf(stderr, "Error creating admin.\n");
            close(g_sock);
            exit(1);
        }

        printf("Admin created successfully.\n");
    }
}

static void adminPanel(void)
{
    char regnobuf[MAX_REGNO];
    char passbuf[MAX_PASSWORD];
    int  auth, choice;

    printf("\n--- Admin Panel ---\n");
    printf("\n--- Admin Authentication ---\n");

    printf("Enter Admin Registration Number: ");
    getchar();
    fgets(regnobuf, sizeof(regnobuf), stdin);
    regnobuf[strcspn(regnobuf, "\n")] = 0;

    printf("Enter Admin Password: ");
    fgets(passbuf, sizeof(passbuf), stdin);
    passbuf[strcspn(passbuf, "\n")] = 0;

    auth = stub_verify_admin(regnobuf, passbuf);
    if (auth < 0) {
        fprintf(stderr, "Error communicating with server.\n");
        return;
    }
    if (auth == 0) {
        printf("Invalid admin credentials.\n");
        printf("Admin authentication failed. Returning to main menu.\n");
        return;
    }

    while (1) {
        printf("\nAdmin Options:\n");
        printf("1. Manage Positions\n");
        printf("2. Register Contestant\n");
        printf("3. Tally Votes\n");
        printf("4. Back to Main Menu\n");
        printf("Enter choice: ");
        scanf("%d", &choice);

        switch (choice) {
            case 1: managePositions();    break;
            case 2: registerContestant(); break;
            case 3: tallyVotes();         break;
            case 4: return;
            default: printf("Invalid choice.\n");
        }
    }
}

static void managePositions(void)
{
    char position[MAX_POSITION];
    int  ch;

    printf("\n--- Manage Positions ---\n");
    printf("Enter position names to add (empty line to finish):\n");

    while ((ch = getchar()) != '\n' && ch != EOF) {}

    while (1) {
        printf("Position: ");
        if (fgets(position, sizeof(position), stdin) == NULL) break;
        if (strcmp(position, "\n") == 0) break;
        position[strcspn(position, "\n")] = 0;
        if (strlen(position) == 0) break;

        if (stub_add_position(position) <= 0) {
            printf("Error adding position '%s'.\n", position);
        }
    }

    printf("Positions updated.\n");
}

static void registerVoter(void)
{
    voter_t v;
    int res;

    printf("\n--- Voter Registration ---\n");

    printf("Enter Name: ");
    getchar();
    fgets(v.name, sizeof(v.name), stdin);
    v.name[strcspn(v.name, "\n")] = 0;

    printf("Enter Registration Number: ");
    fgets(v.regNo, sizeof(v.regNo), stdin);
    v.regNo[strcspn(v.regNo, "\n")] = 0;

    printf("Enter Password: ");
    fgets(v.password, sizeof(v.password), stdin);
    v.password[strcspn(v.password, "\n")] = 0;

    v.voted = 0;

    res = stub_register_voter(&v);
    if (res <= 0) {
        printf("Error registering voter.\n");
        return;
    }

    printf("Voter registered successfully!\n");
    printf("Press Enter to return to menu...");
    getchar();
}

static void registerContestant(void)
{
    char positions[MAX_RECORDS][MAX_POSITION];
    int  posCount;
    contestant_t c;
    int  posChoice, res, i;

    posCount = stub_get_positions(positions, MAX_RECORDS);
    if (posCount < 0) {
        fprintf(stderr, "Error communicating with server.\n");
        return;
    }
    if (posCount == 0) {
        printf("No positions found. Please create positions first.\n");
        return;
    }

    printf("\n--- Contestant Registration ---\n");

    printf("Enter Name: ");
    getchar();
    fgets(c.name, sizeof(c.name), stdin);
    c.name[strcspn(c.name, "\n")] = 0;

    printf("Enter Registration Number: ");
    fgets(c.regNo, sizeof(c.regNo), stdin);
    c.regNo[strcspn(c.regNo, "\n")] = 0;

    printf("\nAvailable Positions:\n");
    for (i = 0; i < posCount; i++) {
        printf("%d. %s\n", i + 1, positions[i]);
    }

    printf("Select position number to contest for: ");
    scanf("%d", &posChoice);

    if (posChoice < 1 || posChoice > posCount) {
        printf("Invalid position choice.\n");
        return;
    }

    strncpy(c.position, positions[posChoice - 1], MAX_POSITION - 1);
    c.position[MAX_POSITION - 1] = '\0';
    c.votes = 0;

    res = stub_register_contestant(&c);
    if (res <= 0) {
        printf("Error registering contestant.\n");
        return;
    }

    getchar();
    printf("Contestant registered successfully!\n");
    printf("Press Enter to return to menu...");
    getchar();
}

/* 
 * 1. Verify voter credentials (server returns 0/1/2).
 * 2. Fetch positions and contestants from server.
 * 3. For each position display candidates and collect choice locally.
 * 4. Build pipe-delimited choices string; empty slot = skip.
 * 5. Submit ballot to server.
*/
static void castVote(void)
{
    char regnobuf[MAX_REGNO];
    char passbuf[MAX_PASSWORD];
    int  vstatus;
    char positions[MAX_RECORDS][MAX_POSITION];
    int  posCount;
    contestant_t contestants[MAX_RECORDS];
    int  conCount;
    char choicesBuf[MAX_RECORDS * (MAX_REGNO + 1) + 1];
    int  p, j, k;

    printf("\n--- Cast Vote ---\n");

    printf("Enter your Registration Number: ");
    getchar();
    fgets(regnobuf, sizeof(regnobuf), stdin);
    regnobuf[strcspn(regnobuf, "\n")] = 0;

    printf("Enter your Password: ");
    fgets(passbuf, sizeof(passbuf), stdin);
    passbuf[strcspn(passbuf, "\n")] = 0;

    vstatus = stub_verify_voter(regnobuf, passbuf);
    if (vstatus < 0) {
        fprintf(stderr, "Error communicating with server.\n");
        return;
    }
    if (vstatus == 0) {
        printf("Voter not registered or wrong credentials.\n");
        return;
    }
    if (vstatus == 2) {
        printf("You have already voted.\n");
        return;
    }

    posCount = stub_get_positions(positions, MAX_RECORDS);
    if (posCount <= 0) {
        printf("No positions found.\n");
        return;
    }

    conCount = stub_get_contestants(contestants, MAX_RECORDS);
    if (conCount <= 0) {
        printf("No contestants found.\n");
        return;
    }

    choicesBuf[0] = '\0';

    for (p = 0; p < posCount; p++) {
        int candidateIdx[MAX_RECORDS];
        int candidateCount = 0;

        for (j = 0; j < conCount; j++) {
            if (strcmp(contestants[j].position, positions[p]) == 0) {
                candidateIdx[candidateCount++] = j;
            }
        }

        /* Pipe separator between positions (not before the first) */
        if (p > 0) {
            strncat(choicesBuf, "|", sizeof(choicesBuf) - strlen(choicesBuf) - 1);
        }

        printf("\nPosition: %s\n", positions[p]);

        if (candidateCount == 0) {
            printf("No contestants available for this position.\n");
            printf("Press Enter to continue to the next position...");
            getchar();
        } else {
            for (k = 0; k < candidateCount; k++) {
                int idx = candidateIdx[k];
                printf("%d. %s (%s)\n", k + 1,
                       contestants[idx].name,
                       contestants[idx].regNo);
            }

            int choice;
            printf("Enter candidate number to vote for (or 0 to skip): ");
            scanf("%d", &choice);

            if (choice >= 1 && choice <= candidateCount) {
                int chosen = candidateIdx[choice - 1];
                strncat(choicesBuf,
                        contestants[chosen].regNo,
                        sizeof(choicesBuf) - strlen(choicesBuf) - 1);
            } else if (choice != 0) {
                printf("Invalid choice for this position. Skipping.\n");
            }
        }
    }

    if (stub_cast_vote(regnobuf, passbuf, choicesBuf) <= 0) {
        printf("Error recording vote.\n");
        return;
    }

    printf("Vote cast successfully!\n");
}

static void tallyVotes(void)
{
    printf("\n--- Election Results ---\n");

    if (stub_tally_votes() < 0) {
        fprintf(stderr, "Error communicating with server.\n");
        return;
    }

    printf("\nPress Enter to return to menu...");
    getchar();
    getchar();
}

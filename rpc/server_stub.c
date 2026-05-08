#include "evoting_common.h"
#include "server_stub.h"
#include "server_functions.h"

void server_dispatch(int cfd, char *line) {
    char *saveptr = NULL;
    
    /* First token is the procedure code (e.g., "1", "2") */
    char *proc_str = strtok_r(line, "|", &saveptr);
    if (proc_str == NULL) return;

    int proc = atoi(proc_str);

    /* args points to the remainder of the line after the first '|' */
    char *args = strtok_r(NULL, "", &saveptr);

    switch (proc) {
        case PROC_ADMIN_EXISTS:
            handle_admin_exists(cfd);
            break;
        case PROC_CREATE_ADMIN:
            handle_create_admin(cfd, args);
            break;
        case PROC_VERIFY_ADMIN:
            handle_verify_admin(cfd, args);
            break;
        case PROC_ADD_POSITION:
            handle_add_position(cfd, args);
            break;
        case PROC_REGISTER_VOTER:
            handle_register_voter(cfd, args);
            break;
        case PROC_REGISTER_CONTESTANT:
            handle_register_contestant(cfd, args);
            break;
        case PROC_GET_POSITIONS:
            handle_get_positions(cfd);
            break;
        case PROC_GET_CONTESTANTS:
            handle_get_contestants(cfd);
            break;
        case PROC_VERIFY_VOTER:
            handle_verify_voter(cfd, args);
            break;
        case PROC_CAST_VOTE:
            handle_cast_vote(cfd, args);
            break;
        case PROC_TALLY_VOTES:
            handle_tally_votes(cfd);
            break;
        default:
            send_line(cfd, "0\n");
            break;
    }
}
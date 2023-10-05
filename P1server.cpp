#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sqlite3.h>

#define PORT 2982
#define MAX_LINE 256

int main() {
    
    // Server needed variables
    int server_fd, new_socket, valread;
    struct sockaddr_in sin;
    char buf[MAX_LINE];
    int sin_len = sizeof(sin);

    // SQLite needed variables
    sqlite3 *db;
    char* zErrMsg = 0;
    char *sql;
    
    // Opening database
    int rc = sqlite3_open("PokemonDB.db", &db);
    if (rc) {
        fprintf(stderr, "Failed to open: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0) < 0) {

    }

}
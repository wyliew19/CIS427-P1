#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string>

#define PORT 2982
#define MAX_LINE 256
const std::string COMMANDS = "BUY SELL LIST BALANCE SHUTDOWN QUIT";

int main(int argc, char* argv[]) {
    // Server connection variables
    struct hostent *hp;
    int status, valread;
    struct sockaddr_in sin;
    char buf[MAX_LINE];
    char* host;
    int s, len;

    if (argc == 2) {
        host = argv[1];
    } else {
        perror("Need host server\n");
        exit(1);
    }

    hp = gethostbyname(host);
    if (!hp) {
        fprintf(stderr, "Unknown host: %s\n", host);
        exit(1);
    }

    memset(&sin, 0, sizeof(sin));

    sin.sin_family = AF_INET;
    memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
    sin.sin_port = htons(PORT);

    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Failed to create a socket\n");
        exit(1);
    }
    
    if(connect(s, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("Failed connection\n");
        close(s);
        exit(1);
    }

    printf("Connected to server: type \"HELP\" for commands and format\n\n");

    while (fgets(buf, sizeof(buf), stdin)) {
        buf[MAX_LINE - 1] = '\0';
        len = strlen(buf) + 1;

        char* tmp = (char*)malloc(sizeof(buf)); // temporary variable for use of strtok
        strcpy(tmp, buf);
        char* cpy = strtok(tmp, " ");
        free(tmp);
        // Check to be sure valid command was sent
        if (COMMANDS.find(cpy) == std::string::npos) {
            fprintf(stderr, "400 invalid command\nCommand \"%s\" not recognized\n", buf);
            continue;
        }
        // If client quits
        if (strcmp(cpy, "QUIT") == 0) {
            close(s);
            printf("200 OK");
            return 0;
        }
        // Send command
        send(s, buf, len, 0);
        // Receive server feedback
        valread = recv(s, buf, sizeof(buf), 0);
        if (valread <= 0) {
            fprintf(stderr, "Server unexpectedly closed");
            exit(1);
        }
        printf(buf);
        if (strcmp(cpy, "SHUTDOWN")) {
            close(s);
            return 0;
        }
    }
}
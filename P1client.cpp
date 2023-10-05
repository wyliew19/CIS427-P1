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
    struct hostent *hp;      // Host entry data structure
    int status, valread;    // Status and data read values
    struct sockaddr_in sin; // Socket address structure
    char buf[MAX_LINE];     // Buffer for sending and receiving data
    char* host;             // Hostname or IP address
    int s, len;

    // Check for command-line argument specifying the server hostname or IP address
    if (argc == 2) {
        host = argv[1];
    } else {
        perror("Need host server\n");
        exit(1);
    }

    // Resolve the server's hostname to an IP address using gethostbyname
    hp = gethostbyname(host);
    if (!hp) {
        fprintf(stderr, "Unknown host: %s\n", host);
        exit(1);
    }

    // Initialize the sockaddr_in structure
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
    sin.sin_port = htons(PORT);

    // Create a socket
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Failed to create a socket\n");
        exit(1);
    }

    // Connect to the server
    if (connect(s, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
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

        // If the client wants to quit
        if (strcmp(cpy, "QUIT") == 0) {
            close(s);
            printf("200 OK");
            return 0;
        }

        // Send the command to the server
        send(s, buf, len, 0);

        // Receive server feedback
        valread = recv(s, buf, sizeof(buf), 0);
        if (valread <= 0) {
            fprintf(stderr, "Server unexpectedly closed");
            exit(1);
        }
        printf(buf);

        // If the client wants to shut down the server
        if (strcmp(cpy, "SHUTDOWN") == 0) {
            close(s);
            return 0;
        }
    }
}

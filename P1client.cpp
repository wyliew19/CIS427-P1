#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define PORT 2982
#define MAX_LINE 256

int main(int argc, char* argv[]) {
    // Server connection variables
    struct hostent *hp;
    int status, valread, client_fd;
    struct sockaddr_in sin;
    char buf[MAX_LINE];
    char* host;
    int sin_len = sizeof(sin);

    if (argc == 2) {
        host = argv[1];
    } else {
        
    }

}
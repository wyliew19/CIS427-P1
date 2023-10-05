#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sqlite3.h>
#include <unistd.h>
#include <string>

#define PORT 2982
#define MAX_LINE 256
#define INV_COM(n) "400 invalid command\n" + n
#define FORM_ERR(n) "403 message format error\n" + n

int main() {
    
    // Server needed variables
    struct sockaddr_in sin;
    char buf[MAX_LINE];
    int s, new_s, valread;
    socklen_t len = sizeof(sin);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(PORT);

    // SQLite needed variables
    sqlite3 *db;
    char* zErrMsg = 0;
    char *sql;
    
    // Opening database
    int rc = sqlite3_open("PokemonDB.db", &db);
    if (rc) {
        fprintf(stderr, "Failed to open: %s\n", sqlite3_errmsg(db));
        exit(1);
    }

    // Creating socket file descriptor
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Failed to create a socket\n");
        exit(1);
    }

    if (bind(s, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("Failed to bind socket\n");
        exit(1);
    }

    listen(s, 1);
    while(1) {
        if ((new_s = accept(s, (struct sockaddr*)&sin, &len)) < 0) {
            perror("Failed to accept client\n");
            exit(1);
        }

        // Receive the client's request
        valread = recv(new_s, buf, sizeof(buf), 0);
        if (valread <= 0) {
            fprintf(stderr, "Client disconnected or an error occurred.\n");
            close(new_s);
            continue;
        }

        buf[valread] = '\0';

        // Check if the request is a "BUY" request
        if (strncmp(buf, "BUY ", 4) == 0) {
            // Handle the "BUY" request
            buy_request(new_s, db, buf);
        } else {
            // Handle other types of requests or provide an error response
            sprintf(buf, "400 invalid command\nUnrecognized command: %s", buf);
            send(new_s, buf, strlen(buf), 0);
        }

        close(new_s);
    }
}

void buy_request(int client_socket, sqlite3* db, const char* request) {
    char pokemon_name[50];
    char card_type[50];
    char rarity[50];
    double price_per_card;
    int count;
    int owner_id;
    char response[MAX_LINE];
    char sql_query[256];
    char* zErrMsg = 0;

    sscanf(request, "BUY %s %s %s %lf %d %d", pokemon_name, card_type, rarity, &price_per_card, &count, &owner_id);

    // Query the database to get user balance
    double user_balance;
    sprintf(sql_query, "SELECT usd_balance FROM Users WHERE ID=%d;", owner_id);
    
    // Check if user has enough balance
    if (user_balance < price_per_card * count) {
        sprintf(response, "Not enough balance.");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Deduct the price from user's balance
    user_balance -= price_per_card * count;
    sprintf(sql_query, "UPDATE Users SET usd_balance=%.2lf WHERE ID=%d;", user_balance, owner_id);
    
    int rc = sqlite3_exec(db, sql_query, 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return; // Error message
    }

    // Create a new record in the card’s table
    sprintf(sql_query, "INSERT INTO Pokemon_Cards (card_name, card_type, rarity, count, owner_id) VALUES ('%s', '%s', '%s', %d, %d);",
            pokemon_name, card_type, rarity, count, owner_id);

    rc = sqlite3_exec(db, sql_query, 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return; // Error message
    }
    // Send a success response
    sprintf(response, "200 OK\nBOUGHT: New balance: %d %s. User USD balance $%.2lf", count, pokemon_name, user_balance);
    send(client_socket, response, strlen(response), 0);
}
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "sqlite3.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define PORT 2982
#define MAX_LINE 256
#define INV_COM(n) "400 invalid command\n" + n
#define FORM_ERR(n) "403 message format error\n" + n

// Author: Esam Alwaseem
int callback_get_balance(void* user_balance, int argc, char** argv, char** azColName) {
    if (argc == 1) {
        // Assuming that the result set has only one column (usd_balance)
        *((double*)user_balance) = atof(argv[0]);
    }
    return 0; // Continue processing other rows if any
}

// Author: Esam Alwaseem
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

    if (sscanf(request, "BUY %s %s %s %lf %d %d", pokemon_name, card_type, rarity, &price_per_card, &count, &owner_id) != 6) {
        sprintf(response, "403 message format error\nInvalid BUY request format\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Query the database to get user balance
    sprintf(sql_query, "SELECT usd_balance FROM Users WHERE ID=%d;", owner_id);

    double user_balance; // Declare the variable to store the user's balance

    int rc = sqlite3_exec(db, sql_query, callback_get_balance, &user_balance, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return;
    }

    // Check if user has enough balance
    if (user_balance < price_per_card * count) {
        sprintf(response, "Not enough balance.");
        printf("%s\n", response);  // Replaced send with printf
        return;
    }

    // Deduct the price
    user_balance -= price_per_card * count;
    sprintf(sql_query, "UPDATE Users SET usd_balance=%.2lf WHERE ID=%d;", user_balance, owner_id);

    rc = sqlite3_exec(db, sql_query, 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return;
    }

    // Create a new record in the card’s table
    sprintf(sql_query, "INSERT INTO Pokemon_Cards (card_name, card_type, rarity, count, owner_id) VALUES ('%s', '%s', '%s', %d, %d);",
        pokemon_name, card_type, rarity, count, owner_id);

    rc = sqlite3_exec(db, sql_query, 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return;
    }

    // Success response
    sprintf(response, "200 OK\nBOUGHT: New balance: %d %s. User USD balance $%.2lf\n", count, pokemon_name, user_balance);
    send(client_socket, response, strlen(response), 0);
}

// Author: Hadeel Akhdar
void sell_request(int client_socket, sqlite3* db, const char* request) {
    char pokemon_name[50];
    int count;
    double price_per_card;
    int owner_id;
    char response[MAX_LINE];
    char sql_query[256];
    char* zErrMsg = 0;

    // Parse the SELL command request
    if (sscanf(request, "SELL %s %d %lf %d", pokemon_name, &count, &price_per_card, &owner_id) != 4) {
        sprintf(response, "403 message format error\nInvalid SELL request format\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Calculate the total card price to be deposited to the user's balance
    double total_price = price_per_card * count;
    double user_balance;

    // Update the user's balance in the database
    sprintf(sql_query, "UPDATE Users SET usd_balance=usd_balance+%.2lf WHERE ID=%d; " \
                        "SELECT usd_balance FROM Users WHERE ID=%d;", total_price, owner_id, owner_id);

    int rc = sqlite3_exec(db, sql_query, callback_get_balance, &user_balance, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        sprintf(response, "Failed to update user balance.\n");
        send(client_socket, response, strlen(response), 0);
        return; // Error message
    }

    // Send a success response
    sprintf(response, "200 OK\nSOLD: New balance: %d %s. User USD balance $%.2lf\n", count, pokemon_name, user_balance);
    send(client_socket, response, strlen(response), 0);
}

// Author: Will Wylie
void add_user(int client_socket, sqlite3* db, const char* request) {
    char email[255];
    char first_name[255];
    char last_name[255];
    char user_name[255];
    char password[255];
    double usd_balance;
    char response[MAX_LINE];
    char sql_query[256];
    char* zErrMsg = 0;

    // Parse the ADD_USER command request
    if (sscanf(request, "ADD_USER %s %s %s %s %s %lf", email, first_name, last_name, user_name, password, &usd_balance) != 6) {
        sprintf(response, "403 message format error\nInvalid ADD_USER request format\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Create the SQL query to insert a new user
    sprintf(sql_query, "INSERT INTO Users (email, first_name, last_name, user_name, password, usd_balance) VALUES ('%s', '%s', '%s', '%s', '%s', %.2lf);",
        email, first_name, last_name, user_name, password, usd_balance);

    int rc = sqlite3_exec(db, sql_query, 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        if (client_socket) {
            sprintf(response, "SQL Error\nFailed insert into USERS\n");
            send(client_socket, response, strlen(response), 0);
        }
        return;
    }

    // Send a success response
    sprintf(response, "200 OK\nUser added: %s\n", user_name);
    printf("Sending client: %s\n", response);
    send(client_socket, response, strlen(response), 0);
}

// Author: Will Wylie
// Callback function to process the SELECT query result
int callback_get_list(void* data, int argc, char** argv, char** azColName) {
    char* result = (char*)data;

    // Loop through the query result
    for (int i = 0; i < argc; i++) {
        // Append the column values to the result string
        strcat(result, azColName[i]);
        strcat(result, ": ");
        strcat(result, argv[i]);
        strcat(result, "\n");
    }
    strcat(result, "\n");

    return 0; // Continue processing other rows if any
}

// Author: Mohammed Al-Mohammed
void list_request(int client_socket, sqlite3* db) { 
    char response[MAX_LINE];
    char* zErrMsg = 0;

    // Initialize the result string
    char* result = (char*)malloc(MAX_LINE);
    strcpy(result, "");

    // Query the database to get a list of available Pokemon cards
    const char* sql_query = "SELECT ID, card_name, card_type, rarity, count, owner_id FROM Pokemon_Cards;";

    int rc = sqlite3_exec(db, sql_query, callback_get_list, result, &zErrMsg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        free(result); // Free the allocated memory
        return;
    }

    // Respond with the list of Pokemon cards
    if (strlen(result) > 0) {
        sprintf(response, "200 OK\n%s", result);
        send(client_socket, response, strlen(response), 0);
    } else {
        sprintf(response, "200 OK\nNo Pokemon cards available.\n");
        send(client_socket, response, strlen(response), 0);
    }

    // Free the allocated memory
    free(result);
}

// Author: Mohammed Al-Mohammed
void balance_request(int client_socket, sqlite3* db, const char* request) {
    int owner_id;
    char response[MAX_LINE];
    char sql_query[256];
    char* zErrMsg = 0;

    // Parse the BALANCE command request
    if (sscanf(request, "BALANCE %d", &owner_id) != 1) {
        sprintf(response, "403 message format error\nInvalid BALANCE request format\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Query the database to get user balance
    sprintf(sql_query, "SELECT usd_balance FROM Users WHERE ID=%d;", owner_id);
    double user_balance = 0.0;
    int rc = sqlite3_exec(db, sql_query, callback_get_balance, &user_balance, &zErrMsg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return;
    }

    // Respond with the user's balance
    sprintf(response, "200 OK\nBalance for user ID %d: $%.2lf\n", owner_id, user_balance);
    send(client_socket, response, strlen(response), 0);
}

// Author: Will Wylie
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
    char* req = (char*)malloc(sizeof(buf));
    while(1) {
        if ((new_s = accept(s, (struct sockaddr*)&sin, &len)) < 0) {
            perror("Failed to accept client\n");
            exit(1);
        }

        // Receive the client's request
        while(valread = recv(new_s, buf, sizeof(buf), 0)) {

            
            strcpy(req, buf);
            printf("Received: \"%s\"\n", req);

            // Check if the request is a "BUY" request
            if (strncmp(req, "BUY", 3) == 0) {
                // Handle the "BUY" request
                buy_request(new_s, db, req);
            } else if (strncmp(req, "SELL", 4) == 0) {
                // Handle the "SELL" request
                sell_request(new_s, db, req);
            } else if (strncmp(req, "ADD_USER", 8) == 0) {
                // Handle the "ADD_USER" request
                add_user(new_s, db, req);
            } else if (strncmp(req, "LIST", 4) == 0) {
                // Handle the "LIST" request
                list_request(new_s, db);
            } else if (strncmp(req,"BALANCE", 7) == 0) {
                // Handle the "BALANCE" request
                balance_request(new_s, db, req);
            } else if (strncmp(req, "SHUTDOWN", 8) == 0) {
                // Handle the "SHUTDOWN" request
                close(new_s);
                goto SHUTDOWN;
            } else {
                // Handle other types of requests or provide an error response
                sprintf(buf, "400 invalid command\nUnrecognized command: %s\n", req);
                send(new_s, buf, strlen(buf), 0);
            }
        }
    }
SHUTDOWN:
    free(req);
    shutdown(s, SHUT_RDWR);
    sqlite3_close(db);
    return 0;
}

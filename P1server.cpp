#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "sqlite3.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>

#define PORT 2982
#define MAX_LINE 256
#define MAX_CLIENTS 4
#define ROOT_USERID 0

struct user {
    int fd;
    char* username;
    int id;

    user& operator=(int new_fd) {
        this->fd = new_fd;
        memset(this->username, 0, sizeof(this->username));
        this->id = -1;
        return *this;
    }
};

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

// Author: Esam Alwaseem
void deposit_request(int client_socket, sqlite3* db, const char* request) {
    int user_id;
    double deposit_amount;
    char response[MAX_LINE];
    char sql_query[256];
    char* zErrMsg = 0;

    // Parse the DEPOSIT command request
    if (sscanf(request, "DEPOSIT %d %lf", &user_id, &deposit_amount) != 2) {
        sprintf(response, "403 message format error\nInvalid DEPOSIT request format\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Check for a valid deposit amount
    if (deposit_amount <= 0) {
        sprintf(response, "403 Invalid deposit amount\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Update the user's balance in the database
    sprintf(sql_query, "UPDATE Users SET usd_balance = usd_balance + %.2lf WHERE ID = %d; "
                       "SELECT usd_balance FROM Users WHERE ID = %d;", 
                       deposit_amount, user_id, user_id);
    
    double new_balance = 0.0;
    int rc = sqlite3_exec(db, sql_query, callback_get_balance, &new_balance, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        sprintf(response, "Failed to update user balance.\n");
        send(client_socket, response, strlen(response), 0);
        return; // Error message
    }

    // Send a success response with the new balance
    sprintf(response, "200 OK\nDeposit successful. New User balance: $%.2lf\n", new_balance);
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

int callback_format_list(void* notUsed, int argc, char** argv, char** azColName) {
    char* data = (char*)notUsed;
    int offset = strlen(data);

    // Add a formatted string for each record to the result
    for (int i = 0; i < argc; i++) {
        offset += sprintf(data + offset, "%s ", argv[i] ? argv[i] : "NULL");
    }
    sprintf(data + offset, "\n");
    return 0; // Continue processing other rows if any
}

// Modified the list_request to include owner_id parameter and a check for the root user
void list_request(int client_socket, sqlite3* db, int owner_id, int is_root) { 
    char response[MAX_LINE * 10]; // Increase buffer size as needed to accommodate the list
    char* zErrMsg = 0;

    // Determine the SQL query based on the user's privileges
    char sql_query[MAX_LINE];
    if (is_root) {
        sprintf(sql_query, "SELECT ID, card_name, card_type, rarity, count, owner_id FROM Pokemon_Cards;");
    } else {
        sprintf(sql_query, "SELECT ID, card_name, card_type, rarity, count, owner_id FROM Pokemon_Cards WHERE owner_id=%d;", owner_id);
    }

    int rc = sqlite3_exec(db, sql_query, callback_format_list, response, &zErrMsg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        sprintf(response, "500 Internal Server Error\nCould not retrieve the list.\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // If the result string is empty, no records are available
    if (strlen(response) == 0) {
        sprintf(response, "200 OK\nNo Pokemon cards available for the user.\n");
    } else {
        // Prepend the response status to the actual data
        char full_response[MAX_LINE * 10];
        sprintf(full_response, "200 OK\nThe list of records in the Pokemon cards table for %s user:\n%s",
                is_root ? "ALL" : "current", response);
        strcpy(response, full_response);
    }

    send(client_socket, response, strlen(response), 0);
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

// LOGOUT Function
void logout_request(int client_socket, user* client_sockets) {
    int client_index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i].fd == client_socket) {
            client_index = i;
            break;
        }
    }

    if (client_index == -1) {
        // Client not found, handle the error
        char response[] = "400 Client not found\n";
        send(client_socket, response, sizeof(response) - 1, 0);
        close(client_socket);
        return;
    }

    // Clear the user's username and close the socket
    memset(client_sockets[client_index].username, 0, sizeof(client_sockets[client_index].username));
    close(client_socket);

    // Return a success response
    char response[] = "200 OK\n";
    send(client_socket, response, strlen(response) - 1, 0);
}

// LOGIN function
void login(int client_socket, sqlite3* db, user& client) {
    char buf[MAX_LINE];
    char req[MAX_LINE];
    int valread = recv(client_socket, buf, strlen(buf), 0);
    if (valread <= 0) {
        close(client_socket);
        return;
    }
    strncpy(req, buf, valread);
    req[valread] = '\0';

    char command[64];
    char username[64];
    char password[64];

    if (sscanf(req, "%63s %63s %63s", command, username, password) != 3 || strcmp(command, "LOGIN") != 0) {
        char* response = "400 Bad Request\nInvalid LOGIN command format\n";
        send(client_socket, response, strlen(response), 0);
        close(client_socket);
        return;
    }

    // Check if the user credentials are correct in your SQLite database
    char query[256];
    snprintf(query, sizeof(query), "SELECT username FROM users WHERE username='%s' AND password='%s';", username, password);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK) {
        char* zErrMsg = (char*)sqlite3_errmsg(db);
        fprintf(stderr,"SQLite error: ", zErrMsg, "\n");
        close(client_socket);
        return;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // Successful login
        strncpy(client.username, username, sizeof(client.username));
        char* response = "200 OK\n";
        send(client_socket, response, strlen(response), 0);
    } else {
        // Wrong UserID or Password
        char* response = "403 Wrong UserID or Password\n";
        send(client_socket, response, strlen(response), 0);
        close(client_socket);
    }

    sqlite3_finalize(stmt);
}


// Author: Will Wylie
int main() {
    struct sockaddr_in sin;
    int s, opts = 1;
    socklen_t len = sizeof(sin);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(PORT);

    sqlite3* db;
    char* zErrMsg = 0;
    char* sql;
    int rc = sqlite3_open("PokemonDB.db", &db);

    if (rc) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db));
        exit(1);
    }

    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Failed to create a socket\n");
        exit(1);
    }

    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts));

    // Setting options non-blocking
    if((opts = fcntl(s, F_GETFL)) < 0) { // Get current options
        perror("Error in getting current options\n");
    }
    opts = (opts | O_NONBLOCK); // Don't clobber your old settings
    if(fcntl(s, F_SETFL, opts) < 0) 
        perror("Error\n");

    if (bind(s, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("Failed to bind socket\n");
        exit(1);
    }

    if (bind(s, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("Failed to bind socket\n");
        exit(1);
    }

    listen(s, MAX_CLIENTS);

    user client_sockets[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = -1;
    }

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s, &read_fds);

        int max_fd = s;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int client_socket = client_sockets[i].fd;
            if (client_socket != -1) {
                FD_SET(client_socket, &read_fds);
                if (client_socket > max_fd) {
                    max_fd = client_socket;
                }
            }
        }

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select failed\n");
            exit(1);
        }

        if (FD_ISSET(s, &read_fds)) {
            // New client connection
            int new_s;
            if ((new_s = accept(s, (struct sockaddr*)&sin, &len)) < 0) {
                perror("Failed to accept client\n");
                exit(1);
            }

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i].fd == -1) {
                    client_sockets[i] = new_s;
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int client_socket = client_sockets[i].fd;
            if (client_socket != -1 && FD_ISSET(client_socket, &read_fds)) {
                if (client_sockets[i].username[0] == '\0') {
                    login(client_socket, db, client_sockets[i]);
                } else {
                    char buf[MAX_LINE];
                    char req[MAX_LINE];
                    int valread = recv(client_socket, buf, sizeof(buf), 0);

                    if (valread <= 0) {
                        // Client disconnected or an error occurred
                        logout_request(client_socket, client_sockets); // Added LOGOUT handling
                    } else {
                        strncpy(req, buf, valread);
                        req[valread] = '\0';

                        printf("Received from client %d: \"%s\"\n", i, req);

                        if (strncmp(req, "BUY", 3) == 0) {
                            buy_request(client_socket, db, req);
                        } else if (strncmp(req, "SELL", 4) == 0) {
                            sell_request(client_socket, db, req);
                        } else if (strncmp(req, "ADD_USER", 8) == 0) {
                            add_user(client_socket, db, req);
                        } else if (strncmp(req, "LIST", 4) == 0) {
                            list_request(client_socket, db, client_sockets[i].id, client_sockets[i].id == ROOT_USERID);
                        } else if (strncmp(req, "BALANCE", 7) == 0) {
                            balance_request(client_socket, db, req);
                        } else if (strncmp(req, "LOGOUT", 6) == 0) {
                            logout_request(client_socket, client_sockets);
                        } else if (strncmp(req, "SHUTDOWN", 8) == 0) {
                            if (client_sockets[i].id == ROOT_USERID)
                                goto SHUTDOWN;
                        } else {
                            sprintf(buf, "400 invalid command\nUnrecognized command: %s\n", req);
                            send(client_socket, buf, strlen(buf), 0);
                        }
                    }
                }
            }
        }
    }
SHUTDOWN:
    for (user u : client_sockets) {
        if (u.fd != -1)
            close(u.fd);
    }
    shutdown(s, SHUT_RDWR);
    sqlite3_close(db);
    return 0;
}

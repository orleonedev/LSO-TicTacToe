#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include "../common/include/protocol.h"

int sock;
char my_name[MAX_NAME_PLAYER];
char my_symbol;
char board[3][3];

void handle_lobby();
void handle_hosting_wait();
void handle_join_wait();
void play_match();
void send_request(MessageType type, const void *payload, int payload_size);
void print_board();
void clear_screen();
void list_games(); 
void consume_payload(int sock, int size);

/*
 * Main entry point for the client application.
 * Connects to the server and handles the login process.
 */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server.\n");

    while (1) {
        printf("Enter Username: ");
        char name[MAX_NAME_PLAYER];
        fgets(name, MAX_NAME_PLAYER, stdin);
        name[strcspn(name, "\n")] = 0;

        LoginRequest req;
        strncpy(req.name, name, MAX_NAME_PLAYER);
        send_request(CMD_LOGIN, &req, sizeof(req));

        PacketHeader header;
        recv(sock, &header, sizeof(header), 0);
        
        char buffer[128];
        if (header.payload_size > 0) recv(sock, buffer, header.payload_size, 0);

        if (header.type == RSP_OK) {
            strncpy(my_name, name, MAX_NAME_PLAYER);
            printf("Login Successful!\n");
            break;
        } else {
            printf("Login Failed: %s\n", buffer);
        }
    }

    handle_lobby();

    close(sock);
    return 0;
}

/*
 * Consumes remaining payload bytes from the socket to prevent desynchronization.
 * Used when a packet is received but its payload is not needed or handled.
 */
void consume_payload(int sock, int size) {
    if (size <= 0) return;
    char *buf = malloc(size);
    int total = 0;
    while (total < size) {
        int r = recv(sock, buf + total, size - total, 0);
        if (r <= 0) break;
        total += r;
    }
    free(buf);
}

/*
 * Sends a request packet to the server.
 * Combines the header and optional payload into the send operation.
 */
void send_request(MessageType type, const void *payload, int payload_size) {
    PacketHeader header;
    header.type = type;
    header.payload_size = payload_size;
    send(sock, &header, sizeof(header), 0);
    if (payload_size > 0) send(sock, payload, payload_size, 0);
}

/*
 * Clears the terminal screen.
 */
void clear_screen() {
    system("clear");
}

/*
 * Requests and displays the list of active games from the server.
 */
void list_games() {
    printf("--- LOBBY ---\n");
    send_request(CMD_LIST_GAMES, NULL, 0);
    PacketHeader header;
    recv(sock, &header, sizeof(header), 0); 
    if (header.type == RSP_GAME_LIST) {
        int count;
        recv(sock, &count, sizeof(int), 0);
        printf("\nActive Games: %d\n", count);
        for (int i=0; i<count; i++) {
            GameInfoDTO game;
            recv(sock, &game, sizeof(game), 0);
            printf("[%d] Owner: %s | Status: %s\n", game.id, game.owner, game.status);
        }
    }
}

/*
 * Manages the main lobby interaction loop.
 * Allows the user to refresh list, create game, join game, or quit.
 */
void handle_lobby() {
    fd_set fds;
    
    const char *menu_str =
        "\n--- MENU ---\n"
        "[1] Refresh Lobby\n"
        "[2] Create New Game\n"
        "[3] Join Existing Game\n"
        "[4] Quit\n"
        "Make a choice > ";

    my_symbol = 0; 
    
    clear_screen();
    list_games();
    printf("%s", menu_str);
    fflush(stdout);

    while (1) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(STDIN_FILENO, &fds);

        select(sock+1, &fds, NULL, NULL, NULL);

        if (FD_ISSET(sock, &fds)) {
            PacketHeader header;
            if (recv(sock, &header, sizeof(header), MSG_PEEK) <= 0) break; 

            if (header.type == RSP_LOBBY_UPDATE) {
                recv(sock, &header, sizeof(header), 0);
                clear_screen();
                list_games();
                printf("%s", menu_str); //Reprint menu
                fflush(stdout);
            } else {
                recv(sock, &header, sizeof(header), 0);
                if (header.payload_size > 0) {
                    char buf[1024]; recv(sock, buf, header.payload_size, 0);
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char choice_buf[10];
            if (fgets(choice_buf, sizeof(choice_buf), stdin) == NULL) break;
            int choice = atoi(choice_buf);

            if (choice == 1) {
                clear_screen();
                list_games();
                printf("%s", menu_str);
                fflush(stdout);
            }
            else if (choice == 2) {
                printf("Choose Symbol (X/O): ");
                char sym_buf[10];
                fgets(sym_buf, sizeof(sym_buf), stdin);
                CreateGameRequest req;
                req.symbol = (sym_buf[0] == 'o' || sym_buf[0] == 'O') ? 'O' : 'X';
                send_request(CMD_CREATE_GAME, &req, sizeof(req));
                
                PacketHeader header;
                recv(sock, &header, sizeof(header), 0);
                if (header.type == RSP_OK) {
                    char msg[128];
                    recv(sock, msg, header.payload_size, 0);
                    printf("Game Created. Entering Host Mode...\n");
                    my_symbol = req.symbol;
                    
                    handle_hosting_wait();
                    
                    clear_screen();
                    list_games();
                    printf("%s", menu_str);
                    fflush(stdout);
                }
            }
            else if (choice == 3) {
                printf("Enter Game ID: ");
                char id_buf[10];
                fgets(id_buf, sizeof(id_buf), stdin);
                JoinGameRequest req;
                req.game_id = atoi(id_buf);
                send_request(CMD_JOIN_GAME, &req, sizeof(req));
                
                PacketHeader header;
                recv(sock, &header, sizeof(header), 0);
                
                if (header.type == RSP_OK) {
                    char msg[128];
                    recv(sock, msg, header.payload_size, 0);
                    printf("%s\n", msg);
                    
                    handle_join_wait();
                    
                    clear_screen();
                    list_games();
                    printf("%s", menu_str);
                    fflush(stdout);
                } else if (header.type == RSP_ERROR) {
                    char msg[128];
                    recv(sock, msg, header.payload_size, 0);
                    printf("\n[!] Error: %s\n", msg);
                    printf("Make a choice > ");
                    fflush(stdout);
                }
            }
            else if (choice == 4) {
                send_request(CMD_DISCONNECT, NULL, 0);
                printf("Quitting...\n");
                break;
            }
            else {
                printf("Invalid choice.\nMake a choice > ");
                fflush(stdout);
            }
        }
    }
}

/*
 * Loop for the host waiting for challengers.
 * Handles incoming join requests and game start signals.
 */
void handle_hosting_wait() {
    printf("Waiting for challengers... (Type 'c' to Stop Hosting)\n");
    fd_set fds;
    
    while(1) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(STDIN_FILENO, &fds);
        
        select(sock+1, &fds, NULL, NULL, NULL);
        
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char buf[10];
            fgets(buf, sizeof(buf), stdin);
            if (buf[0] == 'c') {
                send_request(CMD_STOP_HOSTING, NULL, 0);
                printf("Stopping hosting...\n");
                return;
            }
        }

        if (FD_ISSET(sock, &fds)) {
            PacketHeader header;
            if (recv(sock, &header, sizeof(header), MSG_PEEK) <= 0) return;
            recv(sock, &header, sizeof(header), 0);
            
            if (header.type == RSP_JOIN_REQUEST) {
                char challenger[MAX_NAME_PLAYER];
                recv(sock, challenger, header.payload_size, 0);
                
                int decision = -1; // -1=pending, 0=No, 1=Yes, 2=CancelledByOpponent

                printf("\nPlayer '%s' wants to join! Accept? (1=Yes, 0=No): ", challenger);
                fflush(stdout);

                while (decision == -1) {
                    fd_set d_fds;
                    FD_ZERO(&d_fds);
                    FD_SET(sock, &d_fds);
                    FD_SET(STDIN_FILENO, &d_fds);

                    select(sock+1, &d_fds, NULL, NULL, NULL);

                    if (FD_ISSET(STDIN_FILENO, &d_fds)) {
                        char ans_buf[10];
                        if (fgets(ans_buf, sizeof(ans_buf), stdin) == NULL) break;
                        if (ans_buf[0] == '1') { decision = 1; } 
                        else if (ans_buf[0] == '0') { decision = 0; } 
                        else { printf("[!] Invalid input. (1=Yes, 0=No): "); fflush(stdout); }
                    }

                    if (FD_ISSET(sock, &d_fds)) {
                        PacketHeader h2;
                        if (recv(sock, &h2, sizeof(h2), MSG_PEEK) <= 0) return;
                        
                        if (h2.type == RSP_JOIN_CANCELLED) {
                            recv(sock, &h2, sizeof(h2), 0);
                            char msg[128];
                            recv(sock, msg, h2.payload_size, 0);
                            printf("\n[!] Player '%s' cancelled request.\nWaiting for challengers... (Type 'c' to Stop Hosting)\n", msg);
                            decision = 2;
                        } else {
                             recv(sock, &h2, sizeof(h2), 0);
                             consume_payload(sock, h2.payload_size);
                        }
                    }
                }

                if (decision == 1 || decision == 0) {
                    send_request(CMD_HOST_DECISION, &decision, sizeof(int));
                    if (decision == 1) printf("Decision sent. Starting game...\n");
                    else printf("Rejected. Waiting for challengers...\n");
                }
            }
            else if (header.type == RSP_GAME_START) {
                char start_sym[2];
                recv(sock, start_sym, 2, 0);
                printf("Game Started! %c moves first.\n", start_sym[0]);
                play_match();
                return; 
            }
            else if (header.type == RSP_OPPONENT_QUIT) {
                 printf("\n[!] Opponent disconnected while joining.\nReturning to lobby...\n");
                 sleep(2);
                 return;
            }
            else if (header.type == RSP_OK) {
                 char msg[128]; recv(sock, msg, header.payload_size, 0);
                 if (strcmp(msg, "Lobby Closed") == 0) {
                     return;
                 }
            }
            else {
                consume_payload(sock, header.payload_size);
            }
        }
    }
}

/*
 * Loop for a client waiting to join a game.
 * Allows cancellation of the join request.
 */
void handle_join_wait() {
    printf("Waiting for Host approval... (Type 'c' to Cancel)\n");
    fd_set fds;

    while(1) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(STDIN_FILENO, &fds);

        select(sock+1, &fds, NULL, NULL, NULL);

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char buf[10];
            fgets(buf, sizeof(buf), stdin);
            if (buf[0] == 'c') {
                send_request(CMD_CANCEL_JOIN, NULL, 0);
                printf("Cancelling request...\n");
                return;
            }
        }

        if (FD_ISSET(sock, &fds)) {
            PacketHeader header;
            if (recv(sock, &header, sizeof(header), 0) <= 0) return;
            
            if (header.type == RSP_REQUEST_RESULT) {
                int accepted;
                recv(sock, &accepted, sizeof(int), 0);
                if (accepted) {
                    printf("Host Accepted! Joining...\n");
                } else {
                    printf("Host Rejected your request.\n");
                    sleep(2);
                    return; 
                }
            }
            else if (header.type == RSP_MATCH_FOUND) {
                char msg[128];
                recv(sock, msg, header.payload_size, 0);
                char opp_name[MAX_NAME_PLAYER];
                sscanf(msg, "%s %c", opp_name, &my_symbol);
                printf("Match Confirmed vs %s. You are %c\n", opp_name, my_symbol);
            }
            else if (header.type == RSP_GAME_START) {
                char start_sym[2];
                recv(sock, start_sym, 2, 0);
                printf("Game Started! %c moves first.\n", start_sym[0]);
                play_match();
                return;
            }
            else if (header.type == RSP_OK) {
                char msg[128]; recv(sock, msg, header.payload_size, 0);
                return;
            }
        }
    }
}

/*
 * Draws the current state of the Tic-Tac-Toe board to the screen.
 */
void print_board() {
    clear_screen();
    printf("\n");
    printf(" %c | %c | %c \n", board[0][0], board[0][1], board[0][2]);
    printf("---|---|---\n");
    printf(" %c | %c | %c \n", board[1][0], board[1][1], board[1][2]);
    printf("---|---|---\n");
    printf(" %c | %c | %c \n", board[2][0], board[2][1], board[2][2]);
    printf("\n");
}

/*
 * Main game loop.
 * Handles moves, opponent moves, and game end conditions.
 */
void play_match() {
    memset(board, ' ', 9);
    int game_over = 0;
    
    print_board();
    if (my_symbol == 'X') {
        printf("Your turn! Enter cell (0-8): "); 
        fflush(stdout);
    } else {
        printf("Waiting for opponent...\n");
    }

    fd_set fds;
    while (!game_over) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(STDIN_FILENO, &fds);

        select(sock+1, &fds, NULL, NULL, NULL);

        if (FD_ISSET(sock, &fds)) {
            PacketHeader header;
            if (recv(sock, &header, sizeof(header), 0) <= 0) break;

            if (header.type == RSP_OPPONENT_MOVE) {
                MoveRequest move;
                recv(sock, &move, sizeof(move), 0);
                char opp_sym = (my_symbol == 'X') ? 'O' : 'X';
                board[move.cell_index/3][move.cell_index%3] = opp_sym;
                print_board();
                printf("Your turn! Enter cell (0-8): ");
                fflush(stdout);
            }
            else if (header.type == RSP_GAME_OVER) {
                int result;
                recv(sock, &result, sizeof(int), 0);
                
                print_board();
                if (result == 1) printf("YOU WON!\n");
                else if (result == 2) printf("YOU LOST!\n");
                else printf("DRAW!\n");
                
                if (result == 2) {
                     printf("Returning to lobby in 3 seconds...\n");
                     sleep(3);
                     game_over = 1;
                }
            }
            else if (header.type == RSP_ASK_PLAY_AGAIN) {
                printf("\nDo you want to host a new game? (1=Yes, 0=No): ");
                fflush(stdout); 
                
                char buf[10];
                fgets(buf, sizeof(buf), stdin);
                int choice = atoi(buf);
                send_request(CMD_PLAY_AGAIN, &choice, sizeof(int));
                
                if (choice == 1) {
                    printf("Choose Symbol for new game (X/O): ");
                    fflush(stdout); 
                    char sym_buf[10];
                    fgets(sym_buf, sizeof(sym_buf), stdin);
                    char new_sym = (sym_buf[0] == 'o' || sym_buf[0] == 'O') ? 'O' : 'X';
                    send(sock, &new_sym, sizeof(char), 0);
                    my_symbol = new_sym; 

                    printf("Waiting for server...\n");

                    while (1) {
                        PacketHeader h2;
                        if (recv(sock, &h2, sizeof(h2), 0) <= 0) return;
                        
                        if (h2.type == RSP_OK) {
                            char msg[128];
                            recv(sock, msg, h2.payload_size, 0);
                            printf("%s\n", msg);
                            handle_hosting_wait(); 
                            return;
                        } 
                        else if (h2.type == RSP_ERROR) {
                             char msg[128];
                             recv(sock, msg, h2.payload_size, 0);
                             printf("Error: %s\n", msg);
                             sleep(2);
                             return;
                        }
                        else {
                            consume_payload(sock, h2.payload_size);
                        }
                    }
                } else {
                    return;
                }
            }
            else if (header.type == RSP_INVALID_MOVE) {
                 printf("\n[!] Invalid Move. Try again: ");
                 fflush(stdout);
            }
            else if (header.type == RSP_ERROR) {
                 char msg[128];
                 recv(sock, msg, header.payload_size, 0);
                 printf("\n[!] Error: %s\n", msg);
            }
            else {
                consume_payload(sock, header.payload_size);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds) && !game_over) {
            char move_buf[10];
            if (fgets(move_buf, sizeof(move_buf), stdin)) {
                 MoveRequest req;
                 req.cell_index = atoi(move_buf);
                 send_request(CMD_MOVE, &req, sizeof(req));
                 
                 if (req.cell_index >= 0 && req.cell_index < 9 && board[req.cell_index/3][req.cell_index%3] == ' ') {
                    board[req.cell_index/3][req.cell_index%3] = my_symbol;
                    print_board();
                    printf("Waiting for opponent...\n");
                 }
            }
        }
    }
}

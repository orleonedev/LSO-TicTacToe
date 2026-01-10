#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include "../common/include/protocol.h"

// Globals
int sock;
char my_name[MAX_NAME_PLAYER];
char my_symbol;
char board[3][3];

// Prototypes
void handle_lobby();
void handle_hosting_wait();
void handle_join_wait();
void play_match();
void send_request(MessageType type, const void *payload, int payload_size);
void print_board();
void clear_screen();
void list_games(); // Helper to refresh list

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Connect
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

    // Login
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

void send_request(MessageType type, const void *payload, int payload_size) {
    PacketHeader header;
    header.type = type;
    header.payload_size = payload_size;
    send(sock, &header, sizeof(header), 0);
    if (payload_size > 0) send(sock, payload, payload_size, 0);
}

void clear_screen() {
    system("clear");
}

void list_games() {
    printf("--- LOBBY ---\n");
    send_request(CMD_LIST_GAMES, NULL, 0);
    PacketHeader header;
    recv(sock, &header, sizeof(header), 0); // Should be RSP_GAME_LIST
    if (header.type == RSP_GAME_LIST) {
        int count;
        recv(sock, &count, sizeof(int), 0);
        printf("\nActive Games: %d\n", count);
        for (int i=0; i<count; i++) {
            GameInfoDTO game;
            recv(sock, &game, sizeof(game), 0);
            printf("[%d] Owner: %s | Status: %s\n", game.id, game.owner, game.status);
        }
        printf("\nCommands: [1] Refresh [2] Create [3] Join [4] Quit\nChoice: ");
        fflush(stdout);
    }
}

void handle_lobby() {
    fd_set fds;
    
    // Initial List
    clear_screen();
    list_games();

    while (1) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(STDIN_FILENO, &fds);

        select(sock+1, &fds, NULL, NULL, NULL);

        // Check Server Messages (e.g. Lobby Updates)
        if (FD_ISSET(sock, &fds)) {
            PacketHeader header;
            // Peek to see type? No, read it.
            ssize_t r = recv(sock, &header, sizeof(header), MSG_PEEK); 
            if (r <= 0) break; // Disconnect

            if (header.type == RSP_LOBBY_UPDATE) {
                // Consume packet
                recv(sock, &header, sizeof(header), 0); 
                clear_screen();
                list_games();
            } else {
                // Unexpected packet in lobby? Consume and ignore or error.
                recv(sock, &header, sizeof(header), 0);
                if (header.payload_size > 0) {
                    char buf[1024]; recv(sock, buf, header.payload_size, 0);
                }
            }
        }

        // Check User Input
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char choice_buf[10];
            if (fgets(choice_buf, sizeof(choice_buf), stdin) == NULL) break;
            int choice = atoi(choice_buf);

            if (choice == 1) {
                clear_screen();
                list_games();
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
                    // Return to lobby loop after game ends
                    clear_screen();
                    list_games();
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
                    printf("%s\nWaiting for Host approval...\n", msg);
                    handle_join_wait();
                    clear_screen();
                    list_games();
                } else if (header.type == RSP_ERROR) {
                    char msg[128];
                    recv(sock, msg, header.payload_size, 0);
                    printf("Error: %s\n", msg);
                }
            }
            else if (choice == 4) {
                send_request(CMD_DISCONNECT, NULL, 0);
                break;
            }
        }
    }
}

void handle_hosting_wait() {
    printf("Waiting for challengers... (Ctrl+C to abort TODO)\n");
    fd_set fds;
    
    while(1) {
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        // We don't listen to STDIN here unless we want to allow cancelling.
        // For simplicity, only listen to server.

        select(sock+1, &fds, NULL, NULL, NULL);
        
        if (FD_ISSET(sock, &fds)) {
            PacketHeader header;
            recv(sock, &header, sizeof(header), 0);
            
            if (header.type == RSP_JOIN_REQUEST) {
                char challenger[MAX_NAME_PLAYER];
                recv(sock, challenger, header.payload_size, 0);
                printf("\nPlayer '%s' wants to join! Accept? (1=Yes, 0=No): ", challenger);
                fflush(stdout);
                
                char ans_buf[10];
                fgets(ans_buf, sizeof(ans_buf), stdin);
                int decision = atoi(ans_buf);
                
                send_request(CMD_HOST_DECISION, &decision, sizeof(int));
                printf("Decision sent. Waiting...\n");
            }
            else if (header.type == RSP_GAME_START) {
                char start_sym[2];
                recv(sock, start_sym, 2, 0);
                printf("Game Started! %c moves first.\n", start_sym[0]);
                play_match();
                return; // Game Over
            }
        }
    }
}

void handle_join_wait() {
    PacketHeader header;
    while(1) {
        recv(sock, &header, sizeof(header), 0);
        
        if (header.type == RSP_REQUEST_RESULT) {
            int accepted;
            recv(sock, &accepted, sizeof(int), 0);
            if (accepted) {
                // Wait for RSP_GAME_START which follows immediately
                printf("Host Accepted! Joining...\n");
            } else {
                printf("Host Rejected your request.\n");
                sleep(3);
                return; // Back to lobby
            }
        }
        else if (header.type == RSP_MATCH_FOUND) {
            // Should be followed by GAME_START
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
    }
}

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

void play_match() {
    memset(board, ' ', 9);
    int game_over = 0;
    
    // Initial Board
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
            recv(sock, &header, sizeof(header), 0);

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
                
                if (result == 1) printf("YOU WON!\n");
                else if (result == 2) printf("YOU LOST!\n");
                else printf("DRAW!\n");
                
                if (result == 2) {
                     // Lost: Wait and exit loop
                     printf("Returning to lobby in 3 seconds...\n");
                     sleep(3);
                     game_over = 1;
                }
                // If Won/Draw: Wait for RSP_ASK_PLAY_AGAIN in next loop iteration
            }
            else if (header.type == RSP_ASK_PLAY_AGAIN) {
                printf("Play again? (1=Yes, 0=No): ");
                fflush(stdout);
                
                char buf[10];
                fgets(buf, sizeof(buf), stdin);
                int choice = atoi(buf);
                send_request(CMD_PLAY_AGAIN, &choice, sizeof(int));
                
                if (choice == 1) {
                    // Prompt for new symbol
                    printf("Choose Symbol for next game (X/O): ");
                    fflush(stdout);
                    char sym_buf[10];
                    fgets(sym_buf, sizeof(sym_buf), stdin);
                    char new_sym = (sym_buf[0] == 'o' || sym_buf[0] == 'O') ? 'O' : 'X';
                    send(sock, &new_sym, sizeof(char), 0);
                    my_symbol = new_sym; // Update local symbol

                    // Expect RSP_OK for new game
                    PacketHeader h2;
                    recv(sock, &h2, sizeof(h2), 0);
                    if (h2.type == RSP_OK) {
                        char msg[128];
                        recv(sock, msg, h2.payload_size, 0);
                        printf("%s\n", msg);
                        handle_hosting_wait(); // Enter host wait loop
                    }
                    return; // Return to lobby (after game ends/hosting aborts)
                } else {
                    return; // Return to lobby
                }
            }
            // TODO: HANDLE EVERY TYPE OF ERROR
            else if (header.type == RSP_ERROR) {
                 // Opponent quit
                 char msg[128];
                 recv(sock, msg, header.payload_size, 0);
                 printf("%s\n", msg);
                 game_over = 1;
                 sleep(3);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds) && !game_over) {
            char move_buf[10];
            if (fgets(move_buf, sizeof(move_buf), stdin)) {
                 // Check if it's our turn? We just send. Server validates.
                 MoveRequest req;
                 req.cell_index = atoi(move_buf);
                 send_request(CMD_MOVE, &req, sizeof(req));
                 // Speculative update? No, wait for error or just update.
                 // Ideally update only on ACK. But for responsiveness update now.
                 if (board[req.cell_index/3][req.cell_index%3] == ' ') {
                    board[req.cell_index/3][req.cell_index%3] = my_symbol;
                    print_board();
                    printf("Waiting for opponent...\n");
                 }
            }
        }
    }
}
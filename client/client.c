#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../common/include/protocol.h"

// Globals
int sock;
char my_name[MAX_NAME_PLAYER];
char my_symbol;
char board[3][3];

// Prototypes
void show_menu();
void handle_lobby();
void handle_game();
void send_request(MessageType type, const void *payload, int payload_size);
void print_board();
void clear_screen();

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

void handle_lobby() {
    while (1) {
        // clear_screen();
        printf("\n--- LOBBY #%s ---\n", my_name);
        printf("1. List Games\n");
        printf("2. Create Game\n");
        printf("3. Join Game\n");
        printf("4. Quit\n");
        printf("Choice: ");
        
        char choice_buf[10];
        fgets(choice_buf, sizeof(choice_buf), stdin);
        int choice = atoi(choice_buf);

        if (choice == 1) {
            send_request(CMD_LIST_GAMES, NULL, 0);
            PacketHeader header;
            recv(sock, &header, sizeof(header), 0);
            if (header.type == RSP_GAME_LIST) {
                int count;
                recv(sock, &count, sizeof(int), 0);
                clear_screen();
                printf("\nActive Games: %d\n", count);
                for (int i=0; i<count; i++) {
                    GameInfoDTO game;
                    recv(sock, &game, sizeof(game), 0);
                    printf("[%d] Owner: %s | Status: %s\n", game.id, game.owner, game.status);
                }
            }
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
                clear_screen();
                printf("Game Created! Waiting for opponent...\n");
                my_symbol = req.symbol;
                handle_game(); // Enter wait/game loop
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
            if (header.type == RSP_MATCH_FOUND) {
                char msg[128];
                recv(sock, msg, header.payload_size, 0);
                // msg format: "OpponentName Symbol"
                char opp_name[MAX_NAME_PLAYER];
                sscanf(msg, "%s %c", opp_name, &my_symbol);
                clear_screen();
                printf("Joined game against %s. You are %c\n", opp_name, my_symbol);
                handle_game();
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

void print_board() {
    printf("\n");
    printf(" %c | %c | %c \n", board[0][0], board[0][1], board[0][2]);
    printf("---|---|---\n");
    printf(" %c | %c | %c \n", board[1][0], board[1][1], board[1][2]);
    printf("---|---|---\n");
    printf(" %c | %c | %c \n", board[2][0], board[2][1], board[2][2]);
    printf("\n");
}

void handle_game() {
    memset(board, ' ', 9);
    int game_over = 0;

    printf("Waiting for game start...\n");

    while (!game_over) {
        PacketHeader header;
        int r = recv(sock, &header, sizeof(header), 0);
        if (r <= 0) break;

        if (header.type == RSP_MATCH_FOUND) {
            // Already handled in Join, but Host receives it here
             char msg[128];
             recv(sock, msg, header.payload_size, 0);
             printf("Match Found! Opponent: %s\n", msg);
        }
        else if (header.type == RSP_GAME_START) {
            char start_sym[2];
            recv(sock, start_sym, 2, 0);
            printf("Game Started! %c moves first.\n", start_sym[0]);
            print_board();
            if (start_sym[0] == my_symbol) {
                printf("Your turn! Enter cell (0-8): ");
                char move_buf[10];
                fgets(move_buf, sizeof(move_buf), stdin);
                MoveRequest req;
                req.cell_index = atoi(move_buf);
                send_request(CMD_MOVE, &req, sizeof(req));
                board[req.cell_index/3][req.cell_index%3] = my_symbol;
                clear_screen();
                print_board();
                printf("Waiting for opponent...\n");
            } else {
                printf("Waiting for opponent...\n");
            }
        }
        else if (header.type == RSP_OPPONENT_MOVE) {
            MoveRequest move;
            recv(sock, &move, sizeof(move), 0);
            char opp_sym = (my_symbol == 'X') ? 'O' : 'X';
            board[move.cell_index/3][move.cell_index%3] = opp_sym;
            clear_screen();
            print_board();
            printf("Your turn! Enter cell (0-8): ");
            fflush(stdout);
            
            fd_set fds;
            int moved = 0;
            while(!moved && !game_over) {
                 FD_ZERO(&fds);
                 FD_SET(sock, &fds);
                 FD_SET(STDIN_FILENO, &fds);

                 select(sock+1, &fds, NULL, NULL, NULL);
                 if (FD_ISSET(sock, &fds)) {
                     // Message from server (Likely Game Over)
                     // Break to outer loop to handle it
                     break; 
                 }
                 if (FD_ISSET(STDIN_FILENO, &fds)) {
                     char move_buf[10];
                     if (fgets(move_buf, sizeof(move_buf), stdin) != NULL) {
                         MoveRequest req;
                         req.cell_index = atoi(move_buf);
                         send_request(CMD_MOVE, &req, sizeof(req));
                         board[req.cell_index/3][req.cell_index%3] = my_symbol;
                         clear_screen();
                         print_board();
                         printf("Waiting for opponent...\n");
                         moved = 1;
                     }
                 }
            }
        }
        else if (header.type == RSP_GAME_OVER) {
            int result;
            recv(sock, &result, sizeof(int), 0);
            if (result == 1) printf("YOU WON!\n");
            else if (result == 2) printf("YOU LOST!\n");
            else printf("DRAW!\n");
            game_over = 1;
        }
        else if (header.type == RSP_ASK_PLAY_AGAIN) {
             printf("Play again? (1=Yes, 0=No): ");
             char buf[10];
             fgets(buf, sizeof(buf), stdin);
             int choice = atoi(buf);
             send(sock, &choice, sizeof(int), 0); // Send raw int as payload?
             // Protocol says CMD_PLAY_AGAIN payload is int choice?
             // My implementation of handle_play_again expects raw int recv?
             // Let's fix protocol consistency.
             // Server: recv(client_sd, &choice, sizeof(int), 0);
             // So I should send raw int? No, everything should be wrapped in PacketHeader ideally.
             // But my handle_play_again reads header FIRST.
             // So I MUST send Header + Payload.
             
             send_request(CMD_PLAY_AGAIN, &choice, sizeof(int));
             if (choice == 0) return; // Back to lobby
             // If 1, wait for logic...
        }
    }
}
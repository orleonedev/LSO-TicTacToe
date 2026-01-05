#include "thread_logic.h"
#include "../common/include/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>

// --- Helper Functions Prototypes ---
void handle_client_message(int client_sd, ServerState *state, PlayerNode *myself);
void handle_internal_message(PlayerNode *myself, ServerState *state);

// Handlers for specific commands
void handle_login(int client_sd, ServerState *state, PlayerNode **myself_ptr);
void handle_create_game(int client_sd, ServerState *state, PlayerNode *myself);
void handle_join_game(int client_sd, ServerState *state, PlayerNode *myself);
void handle_list_games(int client_sd, ServerState *state);
void handle_move(int client_sd, ServerState *state, PlayerNode *myself);
void handle_disconnect(ServerState *state, PlayerNode *myself);
void handle_play_again(int client_sd, ServerState *state, PlayerNode *myself);

// Utilities
void send_packet(int sd, MessageType type, const void *payload, int payload_size);
void send_error(int sd, const char *msg);
PlayerNode* create_player_node(const char* name, int sd);
void remove_player(ServerState *state, PlayerNode *player);
GameNode* find_game_by_id(ServerState *state, int id);
void cleanup_game(ServerState *state, int game_id);

// --- Main Thread Handler ---
void *client_thread_handler(void *arg) {
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    ServerState *state = args->server_state;
    int client_sd = args->client_sd;
    free(args);

    printf("Thread started for client SD: %d\n", client_sd);

    PlayerNode *myself = NULL;

    // 1. Registration Phase (Synchronous)
    // The client MUST send a LOGIN packet first.
    // We can use a small timeout or just block for simplicity as per requirements "Upon connection... sends username".
    
    handle_login(client_sd, state, &myself);

    if (!myself) {
        // Login failed or disconnected
        close(client_sd);
        pthread_exit(NULL);
    }

    // 2. Main Event Loop (Select)
    fd_set read_fds;
    int max_fd;
    int pipe_read_fd = myself->pipe_fd[0];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(client_sd, &read_fds);
        FD_SET(pipe_read_fd, &read_fds);

        max_fd = (client_sd > pipe_read_fd) ? client_sd : pipe_read_fd;

        // Monitoring
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
            break;
        }

        // Check Internal Messages (Pipe)
        if (FD_ISSET(pipe_read_fd, &read_fds)) {
            handle_internal_message(myself, state);
        }

        // Check Network Messages (Socket)
        if (FD_ISSET(client_sd, &read_fds)) {
            // Peek or Read header first? 
            // We'll read the header in handle_client_message
            // If read returns 0 (disconnect), we handle it.
            PacketHeader header;
            ssize_t bytes = recv(client_sd, &header, sizeof(header), MSG_PEEK);
            if (bytes <= 0) {
                printf("Client %s disconnected (socket closed).\n", myself->name);
                handle_disconnect(state, myself);
                break;
            }
            
            handle_client_message(client_sd, state, myself);
        }
    }

    // Cleanup ensures player is removed if loop breaks
    // handle_disconnect should handle list removal.
    close(client_sd); // Ensure closed
    pthread_exit(NULL);
}

// --- Implementation of Helpers ---

void send_packet(int sd, MessageType type, const void *payload, int payload_size) {
    PacketHeader header;
    header.type = type;
    header.payload_size = payload_size;

    if (send(sd, &header, sizeof(header), 0) < 0) {
        perror("send header");
        return;
    }
    if (payload_size > 0 && payload != NULL) {
        if (send(sd, payload, payload_size, 0) < 0) {
            perror("send payload");
        }
    }
}

void send_error(int sd, const char *msg) {
    send_packet(sd, RSP_ERROR, msg, strlen(msg) + 1);
}

void handle_login(int client_sd, ServerState *state, PlayerNode **myself_ptr) {
    PacketHeader header;
    if (recv(client_sd, &header, sizeof(header), 0) <= 0) return;

    if (header.type != CMD_LOGIN) {
        send_error(client_sd, "Expected LOGIN");
        return;
    }

    LoginRequest req;
    if (recv(client_sd, &req, header.payload_size, 0) <= 0) return;

    // Check Uniqueness
    pthread_mutex_lock(&state->player_mutex);
    PlayerNode *curr = state->player_head;
    while (curr) {
        if (strcmp(curr->name, req.name) == 0) {
            pthread_mutex_unlock(&state->player_mutex);
            send_error(client_sd, "Name taken");
            return;
        }
        curr = curr->next;
    }

    // Success: Create Node
    PlayerNode *new_node = create_player_node(req.name, client_sd);
    new_node->next = state->player_head;
    state->player_head = new_node;
    pthread_mutex_unlock(&state->player_mutex);

    *myself_ptr = new_node;
    send_packet(client_sd, RSP_OK, "Welcome", 8);
    printf("Player %s logged in.\n", req.name);
}

PlayerNode* create_player_node(const char* name, int sd) {
    PlayerNode *node = malloc(sizeof(PlayerNode));
    strncpy(node->name, name, MAX_NAME_PLAYER);
    node->client_sd = sd;
    node->status = IN_LOBBY;
    node->current_game_id = -1;
    if (pipe(node->pipe_fd) < 0) {
        perror("pipe creation failed");
    }
    return node;
}

void handle_client_message(int client_sd, ServerState *state, PlayerNode *myself) {
    PacketHeader header;
    if (recv(client_sd, &header, sizeof(header), 0) <= 0) {
        handle_disconnect(state, myself);
        pthread_exit(NULL); // Terminate thread
    }

    // Validate payload size safety (basic check)
    if (header.payload_size > 1024) {
         // Prevent buffer overflow attacks
         // consume bytes
         return; 
    }

    // Dispatch
    switch (header.type) {
        case CMD_LIST_GAMES:
            handle_list_games(client_sd, state);
            break;
        case CMD_CREATE_GAME:
            handle_create_game(client_sd, state, myself);
            break;
        case CMD_JOIN_GAME:
            handle_join_game(client_sd, state, myself);
            break;
        case CMD_MOVE:
            handle_move(client_sd, state, myself);
            break;
        case CMD_DISCONNECT:
            handle_disconnect(state, myself);
            pthread_exit(NULL);
            break;
        case CMD_PLAY_AGAIN:
            handle_play_again(client_sd, state, myself);
            break;
        default:
            printf("Unknown command from %s\n", myself->name);
            break;
    }
}

void handle_list_games(int client_sd, ServerState *state) {
    // We send a series of GameInfoDTOs, or a count first.
    // Let's send a custom packed response: [Count] [Game1] [Game2]...
    // Or simpler: Send RSP_GAME_LIST with a large payload.
    // For simplicity, let's send them one by one or just one large buffer.
    // Given the constraints, let's just send the count first (as payload of RSP_GAME_LIST) 
    // and then individual packets? No, spec says RSP_GAME_LIST.
    
    pthread_mutex_lock(&state->game_mutex);
    
    int count = 0;
    GameNode *curr = state->game_head;
    while(curr) { count++; curr = curr->next; }

    // Send Count first? Or construct a big buffer.
    // Let's assume max 20 games for buffer simplicity? 
    // Better: Send payload containing Count, then Loop sending data.
    // Protocol definition said "List of games (custom serialization)".
    
    // Let's send a RSP_GAME_LIST with payload = int count
    send_packet(client_sd, RSP_GAME_LIST, &count, sizeof(int));

    curr = state->game_head;
    while(curr) {
        GameInfoDTO dto;
        dto.id = curr->game_id;
        strncpy(dto.owner, curr->owner_name, MAX_NAME_PLAYER);
        strncpy(dto.opponent, curr->opponent_name, MAX_NAME_PLAYER);
        if (curr->status == WAITING) strcpy(dto.status, "WAITING");
        else if (curr->status == RUNNING) strcpy(dto.status, "RUNNING");
        else strcpy(dto.status, "FINISHED");

        send(client_sd, &dto, sizeof(GameInfoDTO), 0);
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&state->game_mutex);
}

void handle_create_game(int client_sd, ServerState *state, PlayerNode *myself) {
    CreateGameRequest req;
    recv(client_sd, &req, sizeof(req), 0);

    pthread_mutex_lock(&state->game_mutex);
    
    GameNode *new_game = malloc(sizeof(GameNode));
    new_game->game_id = state->next_game_id++;
    strncpy(new_game->owner_name, myself->name, MAX_NAME_PLAYER);
    new_game->owner_sd = myself->client_sd;
    new_game->owner_symbol = req.symbol; // 'X' or 'O'
    new_game->opponent_sd = -1;
    new_game->status = WAITING;
    memset(new_game->board, ' ', 9);
    new_game->next = state->game_head;
    state->game_head = new_game;
    
    myself->current_game_id = new_game->game_id;
    myself->symbol = req.symbol;
    myself->status = IN_GAME;

    pthread_mutex_unlock(&state->game_mutex);

    send_packet(client_sd, RSP_OK, "Game Created", 13);
}

void handle_join_game(int client_sd, ServerState *state, PlayerNode *myself) {
    JoinGameRequest req;
    recv(client_sd, &req, sizeof(req), 0);

    pthread_mutex_lock(&state->game_mutex);
    GameNode *curr = state->game_head;
    while (curr) {
        if (curr->game_id == req.game_id) {
            if (curr->status == WAITING) {
                // Join success
                curr->status = RUNNING;
                strncpy(curr->opponent_name, myself->name, MAX_NAME_PLAYER);
                curr->opponent_sd = myself->client_sd;
                
                // Assign symbol
                curr->opponent_symbol = (curr->owner_symbol == 'X') ? 'O' : 'X';
                myself->symbol = curr->opponent_symbol;
                myself->current_game_id = curr->game_id;
                myself->status = IN_GAME;
                
                // Determine who starts (e.g., 'X' always starts)
                char start_symbol = 'X';
                curr->current_turn_sd = (curr->owner_symbol == start_symbol) ? curr->owner_sd : curr->opponent_sd;

                // Notify Owner (Internal Message)
                pthread_mutex_lock(&state->player_mutex);
                PlayerNode *owner_node = state->player_head;
                while(owner_node) {
                    if (owner_node->client_sd == curr->owner_sd) {
                        InternalMessage msg;
                        msg.type = MSG_MATCH_FOUND;
                        strncpy(msg.payload, myself->name, MAX_NAME_PLAYER); 
                        write(owner_node->pipe_fd[1], &msg, sizeof(msg));
                        break;
                    }
                    owner_node = owner_node->next;
                }
                pthread_mutex_unlock(&state->player_mutex);

                // Notify Self (Network)
                // Payload: Opponent Name + Assigned Symbol
                // Let's repackage RSP_MATCH_FOUND payload to contain symbol
                char buf[128];
                snprintf(buf, sizeof(buf), "%s %c", curr->owner_name, myself->symbol);
                send_packet(client_sd, RSP_MATCH_FOUND, buf, strlen(buf)+1);

                // Send Start Notification
                char start_msg[2] = {start_symbol, '\0'};
                send_packet(client_sd, RSP_GAME_START, start_msg, 2);
                
                // Owner also needs RSP_GAME_START, handled in process_internal_message or sent here?
                // Ideally sent by the Owner's thread when it receives MSG_MATCH_FOUND.
                
                pthread_mutex_unlock(&state->game_mutex);
                return;
            } else {
                pthread_mutex_unlock(&state->game_mutex);
                send_error(client_sd, "Game full or running");
                return;
            }
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&state->game_mutex);
    send_error(client_sd, "Game not found");
}

void handle_internal_message(PlayerNode *myself, ServerState *state) {
    InternalMessage msg;
    read(myself->pipe_fd[0], &msg, sizeof(msg));

    switch (msg.type) {
        case MSG_MATCH_FOUND:
            // I am the owner, someone joined.
            send_packet(myself->client_sd, RSP_MATCH_FOUND, msg.payload, strlen(msg.payload)+1);
            // msg.payload contains opponent name.
            // Need to tell client who starts.
            // We know 'X' always starts.
            send_packet(myself->client_sd, RSP_GAME_START, "X", 2); 
            break;
            
        case MSG_OPPONENT_MOVE:
            // msg.data contains the move index (0-8)
            {
                MoveRequest move;
                move.cell_index = msg.data;
                send_packet(myself->client_sd, RSP_OPPONENT_MOVE, &move, sizeof(move));
                
                // Check if Game Over (the move logic updates state, but here we just forward)
                // Actually, the sender of this message should have checked win condition?
                // Or we check it shared?
                // The requirements say "Server checks for win conditions".
                // Usually the thread processing the MOVE (the active player) checks the win.
                // If Win/Draw, it sends MSG_GAME_OVER to both (or RSP to self and MSG to opponent).
            }
            break;
            
        case MSG_GAME_OVER:
            // msg.data = result (0=Draw, 1=Win, 2=Loss) - relative to receiver?
            // Let's standardise: 1=Winner, 2=Loser.
            {
                int res = msg.data;
                send_packet(myself->client_sd, RSP_GAME_OVER, &res, sizeof(int));
                
                // Prompt for restart
                send_packet(myself->client_sd, RSP_ASK_PLAY_AGAIN, NULL, 0);
            }
            break;
            
        case MSG_OPPONENT_QUIT:
            send_packet(myself->client_sd, RSP_ERROR, "Opponent disconnected. You win!", 30);
             // handle win by default logic
            myself->status = IN_LOBBY;
            myself->current_game_id = -1;
            break;
            
default: 
            break;
    }
}

// Check win helper
int check_win(char board[3][3]) {
    // Rows
    for(int i=0; i<3; i++) {
        if(board[i][0] != ' ' && board[i][0] == board[i][1] && board[i][1] == board[i][2]) return 1;
    }
    // Cols
    for(int i=0; i<3; i++) {
        if(board[0][i] != ' ' && board[0][i] == board[1][i] && board[1][i] == board[2][i]) return 1;
    }
    // Diagonals
    if(board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2]) return 1;
    if(board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0]) return 1;
    
    return 0;
}

int check_draw(char board[3][3]) {
    for(int i=0; i<3; i++)
        for(int j=0; j<3; j++)
            if(board[i][j] == ' ') return 0;
    return 1;
}


void handle_move(int client_sd, ServerState *state, PlayerNode *myself) {
    MoveRequest req;
    recv(client_sd, &req, sizeof(req), 0);

    pthread_mutex_lock(&state->game_mutex);
    GameNode *game = find_game_by_id(state, myself->current_game_id);
    if (!game || game->status != RUNNING) {
        pthread_mutex_unlock(&state->game_mutex);
        send_error(client_sd, "No active game");
        return;
    }

    // Check Turn
    if (game->current_turn_sd != myself->client_sd) {
        pthread_mutex_unlock(&state->game_mutex);
        send_error(client_sd, "Not your turn");
        return;
    }

    // Update Board
    int row = req.cell_index / 3;
    int col = req.cell_index % 3;
    if (game->board[row][col] != ' ') {
         pthread_mutex_unlock(&state->game_mutex);
         send_error(client_sd, "Invalid move");
         return;
    }
    game->board[row][col] = myself->symbol;

    // Notify Opponent (Internal Message)
    int opponent_sd = (myself->client_sd == game->owner_sd) ? game->opponent_sd : game->owner_sd;
    
    // Find Opponent Node to get pipe
    pthread_mutex_lock(&state->player_mutex);
    PlayerNode *opp_node = state->player_head;
    while(opp_node) {
        if (opp_node->client_sd == opponent_sd) break;
        opp_node = opp_node->next;
    }
    
    if (opp_node) {
        InternalMessage msg;
        msg.type = MSG_OPPONENT_MOVE;
        msg.data = req.cell_index;
        write(opp_node->pipe_fd[1], &msg, sizeof(msg));
    }
    pthread_mutex_unlock(&state->player_mutex);

    // Check Win/Draw
    int win = check_win(game->board);
    int draw = check_draw(game->board);

    if (win) {
        // I won
        game->status = FINISHED;
        int my_res = 1; // Win
        int opp_res = 2; // Loss
        send_packet(client_sd, RSP_GAME_OVER, &my_res, sizeof(int));
        send_packet(client_sd, RSP_ASK_PLAY_AGAIN, NULL, 0);

        if (opp_node) {
            InternalMessage msg;
            msg.type = MSG_GAME_OVER;
            msg.data = opp_res;
            write(opp_node->pipe_fd[1], &msg, sizeof(msg));
        }
    } else if (draw) {
        game->status = FINISHED;
        int res = 0; // Draw
        send_packet(client_sd, RSP_GAME_OVER, &res, sizeof(int));
        send_packet(client_sd, RSP_ASK_PLAY_AGAIN, NULL, 0);

        if (opp_node) {
            InternalMessage msg;
            msg.type = MSG_GAME_OVER;
            msg.data = 0;
            write(opp_node->pipe_fd[1], &msg, sizeof(msg));
        }
    } else {
        // Toggle Turn
        game->current_turn_sd = opponent_sd;
    }

    pthread_mutex_unlock(&state->game_mutex);
}

void handle_disconnect(ServerState *state, PlayerNode *myself) {
    if (!myself) return;
    
    pthread_mutex_lock(&state->player_mutex);
    // Remove from list
    PlayerNode **curr = &state->player_head;
    while (*curr) {
        if (*curr == myself) {
            *curr = myself->next;
            break;
        }
        curr = &(*curr)->next;
    }
    pthread_mutex_unlock(&state->player_mutex);

    // Handle Active Game cleanup
    if (myself->status == IN_GAME) {
        // Notify opponent...
        // This requires finding the game and the opponent.
        // Simplified: The opponent will eventually detect closed socket or we should notify via pipe if possible.
        // We'll leave this for robustness improvements.
    }
    
    close(myself->pipe_fd[0]);
    close(myself->pipe_fd[1]);
    free(myself);
}

void handle_play_again(int client_sd, ServerState *state, PlayerNode *myself) {
    // Read choice (1=Yes, 0=No)
    // If Yes -> Logic to restart or host new.
    // If No -> Lobby.
    // Implementation of specific requirement: "Winner becomes Host", "Loser -> Lobby", "Draw -> Ask both".
    // This requires knowing the context (was I winner/loser?). 
    // For now, let's just reset to Lobby if No.
    
    int choice;
    recv(client_sd, &choice, sizeof(int), 0);
    
    if (choice == 1) {
        // Host new game logic...
        // Reuse handle_create_game logic or similar.
    } else {
        myself->status = IN_LOBBY;
        myself->current_game_id = -1;
    }
}

GameNode* find_game_by_id(ServerState *state, int id) {
    GameNode *curr = state->game_head;
    while(curr) {
        if (curr->game_id == id) return curr;
        curr = curr->next;
    }
    return NULL;
}
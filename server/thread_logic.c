#include "thread_logic.h"
#include "../common/include/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>

void handle_client_message(int client_sd, ServerState *state, PlayerNode *myself);
void handle_internal_message(PlayerNode *myself, ServerState *state);

void handle_login(int client_sd, ServerState *state, PlayerNode **myself_ptr);
void handle_create_game(int client_sd, ServerState *state, PlayerNode *myself);
void handle_join_game(int client_sd, ServerState *state, PlayerNode *myself);
void handle_list_games(int client_sd, ServerState *state);
void handle_move(int client_sd, ServerState *state, PlayerNode *myself);
void handle_disconnect(ServerState *state, PlayerNode *myself);
void handle_play_again(int client_sd, ServerState *state, PlayerNode *myself);
void handle_host_decision(int client_sd, ServerState *state, PlayerNode *myself);
void handle_cancel_join(int client_sd, ServerState *state, PlayerNode *myself);
void handle_stop_hosting(int client_sd, ServerState *state, PlayerNode *myself);

void send_packet(int sd, MessageType type, const void *payload, int payload_size);
void send_error(int sd, const char *msg);
PlayerNode* create_player_node(const char* name, int sd);
void remove_player(ServerState *state, PlayerNode *player);
GameNode* find_game_by_id(ServerState *state, int id);
void cleanup_game(ServerState *state, int game_id);
void broadcast_lobby_update(ServerState *state, PlayerNode *exclude_player);
void process_next_challenger(ServerState *state, GameNode *game);
void handle_create_game_fallback(int client_sd, ServerState *state, PlayerNode *myself);
int check_win(char board[3][3]);
int check_draw(char board[3][3]);

/*
 * Entry point for the client handling thread.
 * Manages the client lifecycle: login, main loop, and cleanup.
 */
void *client_thread_handler(void *arg) {
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    ServerState *state = args->server_state;
    int client_sd = args->client_sd;
    free(args);

    printf("[Thread] Started for client SD: %d\n", client_sd);

    PlayerNode *myself = NULL;

    handle_login(client_sd, state, &myself);

    if (!myself) {
        printf("[Thread] Login failed or disconnected for SD: %d\n", client_sd);
        close(client_sd);
        pthread_exit(NULL);
    }

    fd_set read_fds;
    int max_fd;
    int pipe_read_fd = myself->pipe_fd[0];

    printf("[Loop] Entering main loop for player: %s\n", myself->name);

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(client_sd, &read_fds);
        FD_SET(pipe_read_fd, &read_fds);

        max_fd = (client_sd > pipe_read_fd) ? client_sd : pipe_read_fd;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
            break;
        }

        if (FD_ISSET(pipe_read_fd, &read_fds)) {
            handle_internal_message(myself, state);
        }

        if (FD_ISSET(client_sd, &read_fds)) {
            PacketHeader header;
            ssize_t bytes = recv(client_sd, &header, sizeof(header), MSG_PEEK);
            if (bytes <= 0) {
                printf("[Disconnect] Client %s disconnected (socket closed).\n", myself->name);
                handle_disconnect(state, myself);
                break;
            }
            
            handle_client_message(client_sd, state, myself);
        }
    }

    close(client_sd); 
    pthread_exit(NULL);
}

/*
 * Sends a structured packet to the specified socket.
 */
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

/*
 * Sends an error response packet to the client.
 */
void send_error(int sd, const char *msg) {
    printf("[Error] Sending error to SD %d: %s\n", sd, msg);
    send_packet(sd, RSP_ERROR, msg, strlen(msg) + 1);
}

/*
 * Handles the login process for a client.
 * Verifies username uniqueness and registers the player in the server state.
 */
void handle_login(int client_sd, ServerState *state, PlayerNode **myself_ptr) {
    PacketHeader header;
    if (recv(client_sd, &header, sizeof(header), 0) <= 0) return;

    if (header.type != CMD_LOGIN) {
        send_error(client_sd, "Expected LOGIN");
        return;
    }

    LoginRequest req;
    if (recv(client_sd, &req, header.payload_size, 0) <= 0) return;

    printf("[Login] Request from SD %d: Name='%s'\n", client_sd, req.name);

    pthread_mutex_lock(&state->player_mutex);
    PlayerNode *curr = state->player_head;
    while (curr) {
        if (strcmp(curr->name, req.name) == 0) {
            pthread_mutex_unlock(&state->player_mutex);
            printf("[Login] Failed: Name '%s' already taken.\n", req.name);
            send_error(client_sd, "Name taken");
            return;
        }
        curr = curr->next;
    }

    PlayerNode *new_node = create_player_node(req.name, client_sd);
    new_node->next = state->player_head;
    state->player_head = new_node;
    pthread_mutex_unlock(&state->player_mutex);

    *myself_ptr = new_node;
    send_packet(client_sd, RSP_OK, "Welcome", 8);
    printf("[Login] Success: Player '%s' logged in.\n", req.name);
}

/*
 * Allocates and initializes a new PlayerNode.
 */
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

/*
 * Processes messages received from the client socket.
 */
void handle_client_message(int client_sd, ServerState *state, PlayerNode *myself) {
    PacketHeader header;
    if (recv(client_sd, &header, sizeof(header), 0) <= 0) {
        handle_disconnect(state, myself);
        pthread_exit(NULL);
    }

    if (header.payload_size > 1024) {
         return; 
    }

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
        case CMD_CANCEL_JOIN:
            handle_cancel_join(client_sd, state, myself);
            break;
        case CMD_HOST_DECISION:
            handle_host_decision(client_sd, state, myself);
            break;
        case CMD_STOP_HOSTING:
            handle_stop_hosting(client_sd, state, myself);
            break;
        case CMD_MOVE:
            handle_move(client_sd, state, myself);
            break;
        case CMD_DISCONNECT:
            printf("[Action] Player '%s' requested disconnect.\n", myself->name);
            handle_disconnect(state, myself);
            pthread_exit(NULL);
            break;
        case CMD_PLAY_AGAIN:
            handle_play_again(client_sd, state, myself);
            break;
        default:
            printf("[Warning] Unknown command type %d from %s\n", header.type, myself->name);
            break;
    }
}

/*
 * Handles a host stopping the hosting process.
 * Cleans up the game and notifies queued challengers.
 */
void handle_stop_hosting(int client_sd, ServerState *state, PlayerNode *myself) {
    printf("[Game] Host '%s' requested to stop hosting.\n", myself->name);
    
    pthread_mutex_lock(&state->game_mutex);
    GameNode *game = find_game_by_id(state, myself->current_game_id);
    
    if (game && game->owner_sd == myself->client_sd && game->status == WAITING) {
        
        ChallengerNode *curr = game->challenger_queue_head;
        while(curr) {
            pthread_mutex_lock(&state->player_mutex);
            PlayerNode *p = state->player_head;
            while(p) {
                if (p->client_sd == curr->client_sd) {
                    InternalMessage msg;
                    msg.type = MSG_REQUEST_RESULT;
                    msg.data = 0; // Rejected (effectively kicked)
                    write(p->pipe_fd[1], &msg, sizeof(msg));
                    break;
                }
                p = p->next;
            }
            pthread_mutex_unlock(&state->player_mutex);
            
            ChallengerNode *tmp = curr;
            curr = curr->next;
            free(tmp);
        }
        
        GameNode **g_curr = &state->game_head;
        while(*g_curr) {
            if (*g_curr == game) {
                *g_curr = game->next;
                free(game);
                break;
            }
            g_curr = &(*g_curr)->next;
        }
        
        myself->status = IN_LOBBY;
        myself->current_game_id = -1;
        
        send_packet(client_sd, RSP_OK, "Lobby Closed", 13);
        
        pthread_mutex_unlock(&state->game_mutex);
        broadcast_lobby_update(state, myself);
        
    } else {
        pthread_mutex_unlock(&state->game_mutex);
        send_error(client_sd, "Cannot stop hosting (Game not found or running)");
    }
}

/*
 * Handles a challenger cancelling their join request.
 * Notifies the host if the challenger was the current candidate.
 */
void handle_cancel_join(int client_sd, ServerState *state, PlayerNode *myself) {
    pthread_mutex_lock(&state->game_mutex);
    GameNode *game = find_game_by_id(state, myself->current_game_id);
    if (game && game->challenger_queue_head) {
        ChallengerNode *curr = game->challenger_queue_head;
        ChallengerNode *prev = NULL;
        while (curr) {
            if (curr->client_sd == client_sd) {
                if (curr == game->challenger_queue_head) {
                     pthread_mutex_lock(&state->player_mutex);
                     PlayerNode *host = state->player_head;
                     while(host) {
                         if (host->client_sd == game->owner_sd) {
                             InternalMessage msg;
                             msg.type = MSG_JOIN_CANCELLED;
                             snprintf(msg.payload, sizeof(msg.payload), "%s", myself->name);
                             write(host->pipe_fd[1], &msg, sizeof(msg));
                             break;
                         }
                         host = host->next;
                     }
                     pthread_mutex_unlock(&state->player_mutex);
                }

                if (prev) prev->next = curr->next;
                else game->challenger_queue_head = curr->next;
                
                if (curr == game->challenger_queue_tail) game->challenger_queue_tail = prev;
                
                free(curr);
                printf("[Game] Player %s cancelled join request for Game %d.\n", myself->name, game->game_id);
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    pthread_mutex_unlock(&state->game_mutex);
    
    myself->current_game_id = -1;
    send_packet(client_sd, RSP_OK, "Cancelled", 10);
}

/*
 * Broadcasts a lobby update message to all players in the lobby.
 */
void broadcast_lobby_update(ServerState *state, PlayerNode *exclude_player) {
    pthread_mutex_lock(&state->player_mutex);
    PlayerNode *curr = state->player_head;
    InternalMessage msg;
    msg.type = MSG_LOBBY_UPDATE;
    
    while(curr) {
        if (curr != exclude_player && curr->status == IN_LOBBY) {
            write(curr->pipe_fd[1], &msg, sizeof(msg));
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&state->player_mutex);
}

/*
 * Sends the list of active games to the client.
 */
void handle_list_games(int client_sd, ServerState *state) {
    pthread_mutex_lock(&state->game_mutex);
    
    int count = 0;
    GameNode *curr = state->game_head;
    while(curr) { 
        if (curr->status != FINISHED) {
            count++; 
        }
        curr = curr->next; 
    }

    send_packet(client_sd, RSP_GAME_LIST, &count, sizeof(int));

    curr = state->game_head;
    while(curr) {
        if (curr->status == FINISHED) {
             curr = curr->next;
             continue;
        }

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

/*
 * Creates a new game instance.
 */
void handle_create_game(int client_sd, ServerState *state, PlayerNode *myself) {
    CreateGameRequest req;
    recv(client_sd, &req, sizeof(req), 0);

    pthread_mutex_lock(&state->game_mutex);
    
    GameNode *new_game = malloc(sizeof(GameNode));
    new_game->game_id = state->next_game_id++;
    strncpy(new_game->owner_name, myself->name, MAX_NAME_PLAYER);
    new_game->owner_sd = myself->client_sd;
    new_game->owner_symbol = req.symbol; 
    new_game->opponent_sd = -1;
    new_game->status = WAITING;
    new_game->challenger_queue_head = NULL;
    new_game->challenger_queue_tail = NULL;
    memset(new_game->board, ' ', 9);
    new_game->next = state->game_head;
    state->game_head = new_game;
    
    myself->current_game_id = new_game->game_id;
    myself->symbol = req.symbol;
    myself->status = IN_GAME; 

    printf("[Game] Created Game ID %d by '%s' (Symbol: %c)\n", new_game->game_id, myself->name, req.symbol);

    pthread_mutex_unlock(&state->game_mutex);

    send_packet(client_sd, RSP_OK, "Game Created", 13);
    
    broadcast_lobby_update(state, myself);
}

/*
 * Handles a request to join an existing game.
 * Enqueues the player as a challenger.
 */
void handle_join_game(int client_sd, ServerState *state, PlayerNode *myself) {
    JoinGameRequest req;
    recv(client_sd, &req, sizeof(req), 0);

    printf("[Game] Player '%s' requesting to join Game ID %d\n", myself->name, req.game_id);

    pthread_mutex_lock(&state->game_mutex);
    GameNode *game = find_game_by_id(state, req.game_id);
    
    if (game && game->status == WAITING) {
        ChallengerNode *node = malloc(sizeof(ChallengerNode));
        node->client_sd = myself->client_sd;
        strncpy(node->name, myself->name, MAX_NAME_PLAYER);
        node->next = NULL;

        if (game->challenger_queue_tail) {
            game->challenger_queue_tail->next = node;
            game->challenger_queue_tail = node;
        } else {
            game->challenger_queue_head = node;
            game->challenger_queue_tail = node;
        }

        if (game->challenger_queue_head == node) {
             process_next_challenger(state, game);
        }
        
        send_packet(client_sd, RSP_OK, "Request Queued", 15);
        myself->current_game_id = game->game_id; 
        
    } else {
        send_error(client_sd, "Game not found or running");
    }
    
    pthread_mutex_unlock(&state->game_mutex);
}

/*
 * Notifies the game host of the next challenger in the queue.
 */
void process_next_challenger(ServerState *state, GameNode *game) {
    if (!game->challenger_queue_head) return;

    ChallengerNode *candidate = game->challenger_queue_head;
    
    pthread_mutex_lock(&state->player_mutex);
    PlayerNode *host_node = state->player_head;
    while(host_node) {
        if (host_node->client_sd == game->owner_sd) {
            InternalMessage msg;
            msg.type = MSG_JOIN_REQUEST;
            strncpy(msg.payload, candidate->name, MAX_NAME_PLAYER); 
            write(host_node->pipe_fd[1], &msg, sizeof(msg));
            break;
        }
        host_node = host_node->next;
    }
    pthread_mutex_unlock(&state->player_mutex);
}

/*
 * Handles the host's decision to accept or reject a challenger.
 */
void handle_host_decision(int client_sd, ServerState *state, PlayerNode *myself) {
    int accepted; 
    recv(client_sd, &accepted, sizeof(int), 0);
    
    pthread_mutex_lock(&state->game_mutex);
    GameNode *game = find_game_by_id(state, myself->current_game_id);
    
    if (!game || !game->challenger_queue_head) {
        pthread_mutex_unlock(&state->game_mutex);
        return;
    }

    ChallengerNode *candidate = game->challenger_queue_head;
    
    pthread_mutex_lock(&state->player_mutex);
    PlayerNode *candidate_node = state->player_head;
    while(candidate_node) {
        if (candidate_node->client_sd == candidate->client_sd) break;
        candidate_node = candidate_node->next;
    }
    
    if (accepted) {
        printf("[Game] Host Accepted candidate %s\n", candidate->name);
        
        game->status = RUNNING;
        strncpy(game->opponent_name, candidate->name, MAX_NAME_PLAYER);
        game->opponent_sd = candidate->client_sd;
        game->opponent_symbol = (game->owner_symbol == 'X') ? 'O' : 'X';
        
        char start_symbol = 'X';
        game->current_turn_sd = (game->owner_symbol == start_symbol) ? game->owner_sd : game->opponent_sd;
        
        if (candidate_node) {
            candidate_node->current_game_id = game->game_id;
            candidate_node->status = IN_GAME;
            candidate_node->symbol = game->opponent_symbol;

            InternalMessage msg;
            msg.type = MSG_REQUEST_RESULT;
            msg.data = 1; // Accepted
            snprintf(msg.payload, sizeof(msg.payload), "%s %c", game->owner_name, candidate_node->symbol);
            write(candidate_node->pipe_fd[1], &msg, sizeof(msg));
        }

        char start_msg[2] = {start_symbol, '\0'};
        send_packet(client_sd, RSP_GAME_START, start_msg, 2);
        
        ChallengerNode *curr = game->challenger_queue_head->next; 
        while(curr) {
            PlayerNode *p = state->player_head;
            while(p) {
                if (p->client_sd == curr->client_sd) {
                    InternalMessage msg;
                    msg.type = MSG_REQUEST_RESULT;
                    msg.data = 0; // Rejected
                    write(p->pipe_fd[1], &msg, sizeof(msg));
                    break;
                }
                p = p->next;
            }
            ChallengerNode *tmp = curr;
            curr = curr->next;
            free(tmp);
        }
        
        free(game->challenger_queue_head);
        game->challenger_queue_head = NULL;
        game->challenger_queue_tail = NULL;
        
        pthread_mutex_unlock(&state->player_mutex);
        broadcast_lobby_update(state, NULL);

    } else {
        printf("[Game] Host Rejected candidate %s\n", candidate->name);
        
        if (candidate_node) {
            InternalMessage msg;
            msg.type = MSG_REQUEST_RESULT;
            msg.data = 0; // Rejected
            write(candidate_node->pipe_fd[1], &msg, sizeof(msg));
        }
        
        game->challenger_queue_head = candidate->next;
        if (!game->challenger_queue_head) game->challenger_queue_tail = NULL;
        free(candidate);
        
        pthread_mutex_unlock(&state->player_mutex);
        
        process_next_challenger(state, game);
    }

    pthread_mutex_unlock(&state->game_mutex);
}

/*
 * Processes internal messages from the thread's pipe.
 */
void handle_internal_message(PlayerNode *myself, ServerState *state) {
    InternalMessage msg;
    read(myself->pipe_fd[0], &msg, sizeof(msg));

    printf("[Pipe] Player '%s' received internal msg type %d\n", myself->name, msg.type);

    switch (msg.type) {
        case MSG_LOBBY_UPDATE:
            send_packet(myself->client_sd, RSP_LOBBY_UPDATE, NULL, 0);
            break;

        case MSG_JOIN_REQUEST:
             send_packet(myself->client_sd, RSP_JOIN_REQUEST, msg.payload, strlen(msg.payload)+1);
             break;

        case MSG_JOIN_CANCELLED:
             send_packet(myself->client_sd, RSP_JOIN_CANCELLED, msg.payload, strlen(msg.payload)+1);
             break;

        case MSG_REQUEST_RESULT:
             {
                 int accepted = msg.data;
                 if (accepted) {
                    send_packet(myself->client_sd, RSP_MATCH_FOUND, msg.payload, strlen(msg.payload)+1);
                    send_packet(myself->client_sd, RSP_GAME_START, "X", 2); 
                 } else {
                     send_packet(myself->client_sd, RSP_REQUEST_RESULT, &accepted, sizeof(int));
                 }
             }
             break;
            
        case MSG_OPPONENT_MOVE:
            {
                MoveRequest move;
                move.cell_index = msg.data;
                send_packet(myself->client_sd, RSP_OPPONENT_MOVE, &move, sizeof(move));
            }
            break;
            
        case MSG_GAME_OVER:
            {
                int res = msg.data;
                send_packet(myself->client_sd, RSP_GAME_OVER, &res, sizeof(int));
                
                if (res == 0 || res == 1) { // Draw or Win
                    send_packet(myself->client_sd, RSP_ASK_PLAY_AGAIN, NULL, 0);
                } else if (res == 2) {
                    myself->status = IN_LOBBY;
                    myself->current_game_id = -1;
                    broadcast_lobby_update(state, myself);
                }
            }
            break;
            
        case MSG_OPPONENT_QUIT: {
            printf("[Pipe] Player %s wins due to opponent disconnect.\n", myself->name);
            int res = 1; // Win
            send_packet(myself->client_sd, RSP_GAME_OVER, &res, sizeof(int));
            send_packet(myself->client_sd, RSP_ASK_PLAY_AGAIN, NULL, 0);
            break;
        }
            
default: 
            break;
    }
}

/*
 * Checks if there is a winning condition on the board.
 */
int check_win(char board[3][3]) {
    for(int i=0; i<3; i++) {
        if(board[i][0] != ' ' && board[i][0] == board[i][1] && board[i][1] == board[i][2]) return 1;
    }
    for(int i=0; i<3; i++) {
        if(board[0][i] != ' ' && board[0][i] == board[1][i] && board[1][i] == board[2][i]) return 1;
    }
    if(board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2]) return 1;
    if(board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0]) return 1;
    return 0;
}

/*
 * Checks if the board is full (draw condition).
 */
int check_draw(char board[3][3]) {
    for(int i=0; i<3; i++)
        for(int j=0; j<3; j++)
            if(board[i][j] == ' ') return 0;
    return 1;
}

/*
 * Handles a move request from a player.
 * Updates board state, checks for win/draw, and notifies opponent.
 */
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

    if (game->current_turn_sd != myself->client_sd) {
        pthread_mutex_unlock(&state->game_mutex);
        send_error(client_sd, "Not your turn");
        return;
    }

    int row = req.cell_index / 3;
    int col = req.cell_index % 3;
    if (game->board[row][col] != ' ') {
         pthread_mutex_unlock(&state->game_mutex);
         send_packet(client_sd, RSP_INVALID_MOVE, NULL, 0); 
         return;
    }
    game->board[row][col] = myself->symbol;

    int opponent_sd = (myself->client_sd == game->owner_sd) ? game->opponent_sd : game->owner_sd;
    
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

    int win = check_win(game->board);
    int draw = check_draw(game->board);

    if (win) {
        game->status = FINISHED;
        int my_res = 1; 
        int opp_res = 2; 
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
        int res = 0; 
        send_packet(client_sd, RSP_GAME_OVER, &res, sizeof(int));
        send_packet(client_sd, RSP_ASK_PLAY_AGAIN, NULL, 0);

        if (opp_node) {
            InternalMessage msg;
            msg.type = MSG_GAME_OVER;
            msg.data = 0;
            write(opp_node->pipe_fd[1], &msg, sizeof(msg));
        }
    } else {
        game->current_turn_sd = opponent_sd;
    }

    pthread_mutex_unlock(&state->game_mutex);
}

/*
 * Handles client disconnection.
 * Cleans up player node and any active games or queues.
 */
void handle_disconnect(ServerState *state, PlayerNode *myself) {
    if (!myself) return; 
    
    printf("[Disconnect] Removing player '%s'\n", myself->name);

    pthread_mutex_lock(&state->player_mutex);
    PlayerNode **curr = &state->player_head;
    while (*curr) {
        if (*curr == myself) {
            *curr = myself->next;
            break;
        }
        curr = &(*curr)->next;
    }
    pthread_mutex_unlock(&state->player_mutex);

    pthread_mutex_lock(&state->game_mutex);
    GameNode **g_curr = &state->game_head;
    while(*g_curr) {
        GameNode *g = *g_curr;
        
        if (g->owner_sd == myself->client_sd) {
             ChallengerNode *qn = g->challenger_queue_head;
             while(qn) {
                 pthread_mutex_lock(&state->player_mutex);
                 PlayerNode *p = state->player_head;
                 while(p) {
                     if (p->client_sd == qn->client_sd) {
                         InternalMessage msg;
                         msg.type = MSG_REQUEST_RESULT;
                         msg.data = 0; 
                         write(p->pipe_fd[1], &msg, sizeof(msg));
                         break;
                     }
                     p = p->next;
                 }
                 pthread_mutex_unlock(&state->player_mutex);
                 
                 ChallengerNode *tmp = qn;
                 qn = qn->next;
                 free(tmp);
             }
             
             if (g->status == RUNNING) {
                 pthread_mutex_lock(&state->player_mutex);
                 PlayerNode *opp = state->player_head;
                 while(opp) {
                     if (opp->client_sd == g->opponent_sd) {
                         InternalMessage msg;
                         msg.type = MSG_OPPONENT_QUIT;
                         write(opp->pipe_fd[1], &msg, sizeof(msg));
                         break;
                     }
                     opp = opp->next;
                 }
                 pthread_mutex_unlock(&state->player_mutex);
             }
             
            *g_curr = g->next;
            free(g);
            break; 
        }
        else if (g->opponent_sd == myself->client_sd) {
             pthread_mutex_lock(&state->player_mutex);
             PlayerNode *owner = state->player_head;
             while(owner) {
                 if (owner->client_sd == g->owner_sd) {
                     InternalMessage msg;
                     msg.type = MSG_OPPONENT_QUIT;
                     write(owner->pipe_fd[1], &msg, sizeof(msg));
                     break;
                 }
                 owner = owner->next;
             }
             pthread_mutex_unlock(&state->player_mutex);
             g->status = FINISHED; 
             break;
        }
        
        g_curr = &(*g_curr)->next;
    }
    pthread_mutex_unlock(&state->game_mutex);

    close(myself->pipe_fd[0]);
    close(myself->pipe_fd[1]);
    free(myself);
    
    broadcast_lobby_update(state, NULL);
}

/*
 * Handles logic for playing a new game after a match finishes.
 */
void handle_play_again(int client_sd, ServerState *state, PlayerNode *myself) {
    int choice;
    recv(client_sd, &choice, sizeof(int), 0);
    
    pthread_mutex_lock(&state->game_mutex);
    GameNode *game = find_game_by_id(state, myself->current_game_id);
    
    if (!game) {
        pthread_mutex_unlock(&state->game_mutex);
        if (choice == 1) {
            handle_create_game_fallback(client_sd, state, myself); 
        } else {
            myself->status = IN_LOBBY;
            myself->current_game_id = -1;
        }
        return;
    }

    int is_owner = (game->owner_sd == myself->client_sd);
    int is_opponent = (game->opponent_sd == myself->client_sd);

    if (choice == 1) {
        char new_symbol;
        recv(client_sd, &new_symbol, sizeof(char), 0);
        
        if (is_owner) {
            printf("[Game] Owner '%s' recycling Game ID %d\n", myself->name, game->game_id);
            game->status = WAITING;
            game->owner_symbol = new_symbol;
            game->opponent_sd = -1; 
            memset(game->opponent_name, 0, MAX_NAME_PLAYER);
            memset(game->board, ' ', 9);
            
            myself->symbol = new_symbol;
            myself->status = IN_GAME;
            
        } else {
            if (is_opponent) game->opponent_sd = -1;
            
            if (game->owner_sd == -1 && game->opponent_sd == -1) {
                 GameNode **curr = &state->game_head;
                 while(*curr) {
                     if (*curr == game) {
                         *curr = game->next;
                         free(game);
                         break;
                     }
                     curr = &(*curr)->next;
                 }
            }

            GameNode *new_game = malloc(sizeof(GameNode));
            new_game->game_id = state->next_game_id++;
            strncpy(new_game->owner_name, myself->name, MAX_NAME_PLAYER);
            new_game->owner_sd = myself->client_sd;
            new_game->owner_symbol = new_symbol;
            new_game->opponent_sd = -1;
            new_game->status = WAITING;
            new_game->challenger_queue_head = NULL;
            new_game->challenger_queue_tail = NULL;
            memset(new_game->board, ' ', 9);
            new_game->next = state->game_head;
            state->game_head = new_game;
            
            myself->current_game_id = new_game->game_id;
            myself->symbol = new_symbol;
            myself->status = IN_GAME;
        }

        pthread_mutex_unlock(&state->game_mutex);
        send_packet(client_sd, RSP_OK, "Game Ready", 11);
        broadcast_lobby_update(state, myself);

    } else {
        if (is_owner) game->owner_sd = -1;
        if (is_opponent) game->opponent_sd = -1;
        
        if (game->owner_sd == -1 && game->opponent_sd == -1) {
             GameNode **curr = &state->game_head;
             while(*curr) {
                 if (*curr == game) {
                     *curr = game->next;
                     free(game);
                     break;
                 }
                 curr = &(*curr)->next;
             }
        }
        
        pthread_mutex_unlock(&state->game_mutex);
        
        myself->status = IN_LOBBY;
        myself->current_game_id = -1;
        broadcast_lobby_update(state, myself);
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

void handle_create_game_fallback(int client_sd, ServerState *state, PlayerNode *myself) {
    char new_symbol;
    recv(client_sd, &new_symbol, sizeof(char), 0);
    
    pthread_mutex_lock(&state->game_mutex);
    GameNode *new_game = malloc(sizeof(GameNode));
    new_game->game_id = state->next_game_id++;
    strncpy(new_game->owner_name, myself->name, MAX_NAME_PLAYER);
    new_game->owner_sd = myself->client_sd;
    new_game->owner_symbol = new_symbol;
    new_game->opponent_sd = -1;
    new_game->status = WAITING;
    new_game->challenger_queue_head = NULL;
    new_game->challenger_queue_tail = NULL;
    memset(new_game->board, ' ', 9);
    new_game->next = state->game_head;
    state->game_head = new_game;
    
    myself->current_game_id = new_game->game_id;
    myself->symbol = new_symbol;
    myself->status = IN_GAME;
    pthread_mutex_unlock(&state->game_mutex);
    
    send_packet(client_sd, RSP_OK, "Game Created", 13);
    broadcast_lobby_update(state, myself);
}

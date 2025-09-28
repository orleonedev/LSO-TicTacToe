#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>

// Represents a player in the game
typedef struct {
    int socket;
    char username[50];
} Player;

// Represents a game match
typedef struct {
    Player* player1;
    Player* player2;
    char board[3][3];
    int current_turn;
} Match;

// Represents the shared state of the server
typedef struct {
    Player** players;
    int player_count;
    Match** matches;
    int match_count;
    pthread_mutex_t players_mutex;
    pthread_mutex_t matches_mutex;
} ServerState;

#endif // COMMON_H

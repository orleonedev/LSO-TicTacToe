#ifndef DATASTRUCTURES_H
#define DATASTRUCTURES_H

#include <pthread.h>
#include <stdbool.h>

#define MAX_NAME_PLAYER 20
#define MAX_MESSAGE_SIZE 128

// Stati del giocatore
typedef enum {
    IN_LOBBY,
    IN_GAME
} PlayerStatus;

// Stati della partita
typedef enum {
    WAITING,
    RUNNING,
    FINISHED
} GameStatus;

// Struttura per un nodo giocatore
typedef struct PlayerNode {
    char name[MAX_NAME_PLAYER];
    int client_sd; // Socket descriptor del client
    PlayerStatus status;
    pthread_t thread_id;
    struct PlayerNode *next;
} PlayerNode;

// Struttura per un nodo partita
typedef struct GameNode {
    char owner_name[MAX_NAME_PLAYER];
    int owner_sd;
    char opponent_name[MAX_NAME_PLAYER];
    int opponent_sd;
    GameStatus status;
    struct GameNode *next;
} GameNode;

// Struttura che incapsula lo stato condiviso del server
typedef struct {
    PlayerNode *player_head;
    GameNode *game_head;
    pthread_mutex_t player_mutex;
    pthread_mutex_t game_mutex;
} ServerState;

#endif // DATASTRUCTURES_H
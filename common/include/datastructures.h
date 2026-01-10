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

// --- Internal Server Communication (Message Queue) ---
typedef enum {
    MSG_MATCH_FOUND,      // Payload: Opponent info
    MSG_OPPONENT_MOVE,    // Payload: Move coordinates
    MSG_GAME_OVER,        // Payload: Result (Win/Loss/Draw)
    MSG_OPPONENT_QUIT,    // Opponent disconnected
    MSG_SHUTDOWN,         // Server is stopping
    MSG_ASK_NEW_HOST,     // Ask player if they want to stay as host
    MSG_LOBBY_UPDATE,     // Inform lobby players of new game
    MSG_JOIN_REQUEST,     // Ask Host to accept/reject
    MSG_REQUEST_RESULT    // Tell Challenger if accepted/rejected
} InternalMsgType;

typedef struct {
    InternalMsgType type;
    int data;            // e.g., move index or game ID, or result code
    char payload[128];   // Text message or extra info
} InternalMessage;
// -----------------------------------------------------

// Struttura per un nodo giocatore
typedef struct PlayerNode {
    char name[MAX_NAME_PLAYER];
    int client_sd; // Socket descriptor del client
    PlayerStatus status;
    
    // Internal Communication Pipe
    // pipe_fd[0] = read (monitored by select), pipe_fd[1] = write (used by other threads)
    int pipe_fd[2]; 
    
    // Game context
    int current_game_id;
    char symbol; // 'X' or 'O'

    pthread_t thread_id;
    struct PlayerNode *next;
} PlayerNode;

// Struttura per un nodo partita
typedef struct GameNode {
    int game_id; // Unique ID

    char owner_name[MAX_NAME_PLAYER];
    int owner_sd; // Socket descriptor (key to find player node if needed)
    char owner_symbol;

    char opponent_name[MAX_NAME_PLAYER];
    int opponent_sd;
    char opponent_symbol;

    // Queue for players waiting to join this game
    // Simple linked list of names or SDs? Better store simplified info or PlayerNode pointers?
    // Storing PlayerNode* is risky if they disconnect.
    // Let's store a separate Queue Node structure.
    struct ChallengerNode *challenger_queue_head;
    struct ChallengerNode *challenger_queue_tail;

    char board[3][3]; // ' ', 'X', 'O'
    int current_turn_sd; // socket descriptor of the player who needs to move

    GameStatus status;
    struct GameNode *next;
} GameNode;

typedef struct ChallengerNode {
    int client_sd;
    char name[MAX_NAME_PLAYER];
    struct ChallengerNode *next;
} ChallengerNode;

// Struttura che incapsula lo stato condiviso del server
typedef struct {
    PlayerNode *player_head;
    GameNode *game_head;
    
    pthread_mutex_t player_mutex;
    pthread_mutex_t game_mutex;

    int next_game_id; // Simple counter (protected by game_mutex)
} ServerState;

#endif // DATASTRUCTURES_H
#ifndef DATASTRUCTURES_H
#define DATASTRUCTURES_H

#include <pthread.h>
#include <stdbool.h>

#define MAX_NAME_PLAYER 20
#define MAX_MESSAGE_SIZE 128

/*
 * Represents the current status of a player.
 */
typedef enum {
    IN_LOBBY,
    IN_GAME
} PlayerStatus;

/*
 * Represents the current status of a game instance.
 */
typedef enum {
    WAITING,
    RUNNING,
    FINISHED
} GameStatus;

/*
 * Types of messages used for internal thread communication via pipes.
 */
typedef enum {
    MSG_MATCH_FOUND,      // Payload: Opponent info
    MSG_OPPONENT_MOVE,    // Payload: Move coordinates
    MSG_GAME_OVER,        // Payload: Result (Win/Loss/Draw)
    MSG_OPPONENT_QUIT,    // Opponent disconnected
    MSG_SHUTDOWN,         // Server is stopping
    MSG_ASK_NEW_HOST,     // Ask player if they want to stay as host
    MSG_LOBBY_UPDATE,     // Inform lobby players of new game
    MSG_JOIN_REQUEST,     // Ask Host to accept/reject
    MSG_REQUEST_RESULT,   // Tell Challenger if accepted/rejected
    MSG_JOIN_CANCELLED    // Tell Host that candidate cancelled
} InternalMsgType;

/*
 * Structure for internal messages passed between threads.
 */
typedef struct {
    InternalMsgType type;
    int data;            // Generic integer data (e.g., coordinates, ID)
    char payload[128];   // Text message or extra information
} InternalMessage;

/*
 * Represents a connected player in the server.
 */
typedef struct PlayerNode {
    char name[MAX_NAME_PLAYER];
    int client_sd;       // Client socket descriptor
    PlayerStatus status;
    
    int pipe_fd[2];      // Pipe for IPC: [0]=read, [1]=write
    
    int current_game_id;
    char symbol;         // 'X' or 'O'

    pthread_t thread_id;
    struct PlayerNode *next;
} PlayerNode;

/*
 * Represents a node in the queue of players waiting to join a game.
 */
typedef struct ChallengerNode {
    int client_sd;
    char name[MAX_NAME_PLAYER];
    struct ChallengerNode *next;
} ChallengerNode;

/*
 * Represents a game instance on the server.
 */
typedef struct GameNode {
    int game_id;

    char owner_name[MAX_NAME_PLAYER];
    int owner_sd;
    char owner_symbol;

    char opponent_name[MAX_NAME_PLAYER];
    int opponent_sd;
    char opponent_symbol;

    struct ChallengerNode *challenger_queue_head;
    struct ChallengerNode *challenger_queue_tail;

    char board[3][3];
    int current_turn_sd; // SD of the player whose turn it is

    GameStatus status;
    struct GameNode *next;
} GameNode;

/*
 * Encapsulates the global shared state of the server.
 */
typedef struct {
    PlayerNode *player_head;
    GameNode *game_head;
    
    pthread_mutex_t player_mutex;
    pthread_mutex_t game_mutex;

    int next_game_id;
} ServerState;

#endif // DATASTRUCTURES_H
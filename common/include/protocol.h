#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "datastructures.h"

// Network Message Types
typedef enum {
    // Client Requests
    CMD_LOGIN,          // Payload: char name[MAX_NAME_PLAYER]
    CMD_LIST_GAMES,     // Payload: None
    CMD_CREATE_GAME,    // Payload: char symbol ('X' or 'O')
    CMD_JOIN_GAME,      // Payload: int game_id
    CMD_MOVE,           // Payload: int cell_index (0-8)
    CMD_PLAY_AGAIN,     // Payload: int choice (1=Yes, 0=No)
    CMD_DISCONNECT,     // Payload: None

    // Server Responses
    RSP_OK,             // Payload: Optional message
    RSP_ERROR,          // Payload: Error message
    RSP_GAME_LIST,      // Payload: List of games (custom serialization)
    RSP_MATCH_FOUND,    // Payload: Opponent name, assigned symbol
    RSP_GAME_START,     // Payload: Who starts
    RSP_OPPONENT_MOVE,  // Payload: int cell_index
    RSP_GAME_OVER,      // Payload: int result (0=Draw, 1=You Won, 2=You Lost)
    RSP_ASK_PLAY_AGAIN, // Payload: None
    RSP_SHUTDOWN        // Payload: Server shutting down
} MessageType;

// Standard Network Packet Header
typedef struct {
    MessageType type;
    int payload_size;
} PacketHeader;

// Specific Payloads (examples)
typedef struct {
    char name[MAX_NAME_PLAYER];
} LoginRequest;

typedef struct {
    char symbol; // 'X' or 'O'
} CreateGameRequest;

typedef struct {
    int game_id;
} JoinGameRequest;

typedef struct {
    int cell_index;
} MoveRequest;

typedef struct {
    int id;
    char owner[MAX_NAME_PLAYER];
    char opponent[MAX_NAME_PLAYER]; // Can be empty if waiting
    char status[20]; // "WAITING", "RUNNING"
} GameInfoDTO; // Data Transfer Object for sending game list

#endif // PROTOCOL_H

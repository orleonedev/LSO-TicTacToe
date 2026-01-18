#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "datastructures.h"

/*
 * Message types for the network protocol between Client and Server.
 */
typedef enum {
    // Client Requests
    CMD_LOGIN,          
    CMD_LIST_GAMES,     
    CMD_CREATE_GAME,    
    CMD_JOIN_GAME,      
    CMD_MOVE,           
    CMD_PLAY_AGAIN,     
    CMD_DISCONNECT,     
    CMD_CANCEL_JOIN,    
    CMD_HOST_DECISION,  
    CMD_STOP_HOSTING,   

    // Server Responses
    RSP_OK,             
    RSP_ERROR,          
    RSP_GAME_LIST,      
    RSP_MATCH_FOUND,    
    RSP_GAME_START,     
    RSP_OPPONENT_MOVE,  
    RSP_GAME_OVER,      // Result: 0=Draw, 1=Win, 2=Loss
    RSP_ASK_PLAY_AGAIN, 
    RSP_SHUTDOWN,       
    RSP_LOBBY_UPDATE,   
    RSP_JOIN_REQUEST,   
    RSP_REQUEST_RESULT, 
    RSP_INVALID_MOVE,   
    RSP_OPPONENT_QUIT,  
    RSP_JOIN_CANCELLED  
} MessageType;

/*
 * Standard header for all network packets.
 */
typedef struct {
    MessageType type;
    int payload_size;
} PacketHeader;

// Payload Structures
typedef struct { char name[MAX_NAME_PLAYER]; } LoginRequest;
typedef struct { char symbol; } CreateGameRequest;
typedef struct { int game_id; } JoinGameRequest;
typedef struct { int cell_index; } MoveRequest;
typedef struct { int id; char owner[MAX_NAME_PLAYER]; char opponent[MAX_NAME_PLAYER]; char status[20]; } GameInfoDTO;

#endif // PROTOCOL_H
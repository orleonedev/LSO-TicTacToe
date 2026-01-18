#ifndef THREAD_LOGIC_H
#define THREAD_LOGIC_H

#include "../common/include/datastructures.h"

/*
 * Arguments passed to the client thread handler.
 */
typedef struct {
    int client_sd;
    ServerState *server_state;
} ClientThreadArgs;

/*
 * Thread entry function for handling a connected client.
 *
 * @param arg Pointer to ClientThreadArgs struct.
 * @return void* Always returns NULL.
 */
void *client_thread_handler(void *arg);

#endif // THREAD_LOGIC_H
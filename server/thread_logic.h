#ifndef THREAD_LOGIC_H
#define THREAD_LOGIC_H

#include "../common/include/common.h"

// Arguments for the client thread handler
typedef struct {
    int socket;
    ServerState *server_state;
} ClientThreadArgs;

/**
 * @brief Handles the communication with a connected client.
 *
 * @param arg The client thread arguments.
 * @return void*
 */
void *client_thread_handler(void *arg);

#endif // THREAD_LOGIC_H

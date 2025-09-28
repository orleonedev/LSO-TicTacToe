#include "thread_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void *client_thread_handler(void *arg) {
    ClientThreadArgs *thread_args = (ClientThreadArgs *)arg;
    int sock = thread_args->socket;
    // ServerState *server_state = thread_args->server_state;

    // TODO: Implement client handling logic here
    // (e.g., authentication, game logic, etc.)

    printf("Client disconnected: %d\n", sock);

    close(sock);
    free(thread_args);
    return NULL;
}



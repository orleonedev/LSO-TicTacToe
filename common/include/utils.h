#ifndef UTILS_H
#define UTILS_H

#include <pthread.h>
#include "datastructures.h"

// Future utility function declarations will go here.
void log_message(const char *message);
void send_message(int client_sd, const char *message);
void broadcast_message(ServerState *state, const char *message);

#endif // UTILS_H

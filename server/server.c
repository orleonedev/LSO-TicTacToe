#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "../common/include/common.h"
#include "thread_logic.h"

/**
 * @brief Initializes the server state.
 *
 * @param state The server state to initialize.
 */
void init_server_state(ServerState *state) {
    state->players = NULL;
    state->player_count = 0;
    state->matches = NULL;
    state->match_count = 0;
    pthread_mutex_init(&state->players_mutex, NULL);
    pthread_mutex_init(&state->matches_mutex, NULL);
}

/**
 * @brief Main function for the server.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int Exit code.
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    ServerState server_state;
    init_server_state(&server_state);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))) {
        printf("New connection accepted\n");

        pthread_t thread_id;
        ClientThreadArgs *thread_args = malloc(sizeof(ClientThreadArgs));
        thread_args->socket = new_socket;
        thread_args->server_state = &server_state;

        if (pthread_create(&thread_id, NULL, client_thread_handler, (void *)thread_args) < 0) {
            perror("could not create thread");
            return 1;
        }
    }

    if (new_socket < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    return 0;
}

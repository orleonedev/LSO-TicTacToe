#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "../common/include/datastructures.h"
#include "thread_logic.h"

/*
 * Main server entry point.
 * Initializes the server socket, shared state, and accepts incoming connections.
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    ServerState *server_state = malloc(sizeof(ServerState));
    if (!server_state) {
        perror("Failed to allocate server state");
        return EXIT_FAILURE;
    }
    server_state->player_head = NULL;
    server_state->game_head = NULL;
    server_state->next_game_id = 1;
    pthread_mutex_init(&server_state->player_mutex, NULL);
    pthread_mutex_init(&server_state->game_mutex, NULL);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        free(server_state);
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        free(server_state);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        free(server_state);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        free(server_state);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_sd < 0) {
            perror("Accept failed");
            continue;
        }

        printf("Nuova connessione accettata.\n");

        ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
        args->server_state = server_state;
        args->client_sd = client_sd;
        
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread_handler, (void *)args) != 0) {
            perror("Failed to create thread");
            close(client_sd);
            free(args);
        }
    }

    close(server_fd);
    free(server_state);

    return 0;
}
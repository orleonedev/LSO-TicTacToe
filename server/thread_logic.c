#include "thread_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

void *client_thread_handler(void *arg) {
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    ServerState *state = args->server_state;
    int client_sd = args->client_sd;
    free(args); // Libera la memoria degli argomenti

    printf("Thread gestore avviato per il client con socket: %d\n", client_sd);

    // TODO:
    // 1. Registrazione del giocatore (ricezione nome, controllo unicità)
    // 2. Aggiunta del giocatore alla lista (usando il mutex)
    // 3. Loop principale per la lobby (mostra partite, crea partita, join)
    // 4. Gestione della logica di gioco
    // 5. Disconnessione e pulizia delle risorse (rimozione giocatore/partita)

    char buffer[1024] = {0};
    while (1) {
        ssize_t bytes_read = read(client_sd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            // Client disconnected or error
            printf("Client %d disconnesso o errore di lettura.\n", client_sd);
            break;
        }
        buffer[bytes_read] = '\0';
        printf("Messaggio dal client: %s\n", buffer);

        if (strcmp(buffer, "quit\n") == 0 || strcmp(buffer, "quit") == 0) {
            send(client_sd, "Connessione chiusa", 19, 0);
            printf("Ricevuto 'quit' dal client: %d\n", client_sd);
            break;
        } else {
            send(client_sd, "Messaggio ricevuto", 18, 0);
        }
    }

    close(client_sd);
    printf("Connessione chiusa con il client: %d\n", client_sd);
    pthread_exit(NULL);
}



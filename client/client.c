#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../common/include/utils.h"
#include "../common/include/datastructures.h"

/**
 * @brief Main function for the client.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int Exit code.
 */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failure");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server\n");
    

    char buffer[1024];
    while (1) {
        printf("Enter message (type 'quit' to exit): ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            printf("Input error.\n");
            break;
        }
        // Remove newline character
        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "quit") == 0) {
            printf("Quitting...\n");
            if (send(sock, buffer, strlen(buffer), 0) < 0) {
                perror("send");
                break;
            }
            break;
        }

        if (send(sock, buffer, strlen(buffer), 0) < 0) {
            perror("send");
            break;
        }

        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0) {
            perror("recv");
            break;
        } else if (bytes_received == 0) {
            printf("Server closed connection.\n");
            break;
        }
        buffer[bytes_received] = '\0';
        printf("Server: %s\n", buffer);
    }

    close(sock);
    return 0;
}

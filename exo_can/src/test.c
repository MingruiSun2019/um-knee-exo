#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main() {
    int client_socket;
    struct sockaddr_un addr;
    char buffer[1024];

    // Check if the socket file exists
    if (access("/tmp/CO_command_socket", F_OK) == -1) {
        printf("Couldn't Connect!\n");
        return 1;
    }

    // Create socket
    if ((client_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Set socket parameters
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/CO_command_socket", sizeof(addr.sun_path) - 1);

    // Connect to server
    if (connect(client_socket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect");
        exit(1);
    }

    printf("Connected to server. Type your messages below.\n");

    while (1) {
        printf("> ");
        fgets(buffer, 1024, stdin);

        // Check for empty input
        if (strcmp(buffer, "\n") != 0) {
            char message[1024];
            snprintf(message, sizeof(message), "[1] %s", buffer);
            printf("SEND: %s", message);

            // Send message
            if (send(client_socket, message, strlen(message), 0) == -1) {
                perror("send");
                continue;
            }

            // Receive response
            int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
            if (bytes_received > 0) {
                printf("RECEIVE: %.*s\n", bytes_received, buffer);
            }
        }
    }

    // Close socket
    close(client_socket);
    return 0;
}

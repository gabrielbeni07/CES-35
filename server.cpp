#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_PORT 8080
#define BUF_SIZE 4096
#define QUEUE_SIZE 10

struct client_info {
    int socket;
    struct sockaddr_in client_addr;
};

struct client_last_access {
    struct sockaddr_in client_addr;
    time_t last_access;
    struct client_last_access *next;
};

struct client_last_access *clients = NULL;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void *client_handler(void *arg) {
    struct client_info *cinfo = (struct client_info *)arg;
    int sa = cinfo->socket;
    struct sockaddr_in client_addr = cinfo->client_addr;
    free(cinfo);

    char buf[BUF_SIZE];
    int bytes_read;

    bytes_read = read(sa, buf, BUF_SIZE - 1);
    if (bytes_read <= 0) {
        close(sa);
        pthread_exit(NULL);
    }
    buf[bytes_read] = '\0';

    time_t current_time = time(NULL);
    pthread_mutex_lock(&clients_mutex);
    struct client_last_access *current = clients;
    while (current != NULL) {
        if (memcmp(&current->client_addr, &client_addr, sizeof(struct sockaddr_in)) == 0) {
            current->last_access = current_time;
            break;
        }
        current = current->next;
    }
    if (current == NULL) {
        struct client_last_access *new_client = (struct client_last_access *)malloc(sizeof(struct client_last_access));
        new_client->client_addr = client_addr;
        new_client->last_access = current_time;
        new_client->next = clients;
        clients = new_client;
    }
    pthread_mutex_unlock(&clients_mutex);

    if (strncmp(buf, "MyGet", 5) == 0) {
        char *filename = buf + 5;
        while (*filename == ' ') filename++;

        int fd = open(filename, O_RDONLY);
        if (fd < 0) {
            const char *error_msg = "Error: Cannot open file\n";
            write(sa, error_msg, strlen(error_msg));
        } else {
            while ((bytes_read = read(fd, buf, BUF_SIZE)) > 0) {
                write(sa, buf, bytes_read);
            }
            close(fd);
        }
    } else if (strncmp(buf, "MyLastAccess", 12) == 0) {
        pthread_mutex_lock(&clients_mutex);
        current = clients;
        while (current != NULL) {
            if (memcmp(&current->client_addr, &client_addr, sizeof(struct sockaddr_in)) == 0) {
                break;
            }
            current = current->next;
        }
        pthread_mutex_unlock(&clients_mutex);

        if (current != NULL) {
            char time_buf[64];
            struct tm *tm_info = localtime(&current->last_access);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S\n", tm_info);
            write(sa, time_buf, strlen(time_buf));
        } else {
            const char *error_msg = "Error: No last access time recorded\n";
            write(sa, error_msg, strlen(error_msg));
        }
    } else {
        const char *error_msg = "Error: Unknown command\n";
        write(sa, error_msg, strlen(error_msg));
    }

    close(sa);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Falha ao criar o socket do servidor");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Falha ao fazer bind no endereço do servidor");
        exit(1);
    }

    if (listen(server_socket, QUEUE_SIZE) < 0) {
        perror("Falha ao fazer listen no socket");
        exit(1);
    }

    printf("Servidor iniciado e aguardando conexões...\n");

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Falha ao aceitar conexão");
            continue;
        }

        struct client_info *cinfo = (struct client_info *)malloc(sizeof(struct client_info));
        cinfo->socket = client_socket;
        cinfo->client_addr = client_addr;

        if (pthread_create(&thread_id, NULL, client_handler, cinfo) != 0) {
            perror("Falha ao criar thread para o cliente");
            close(client_socket);
            free(cinfo);
        }

        pthread_detach(thread_id);
    }

    close(server_socket);
    return 0;
}

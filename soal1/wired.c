#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include "protocol.h"

int server_fd;
Client clients[MAX_CLIENTS];
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
time_t server_start;

void write_log(const char *category, const char *message) {
    pthread_mutex_lock(&clients_lock);
    
    FILE *f = fopen(LOG_FILE, "a"); 
    if (!f) {
        pthread_mutex_unlock(&clients_lock);
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), 
             "%Y-%m-%d %H:%M:%S", t);

    fprintf(f, "[%s] [%s] [%s]\n", timestamp, category, message);
    fclose(f);
    
    pthread_mutex_unlock(&clients_lock);
}

void broadcast(Message *msg, int sender_fd) {
    pthread_mutex_lock(&clients_lock);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].fd != sender_fd) {
            send(clients[i].fd, msg, sizeof(Message), 0);
        }
    }
    
    pthread_mutex_unlock(&clients_lock);
}

void handle_rpc(int client_fd, RPCCommand cmd, const char *sender) {
    Message reply = {MSG_RPC, "System", ""};
    char log_msg[BUFFER_SIZE];

    switch (cmd) {
        case RPC_GET_USERS: {
            int count = 0;
            pthread_mutex_lock(&clients_lock);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].active && 
                    strcmp(clients[i].name, ADMIN_NAME) != 0) {
                    count++;
                }
            }
            pthread_mutex_unlock(&clients_lock);

            snprintf(reply.content, BUFFER_SIZE,
                     "Active Entities: %d", count);
            send(client_fd, &reply, sizeof(reply), 0);

            write_log("Admin", "RPC_GET_USERS");
            break;
        }

        case RPC_GET_UPTIME: {
            time_t now = time(NULL);
            long uptime = (long)(now - server_start);
            long hours   = uptime / 3600;
            long minutes = (uptime % 3600) / 60;
            long seconds = uptime % 60;

            snprintf(reply.content, BUFFER_SIZE,
                     "Uptime: %02ld:%02ld:%02ld", 
                     hours, minutes, seconds);
            send(client_fd, &reply, sizeof(reply), 0);

            write_log("Admin", "RPC_GET_UPTIME");
            break;
        }

        case RPC_SHUTDOWN: {
            write_log("Admin", "RPC_SHUTDOWN");
            write_log("System", "EMERGENCY SHUTDOWN INITIATED");

            Message shutdown_msg = {MSG_CHAT, "System",
                                   "Server shutting down!"};
            broadcast(&shutdown_msg, client_fd);

            close(server_fd);
            exit(0);
            break;
        }
    }
}

void *handle_client(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);

    Message msg;

    recv(client_fd, &msg, sizeof(msg), 0);

    if(msg.type == MSG_REGISTER) {
        pthread_mutex_lock(&clients_lock);
        
        int name_taken = 0;
        for(int i=0; i<MAX_CLIENTS; i++) {
            if(clients[i].active && strcmp(clients[i].name, msg.sender) == 0) {
                name_taken = 1;
                break;
            }
        }
        
        if(name_taken) {
            Message reply = {MSG_REGISTER, "System", "Name already taken"};
            send(client_fd, &reply, sizeof(reply), 0);
            pthread_mutex_unlock(&clients_lock);
            close(client_fd);
            return NULL;
        }
        
        for(int i=0; i<MAX_CLIENTS; i++) {
            if(!clients[i].active) {
                clients[i].fd = client_fd;
                clients[i].active = 1;
                strncpy(clients[i].name, msg.sender, NAME_SIZE);
                break;
            }
        }
        pthread_mutex_unlock(&clients_lock);
        
        Message welcome = {MSG_CHAT, "System", ""};
        snprintf(welcome.content, BUFFER_SIZE, "--- Welcome to The Wired, %s ---", msg.sender);
        send(client_fd, &welcome, sizeof(welcome), 0);
        
        char log_msg[BUFFER_SIZE];
        if (strcmp(msg.sender, ADMIN_NAME) != 0) {
            snprintf(log_msg, BUFFER_SIZE, "User '%s' connected", msg.sender);
            write_log("System", log_msg);
        }
    }

    int is_admin = 0;

    if (strcmp(msg.sender, ADMIN_NAME) == 0) {
        Message pw_prompt = {MSG_REGISTER, "System", "Enter Password: "};
        send(client_fd, &pw_prompt, sizeof(pw_prompt), 0);

        Message pw_msg;
        recv(client_fd, &pw_msg, sizeof(pw_msg), 0);

        if (strcmp(pw_msg.content, ADMIN_PASSWORD) == 0) {
            is_admin = 1;
            Message auth_ok = {MSG_REGISTER, "System", "Authentication Successful. Granted Admin privileges."};
            send(client_fd, &auth_ok, sizeof(auth_ok), 0);
        } else {
            Message auth_fail = {MSG_REGISTER, "System", "Wrong password!"};
            send(client_fd, &auth_fail, sizeof(auth_fail), 0);
            close(client_fd);
            return NULL;
        }
    }

    // loop utama — cukup SATU
    while (recv(client_fd, &msg, sizeof(msg), 0) > 0) {
        if (msg.type == MSG_EXIT) break;

        if (msg.type == MSG_RPC && is_admin) {
            handle_rpc(client_fd, msg.rpc_cmd, msg.sender);
        } else if (msg.type == MSG_CHAT) {
            Message broadcast_msg = {MSG_CHAT, "", ""};
            strncpy(broadcast_msg.sender, msg.sender, NAME_SIZE);
            snprintf(broadcast_msg.content, BUFFER_SIZE,
                    "[%s]: %s", msg.sender, msg.content);
            broadcast(&broadcast_msg, client_fd);

            char log_msg[BUFFER_SIZE];
            snprintf(log_msg, BUFFER_SIZE, "[%s]: %s", 
                     msg.sender, msg.content);
            write_log("User", log_msg);
        }
    }

    char log_msg[BUFFER_SIZE];
    snprintf(log_msg, BUFFER_SIZE, 
             "User '%s' disconnected", msg.sender);
    write_log("System", log_msg);

    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == client_fd) {
            clients[i].active = 0;
            memset(clients[i].name, 0, NAME_SIZE);
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);

    close(client_fd);
    return NULL;
}

int main() {
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));

    listen(server_fd, 10);

    write_log("System", "SERVER ONLINE");

    printf("[ The Wired ] Server online pada port %d\n", PORT);

    while(1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = client_fd;

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, fd_ptr);
        pthread_detach(tid);
    }

    return 0;
}
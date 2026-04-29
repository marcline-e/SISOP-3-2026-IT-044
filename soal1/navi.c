#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <termios.h>
#include "protocol.h"

int sock_fd;

struct termios orig_termios;

pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;
char input_buffer[BUFFER_SIZE] = {0};

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON); 
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void *receive_handler(void *arg) {
    Message msg;
    
    while (recv(sock_fd, &msg, sizeof(msg), 0) > 0) {
        if (msg.type == MSG_CHAT) {
            pthread_mutex_lock(&print_lock);
            printf("\r\033[K");
            printf("%s\n", msg.content); 
            printf("> %s", input_buffer); 
            fflush(stdout);
            pthread_mutex_unlock(&print_lock);
        }
    }

    disable_raw_mode();
    printf("\n[System] Koneksi ke The Wired terputus.\n");
    exit(0);
    return NULL;
}

int main() {
    struct sockaddr_in server_addr;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // localhost

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Gagal konek ke The Wired!\n");
        exit(1);
    }

    char name[NAME_SIZE];
    printf("Enter your name: ");
    fgets(name, NAME_SIZE, stdin);
    name[strcspn(name, "\n")] = 0; 

    Message reg_msg = {MSG_REGISTER, "", ""};
    strncpy(reg_msg.sender, name, NAME_SIZE);
    send(sock_fd, &reg_msg, sizeof(reg_msg), 0);

    Message response;
    recv(sock_fd, &response, sizeof(response), 0);
    printf("%s\n", response.content);

    if (strcmp(response.content, "Name already taken") == 0) { 
        close(sock_fd);
        return 0;
    }

    int is_admin = (strcmp(name, ADMIN_NAME) == 0);

    if (is_admin) {
        char pw[BUFFER_SIZE];
        printf("Enter Password: ");
        fgets(pw, BUFFER_SIZE, stdin);
        pw[strcspn(pw, "\n")] = 0;

        Message pw_msg = {MSG_REGISTER, "", ""};
        strncpy(pw_msg.content, pw, BUFFER_SIZE);
        send(sock_fd, &pw_msg, sizeof(pw_msg), 0);

        Message auth_result;
        recv(sock_fd, &auth_result, sizeof(auth_result), 0);
        printf("%s\n", auth_result.content);

        if (strstr(auth_result.content, "Wrong") != NULL) {
            close(sock_fd);
            return 0;
        }

        while (1) {
            printf("\n=== THE KNIGHTS CONSOLE ===\n");
            printf("1. Check Active Entities (Users)\n");
            printf("2. Check Server Uptime\n");
            printf("3. Execute Emergency Shutdown\n");
            printf("4. Disconnect\n");
            printf("Command >> ");

            int choice;
            scanf("%d", &choice);
            getchar();

            if (choice == 4) {
                Message exit_msg = {MSG_EXIT, ADMIN_NAME, ""};
                send(sock_fd, &exit_msg, sizeof(exit_msg), 0);
                break;
            }

            Message rpc_msg = {MSG_RPC, ADMIN_NAME, ""};
            switch (choice) {
                case 1: rpc_msg.rpc_cmd = RPC_GET_USERS; break;
                case 2: rpc_msg.rpc_cmd = RPC_GET_UPTIME; break;
                case 3: rpc_msg.rpc_cmd = RPC_SHUTDOWN; break;
                default: continue;
            }
            send(sock_fd, &rpc_msg, sizeof(rpc_msg), 0);

            Message result;
            recv(sock_fd, &result, sizeof(result), 0);
            printf("\n%s\n", result.content);
        }

        close(sock_fd);
        return 0;
    }

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_handler, NULL);
    pthread_detach(recv_thread);

    enable_raw_mode();

    char buffer[BUFFER_SIZE];
    int i = 0;
    memset(input_buffer, 0, BUFFER_SIZE);
    printf("> ");
    fflush(stdout);

    while (1) {
        int c = getchar();

        if (c == '\n' || c == '\r') {
            printf("\n");

            pthread_mutex_lock(&print_lock);
            strncpy(buffer, input_buffer, BUFFER_SIZE);
            i = 0;
            memset(input_buffer, 0, BUFFER_SIZE);
            pthread_mutex_unlock(&print_lock);

            if (strcmp(buffer, "/exit") == 0) {
                disable_raw_mode();
                Message exit_msg = {MSG_EXIT, "", ""};
                strncpy(exit_msg.sender, name, NAME_SIZE);
                send(sock_fd, &exit_msg, sizeof(exit_msg), 0);
                printf("[System] Disconnecting from The Wired...\n");
                break;
            }

            if (strlen(buffer) > 0) {
                Message chat_msg = {MSG_CHAT, "", ""};
                strncpy(chat_msg.sender, name, NAME_SIZE);
                strncpy(chat_msg.content, buffer, BUFFER_SIZE);
                send(sock_fd, &chat_msg, sizeof(chat_msg), 0);
            }

            printf("> ");
            fflush(stdout);

        } else if (c == 127 || c == '\b') {
            if (i > 0) {
                pthread_mutex_lock(&print_lock);
                input_buffer[--i] = 0;
                pthread_mutex_unlock(&print_lock);
                printf("\b \b"); 
                fflush(stdout);
            }

        } else if (c >= 32 && i < BUFFER_SIZE - 1) {
            pthread_mutex_lock(&print_lock);
            input_buffer[i++] = c;
            input_buffer[i] = 0;
            pthread_mutex_unlock(&print_lock);
            printf("%c", c); 
            fflush(stdout);
        }
    }

    disable_raw_mode();
    close(sock_fd);
    return 0;
}
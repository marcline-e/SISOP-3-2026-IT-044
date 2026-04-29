#ifndef PROTOCOL_H
#define PROTOCOL_H

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define NAME_SIZE 64
#define LOG_FILE "history.log"

#define ADMIN_NAME     "The Knights"
#define ADMIN_PASSWORD "protocol7"

typedef enum {
    MSG_REGISTER,
    MSG_CHAT,
    MSG_EXIT,
    MSG_RPC
} MessageType;

typedef enum {
    RPC_GET_USERS,      
    RPC_GET_UPTIME,     
    RPC_SHUTDOWN        
} RPCCommand;

typedef struct {
    MessageType type;
    char sender[NAME_SIZE];
    char content[BUFFER_SIZE];
    RPCCommand rpc_cmd;
} Message;

typedef struct {
    int fd;
    char name[NAME_SIZE];
    int active;
} Client;

#endif
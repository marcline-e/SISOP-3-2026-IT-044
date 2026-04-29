#ifndef ARENA_H
#define ARENA_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <time.h>

#define SHM_KEY   0x00001234  
#define MSG_KEY   0x00005678  
#define SEM_KEY   0x00009012  

#define MAX_PLAYERS 10
#define BASE_DAMAGE 10
#define BASE_HEALTH 100
#define MATCHMAKING_TIMEOUT 35

typedef struct  {
    char username[64];
    char password[64];
    int gold;
    int level;
    int xp;
    int weapon_bonus_dmg;
    char weapon_name[32];
    int logged_in;
    int in_battle;
} Player;

typedef struct {
    char time_str[20];    
    char opponent[64];
    int result;
    int xp_gained;
} BattleRecord;

typedef struct {
    char username[64];
    long pid;
} QueueEntry;

typedef struct {
    Player players[MAX_PLAYERS];
    int player_count;

    QueueEntry queue[MAX_PLAYERS];
    int queue_size;

    struct {
        char player1[64];
        char player2[64];
        int hp1;
        int hp2;
        int active;
    } battles[MAX_PLAYERS/2];

    BattleRecord history[MAX_PLAYERS][20];
    int history_count [MAX_PLAYERS];
} Arena;

typedef struct {
    long mtype;
    char mtext[256];
} MsgBuf;

typedef struct {
    char name[32];
    int price;
    int bonus_dmg;
} Weapon;

static const Weapon WEAPONS[] = {
    {"Wood Sword", 100, 5},
    {"Iron Sword", 300, 15},
    {"Steel Axe", 600, 30},
    {"Demon Blade", 1500, 60},
    {"God Slayer", 5000, 150}
};
#define WEAPON_COUNT 5

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <time.h>
#include "arena.h"

void save_history(const char *username, const char *opponent,int result, int xp_gained);
int find_player(const char *username);
void handle_history_save(char *username, char *opponent, int result, int xp, long client_pid);

int shm_id;
int msg_id;
int sem_id;
Arena *arena;

void sem_lock() {
    struct sembuf op = {0, -1, 0};
    semop(sem_id, &op, 1);
}

void sem_unlock() {
    struct sembuf op = {0, 1, 0};
    semop(sem_id, &op, 1);
}

void setup_ipc() {
    shm_id = shmget(SHM_KEY, sizeof(Arena), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget gagal");
        exit(1);
    }

    arena = (Arena*)shmat(shm_id, NULL, 0);
    if (arena == (void*)-1) {
        perror("shmat gagal");
        exit(1);
    }

    memset(arena, 0, sizeof(Arena));

    msg_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msg_id < 0) {
        perror("msgget gagal");
        exit(1);
    }

    sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (sem_id < 0) {
        perror("semget gagal");
        exit(1);
    }

    semctl(sem_id, 0, SETVAL, 1);

    printf("Orion is ready (PID: %d)\n", getpid());
}

void cleanup_ipc() {
    shmdt(arena);                     
    shmctl(shm_id, IPC_RMID, NULL);   
    msgctl(msg_id, IPC_RMID, NULL);   
    semctl(sem_id, 0, IPC_RMID);
}

// ===== HELPER: Cari player by username =====
int find_player(const char *username) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(arena->players[i].username, username) == 0)
            return i;
    }
    return -1; // tidak ditemukan
}

// ===== HANDLER REGISTER =====
void handle_register(char *username, char *password, long client_pid) {
    MsgBuf reply;
    reply.mtype = client_pid;

    sem_lock();

    // Cek apakah username sudah ada
    if (find_player(username) != -1) {
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "FAIL:Username already exists");
        sem_unlock();
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        return;
    }

    // Cek apakah masih ada slot
    if (arena->player_count >= MAX_PLAYERS) {
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "FAIL:Arena is full");
        sem_unlock();
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        return;
    }

    // Daftarkan player baru
    int idx = arena->player_count++;
    strncpy(arena->players[idx].username, username, 64);
    strncpy(arena->players[idx].password, password, 64);
    arena->players[idx].gold       = 150;
    arena->players[idx].level      = 1;
    arena->players[idx].xp         = 0;
    arena->players[idx].logged_in  = 0;
    arena->players[idx].in_battle  = 0;
    arena->players[idx].weapon_bonus_dmg = 0;
    strncpy(arena->players[idx].weapon_name, "None", 32);

    sem_unlock();

    snprintf(reply.mtext, sizeof(reply.mtext), "OK:Account created");
    msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
}

// ===== HANDLER LOGIN =====
void handle_login(char *username, char *password, long client_pid) {
    MsgBuf reply;
    reply.mtype = client_pid;

    sem_lock();

    int idx = find_player(username);

    // Cek username ada
    if (idx == -1) {
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "FAIL:Username not found");
        sem_unlock();
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        return;
    }

    // Cek password
    if (strcmp(arena->players[idx].password, password) != 0) {
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "FAIL:Wrong password");
        sem_unlock();
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        return;
    }

    // Cek sudah login di tempat lain
    if (arena->players[idx].logged_in) {
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "FAIL:Account already logged in");
        sem_unlock();
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        return;
    }

    // Login berhasil
    arena->players[idx].logged_in = 1;
    sem_unlock();

    // Kirim data player untuk ditampilkan
    snprintf(reply.mtext, sizeof(reply.mtext),
             "OK:%s:%d:%d:%d:%s",
             username,
             arena->players[idx].level,
             arena->players[idx].gold,
             arena->players[idx].xp,
             arena->players[idx].weapon_name);
    msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
}

// ===== HANDLER LOGOUT =====
void handle_logout(char *username, long client_pid) {
    MsgBuf reply;
    reply.mtype = client_pid;

    sem_lock();
    int idx = find_player(username);
    if (idx != -1) {
        arena->players[idx].logged_in = 0;
        arena->players[idx].in_battle = 0; // ← sudah ada, pastikan ada!
        
        // ✅ Kalau player di queue, hapus dari queue juga
        for (int i = 0; i < arena->queue_size; i++) {
            if (strcmp(arena->queue[i].username, username) == 0) {
                for (int j = i; j < arena->queue_size-1; j++) {
                    arena->queue[j] = arena->queue[j+1];
                }
                arena->queue_size--;
                break;
            }
        }
    }
    sem_unlock();

    snprintf(reply.mtext, sizeof(reply.mtext), "OK:Logged out");
    msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
}

void handle_battle_request(char *username, long client_pid) {
    MsgBuf reply;
    reply.mtype = client_pid;

    sem_lock();

    int idx = find_player(username);

    if (idx == -1) {
        snprintf(reply.mtext, sizeof(reply.mtext), "FAIL:Player not found");
        sem_unlock();
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        return;
    }

    arena->players[idx].in_battle = 1;

    // Cek queue
    int opponent_idx = -1;
    for (int i = 0; i < arena->queue_size; i++) {
        // ✅ akses .username
        int q_idx = find_player(arena->queue[i].username);
        if (q_idx != -1 && q_idx != idx) {
            opponent_idx = i;
            break;
        }
    }

    if (opponent_idx != -1) {
        char opponent_name[64];
        // ✅ akses .username
        strncpy(opponent_name, 
                arena->queue[opponent_idx].username, 64);
        // ✅ ambil PID lawan
        long opponent_pid = arena->queue[opponent_idx].pid;
        
        int opp_idx = find_player(opponent_name);

        // Hapus dari queue — ✅ pakai assignment langsung
        for (int i = opponent_idx; i < arena->queue_size-1; i++) {
            arena->queue[i] = arena->queue[i+1];
        }
        arena->queue_size--;

        // Setup battle slot
        int battle_slot = -1;
        for (int i = 0; i < MAX_PLAYERS/2; i++) {
            if (!arena->battles[i].active) {
                battle_slot = i;
                break;
            }
        }

        arena->battles[battle_slot].active = 1;
        strncpy(arena->battles[battle_slot].player1, username, 64);
        strncpy(arena->battles[battle_slot].player2, 
                opponent_name, 64);

        int hp1 = BASE_HEALTH + (arena->players[idx].xp / 10);
        int hp2 = BASE_HEALTH + (arena->players[opp_idx].xp / 10);
        arena->battles[battle_slot].hp1 = hp1;
        arena->battles[battle_slot].hp2 = hp2;

        sem_unlock();

        // Beritahu player yang baru request
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "BATTLE_START:%s:%d:%d:%d",
                 opponent_name, battle_slot, hp1, hp2);
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);

        // ✅ Beritahu lawan yang nunggu di queue
        MsgBuf notify;
        notify.mtype = opponent_pid;
        snprintf(notify.mtext, sizeof(notify.mtext),
                 "BATTLE_START:%s:%d:%d:%d",
                 username, battle_slot, hp2, hp1);
        msgsnd(msg_id, &notify, sizeof(notify.mtext), 0);

    } else {
        // Masuk queue
        // ✅ akses .username dan .pid
        strncpy(arena->queue[arena->queue_size].username, 
                username, 64);
        arena->queue[arena->queue_size].pid = client_pid;
        arena->queue_size++;
        sem_unlock();

        snprintf(reply.mtext, sizeof(reply.mtext), 
                 "WAIT:Searching...");
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
    }
}

void handle_attack(char *username, int slot, int damage, long client_pid) {
    MsgBuf reply;
    reply.mtype = client_pid;

    sem_lock();

    // Tentukan apakah attacker adalah player1 atau player2
    int is_p1 = strcmp(arena->battles[slot].player1, username) == 0;

    if (is_p1) {
        arena->battles[slot].hp2 -= damage;
        if (arena->battles[slot].hp2 < 0) 
            arena->battles[slot].hp2 = 0;
    } else {
        arena->battles[slot].hp1 -= damage;
        if (arena->battles[slot].hp1 < 0) 
            arena->battles[slot].hp1 = 0;
    }

    // Cek apakah battle selesai
    if (arena->battles[slot].hp1 <= 0 || 
        arena->battles[slot].hp2 <= 0) {
        arena->battles[slot].active = 0;

        // Update XP & Gold
        char *winner = arena->battles[slot].hp1 > 0 ?
                       arena->battles[slot].player1 :
                       arena->battles[slot].player2;
        char *loser  = arena->battles[slot].hp1 > 0 ?
                       arena->battles[slot].player2 :
                       arena->battles[slot].player1;

        int w_idx = find_player(winner);
        int l_idx = find_player(loser);

        // Update stats
        arena->players[w_idx].xp   += 50;
        arena->players[w_idx].gold += 120;
        arena->players[l_idx].xp   += 15;
        arena->players[l_idx].gold += 30;

        // Update level
        if (arena->players[w_idx].xp / 100 > 
            arena->players[w_idx].level - 1) {
            arena->players[w_idx].level++;
        }
        if (arena->players[l_idx].xp / 100 > 
            arena->players[l_idx].level - 1) {
            arena->players[l_idx].level++;
        }

        // Simpan history
        save_history(winner, loser,  1, 50); // winner: menang, +50 xp
        save_history(loser,  winner, 0, 15); // loser:  kalah,  +15 xp

        // Tandai tidak in_battle
        arena->players[w_idx].in_battle = 0;
        arena->players[l_idx].in_battle = 0;
    }

    sem_unlock();

    snprintf(reply.mtext, sizeof(reply.mtext), "OK:Attack processed");
    msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
}

void handle_buy(char *username, int weapon_idx, long client_pid) {
    MsgBuf reply;
    reply.mtype = client_pid;

    // Validasi index senjata
    if (weapon_idx < 0 || weapon_idx >= WEAPON_COUNT) {
        snprintf(reply.mtext, sizeof(reply.mtext), 
                 "FAIL:Invalid weapon");
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        return;
    }

    sem_lock();

    int idx = find_player(username);
    Weapon w = WEAPONS[weapon_idx];

    // Cek gold cukup
    if (arena->players[idx].gold < w.price) {
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "FAIL:Not enough gold (need %d, have %d)",
                 w.price, arena->players[idx].gold);
        sem_unlock();
        msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        return;
    }

    // Beli senjata — otomatis pakai yang damage terbesar
    if (w.bonus_dmg > arena->players[idx].weapon_bonus_dmg) {
        arena->players[idx].gold -= w.price;
        arena->players[idx].weapon_bonus_dmg = w.bonus_dmg;
        strncpy(arena->players[idx].weapon_name, w.name, 32);

        sem_unlock();
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "OK:Bought %s!", w.name);
    } else {
        sem_unlock();
        snprintf(reply.mtext, sizeof(reply.mtext),
                 "FAIL:You have a stronger weapon already");
    }

    msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
}

void save_history(const char *username, const char *opponent,
                  int result, int xp_gained) {
    int idx = find_player(username);
    if (idx == -1) return;

    int h = arena->history_count[idx];
    if (h >= 20) return; // max 20 history

    // Ambil waktu sekarang
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(arena->history[idx][h].time_str,
             sizeof(arena->history[idx][h].time_str),
             "%H:%M", t);

    strncpy(arena->history[idx][h].opponent, opponent, 64);
    arena->history[idx][h].result    = result;
    arena->history[idx][h].xp_gained = xp_gained;
    arena->history_count[idx]++;
}

void handle_signal(int sig) {
    printf("\n[Orion] Shutting down...\n");
    cleanup_ipc();
    exit(0);
}

void handle_history_save(char *username, char *opponent, int result, int xp, long client_pid) {
    MsgBuf reply;
    reply.mtype = client_pid;

    sem_lock();
    save_history(username, opponent, result, xp);
    sem_unlock();

    snprintf(reply.mtext, sizeof(reply.mtext), "OK:History saved");
    msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
}

int main() {
    signal(SIGINT, handle_signal);   
    signal(SIGTERM, handle_signal);  

    setup_ipc();

    MsgBuf msg;
    while (1) {
        if (msgrcv(msg_id, &msg, sizeof(msg.mtext), 1, 0) < 0) {
            continue;
        }

        // Parse pesan: "COMMAND:arg1:arg2:PID"
        char command[32], arg1[64], arg2[64];
        long client_pid;

        // Format: COMMAND:username:password:PID
        sscanf(msg.mtext, "%[^:]:%[^:]:%[^:]:%ld",
               command, arg1, arg2, &client_pid);

        if (strcmp(command, "REGISTER") == 0) {
            handle_register(arg1, arg2, client_pid);

        } else if (strcmp(command, "LOGIN") == 0) {
            handle_login(arg1, arg2, client_pid);

        } else if (strcmp(command, "LOGOUT") == 0) {
            handle_logout(arg1, client_pid);
        } else if (strcmp(command, "BUY") == 0) {
            int weapon_idx = atoi(arg2);
            handle_buy(arg1, weapon_idx, client_pid);
        } else if (strcmp(command, "ATTACK") == 0) {
            int slot   = atoi(arg2);
            // damage ada di field ke-3, perlu update parsing
            char damage_str[32];
            sscanf(msg.mtext, "%[^:]:%[^:]:%[^:]:%[^:]:%ld",
                command, arg1, arg2, damage_str, &client_pid);
            handle_attack(arg1, slot, atoi(damage_str), client_pid);
        } else if (strcmp(command, "BATTLE") == 0) {
            handle_battle_request(arg1, client_pid);
        } else if (strcmp(command, "HISTORY") == 0) {
            // Format: "HISTORY:username:opponent:result:xp:PID"
            char opponent[64], result_str[8], xp_str[8];
            long pid;
            sscanf(msg.mtext, "%[^:]:%[^:]:%[^:]:%[^:]:%[^:]:%ld",
                command, arg1, opponent, result_str, xp_str, &pid);
            handle_history_save(arg1, opponent, 
                                atoi(result_str), atoi(xp_str), pid);

        } else if (strcmp(command, "CANCEL") == 0) {
            // Hapus dari matchmaking queue
            sem_lock();
            int idx = find_player(arg1);
            for (int i = 0; i < arena->queue_size; i++) {
                if (strcmp(arena->queue[i].username, arg1) == 0) {
                    for (int j = i; j < arena->queue_size-1; j++) {
                        arena->queue[j] = arena->queue[j+1];
                    }
                    arena->queue_size--;
                    if (idx != -1) arena->players[idx].in_battle = 0;
                    break;
                }
            }
            sem_unlock();

            MsgBuf reply;
            reply.mtype = client_pid;
            snprintf(reply.mtext, sizeof(reply.mtext), "OK:Cancelled");
            msgsnd(msg_id, &reply, sizeof(reply.mtext), 0);
        }  
    }

    cleanup_ipc();
    return 0;
}
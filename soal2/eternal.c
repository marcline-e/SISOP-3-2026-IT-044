// eternal.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <time.h>
#include "arena.h"
#include <termios.h>

struct termios orig_termios;

// ===== GLOBAL =====
int    shm_id;
int    msg_id;
Arena *arena;
long   my_pid;

// ===== HELPER: Kirim pesan ke orion & tunggu balasan =====
void send_to_orion(const char *msg_text, MsgBuf *reply) {
    MsgBuf msg;
    msg.mtype = 1;  // semua pesan ke orion pakai mtype=1
    strncpy(msg.mtext, msg_text, sizeof(msg.mtext));
    msgsnd(msg_id, &msg, sizeof(msg.mtext), 0);

    // Tunggu balasan dengan mtype = PID kita
    msgrcv(msg_id, reply, sizeof(reply->mtext), my_pid, 0);
}

// ===== SETUP IPC =====
int setup_ipc() {
    // Attach ke shared memory yang sudah dibuat orion
    shm_id = shmget(SHM_KEY, sizeof(Arena), 0666);
    if (shm_id < 0) {
        printf("Orion are you there?\n");
        return 0; // gagal
    }

    arena = (Arena*)shmat(shm_id, NULL, 0);
    if (arena == (void*)-1) {
        printf("Orion are you there?\n");
        return 0;
    }

    // Attach ke message queue
    msg_id = msgget(MSG_KEY, 0666);
    if (msg_id < 0) {
        printf("Orion are you there?\n");
        return 0;
    }

    return 1; // sukses
}

int find_player(const char *username) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (strcmp(arena->players[i].username, username) == 0)
            return i;
    }
    return -1;
}

// ===== TAMPILAN ASCII ART =====
void print_banner() {
    printf(
        " ____    _  _____ _____ _     _____    ___  _____ \n"
        "| __ )  / \\|_   _|_   _| |   | ____|  / _ \\|  ___|\n"
        "|  _ \\ / _ \\ | |   | | | |   |  _|   | | | | |_   \n"
        "| |_) / ___ \\| |   | | | |___| |___  | |_| |  _|  \n"
        "|____/_/___\\_\\_|__ |_|_|_____|_____|  \\___/|_|    \n"
        "| ____|_   _| ____|  _ \\|_ _/ _ \\| \\ | |          \n"
        "|  _|   | | |  _| | |_) || | | | |  \\| |          \n"
        "| |___  | | | |___|  _ < | | |_| | |\\  |          \n"
        "|_____| |_| |_____|_| \\_\\___\\___/|_| \\_|         \n"
    );
}

// ===== MENU UTAMA (sebelum login) =====
void menu_auth() {
    print_banner();
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Exit\n");
    printf("Choice: ");
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;  // non-blocking read
    raw.c_cc[VTIME] = 1; // timeout 0.1 detik
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void print_battle_screen(Arena *arena, int slot, 
                         int am_player1, const char *my_name) {
    // Tentukan index kita dan lawan
    int my_hp  = am_player1 ? 
                 arena->battles[slot].hp1 : 
                 arena->battles[slot].hp2;
    int opp_hp = am_player1 ? 
                 arena->battles[slot].hp2 : 
                 arena->battles[slot].hp1;
    char *opp_name = am_player1 ?
                     arena->battles[slot].player2 :
                     arena->battles[slot].player1;

    // Clear screen
    printf("\033[2J\033[H");
    printf("========= ARENA =========\n");
    printf("%-15s HP: %d/100\n", my_name, my_hp);
    printf("         VS\n");
    printf("%-15s HP: %d/100\n", opp_name, opp_hp);
    printf("=========================\n");
    printf("[a] Attack  [u] Ultimate\n");
}

void do_battle(int slot, int am_player1, 
               const char *username, int player_idx) {
    enable_raw_mode();

    time_t last_attack = 0; // cooldown tracker

    while (arena->battles[slot].active) {
        print_battle_screen(arena, slot, am_player1, username);

        // Baca input (non-blocking)
        char c = 0;
        read(STDIN_FILENO, &c, 1);

        if (c == 'a') {
            // Cek cooldown 1 detik
            time_t now = time(NULL);
            if (now - last_attack < 1) {
                continue; // masih cooldown
            }
            last_attack = now;

            // Hitung damage
            int total_xp = arena->players[player_idx].xp;
            int weapon_bonus = arena->players[player_idx].weapon_bonus_dmg;
            int damage = BASE_DAMAGE + (total_xp / 50) + weapon_bonus;

            // Kirim attack ke orion
            char msg_text[256];
            MsgBuf reply;
            snprintf(msg_text, sizeof(msg_text),
                     "ATTACK:%s:%d:%d:%ld",
                     username, slot, damage, my_pid);
            send_to_orion(msg_text, &reply);

        } else if (c == 'u') {
            // Ultimate — hanya kalau punya senjata
            if (arena->players[player_idx].weapon_bonus_dmg == 0) {
                continue; // tidak punya senjata
            }

            int total_xp = arena->players[player_idx].xp;
            int weapon_bonus = arena->players[player_idx].weapon_bonus_dmg;
            int base_dmg = BASE_DAMAGE + (total_xp / 50) + weapon_bonus;
            int ult_damage = base_dmg * 3; // Ultimate = damage * 3

            char msg_text[256];
            MsgBuf reply;
            snprintf(msg_text, sizeof(msg_text),
                     "ATTACK:%s:%d:%d:%ld",
                     username, slot, ult_damage, my_pid);
            send_to_orion(msg_text, &reply);
        }

        usleep(100000); // refresh tiap 0.1 detik
    }

    disable_raw_mode();

    // Cek hasil battle
    int my_hp = am_player1 ?
                arena->battles[slot].hp1 :
                arena->battles[slot].hp2;

    if (my_hp > 0) {
        printf("\n==== VICTORY ====\n");
    } else {
        printf("\n==== DEFEAT ====\n");
    }
    printf("Battle ended. Press [ENTER] to continue...\n");
    getchar();
}

void show_armory(const char *username, int player_idx) {
    while (1) {
        Player *p = &arena->players[player_idx];

        printf("\n==== ARMORY ====\n");
        printf("Gold: %d\n\n", p->gold);

        for (int i = 0; i < WEAPON_COUNT; i++) {
            printf("%d. %-15s %4d G  +%d Dmg\n",
                   i + 1,
                   WEAPONS[i].name,
                   WEAPONS[i].price,
                   WEAPONS[i].bonus_dmg);
        }
        printf("0. Back\n");
        printf("Choice: ");

        int choice;
        scanf("%d", &choice);
        getchar();

        if (choice == 0) break;

        if (choice < 1 || choice > WEAPON_COUNT) {
            printf("Invalid choice!\n");
            continue;
        }

        // Kirim request beli ke orion
        char msg_text[256];
        MsgBuf reply;
        snprintf(msg_text, sizeof(msg_text),
                 "BUY:%s:%d:%ld",
                 username, choice - 1, my_pid);
        send_to_orion(msg_text, &reply);

        if (strncmp(reply.mtext, "OK:", 3) == 0) {
            printf("%s\n", reply.mtext + 3);
        } else {
            printf("%s\n", reply.mtext + 5); // skip "FAIL:"
        }
    }
}

void show_history(int player_idx) {
    printf("\n==== MATCH HISTORY ====\n");
    printf("%-8s %-15s %-6s %s\n",
           "Time", "Opponent", "Result", "XP");
    printf("────────────────────────────────\n");

    int count = arena->history_count[player_idx];
    if (count == 0) {
        printf("No battle history yet!\n");
    } else {
        // Tampilkan dari yang terbaru (terbalik)
        for (int i = count - 1; i >= 0; i--) {
            BattleRecord *r = &arena->history[player_idx][i];
            printf("%-8s %-15s %-6s +%d XP\n",
                   r->time_str,
                   r->opponent,
                   r->result ? "WIN" : "LOSS",
                   r->xp_gained);
        }
    }

    printf("\nPress any key...\n");
    getchar();
}

int main() {
    my_pid = (long)getpid();

    // Coba connect ke orion
    if (!setup_ipc()) {
        return 1;
    }

    // ===== LOOP MENU AUTH =====
    char username[64], password[64];
    int logged_in = 0;
    int player_idx = -1;

    while (!logged_in) {
        menu_auth();

        int choice;
        scanf("%d", &choice);
        getchar();

        if (choice == 3) {
            printf("Goodbye!\n");
            return 0;
        }

        printf("\n");
        if (choice == 1) printf("CREATE ACCOUNT\n");
        else if (choice == 2) printf("LOGIN\n");
        else continue;

        printf("Username: ");
        fgets(username, 64, stdin);
        username[strcspn(username, "\n")] = 0;

        printf("Password: ");
        fgets(password, 64, stdin);
        password[strcspn(password, "\n")] = 0;

        // Kirim ke orion
        char msg_text[256];
        MsgBuf reply;

        if (choice == 1) {
            snprintf(msg_text, sizeof(msg_text),
                     "REGISTER:%s:%s:%ld",
                     username, password, my_pid);
        } else {
            snprintf(msg_text, sizeof(msg_text),
                     "LOGIN:%s:%s:%ld",
                     username, password, my_pid);
        }

        send_to_orion(msg_text, &reply);

        // Parse balasan
        if (strncmp(reply.mtext, "OK:", 3) == 0) {
            if (choice == 1) {
                printf("Account created!\n");
            } else {
                printf("Welcome!\n");
                logged_in = 1;
                // Cari index player
                player_idx = find_player(username);
            }
        } else {
            // FAIL:pesan error
            printf("%s\n", reply.mtext + 5); // skip "FAIL:"
        }
    }

    // ===== MENU UTAMA GAME =====
    while (1) {
        Player *p = &arena->players[player_idx];

        printf("\033[2J\033[H"); // clear screen
        printf("-------- PROFILE --------\n");
        printf("Name : %-15s Lvl : %d\n", p->username, p->level);
        printf("Gold : %-15d XP  : %d\n", p->gold, p->xp);
        printf("Weapon: %s\n", p->weapon_name);
        printf("-------------------------\n\n");
        printf("1. Battle\n");
        printf("2. Armory\n");
        printf("3. History\n");
        printf("4. Logout\n");
        printf("> Choice: ");

        int choice;
        scanf("%d", &choice);
        getchar();

        if (choice == 1) {
            // Kirim battle request
            char msg_text[256];
            MsgBuf reply;
            snprintf(msg_text, sizeof(msg_text),
                    "BATTLE:%s::%ld", username, my_pid);
            send_to_orion(msg_text, &reply);

            if (strncmp(reply.mtext, "WAIT:", 5) == 0) {
                // Masuk matchmaking
                printf("Searching for an opponent... (35s)\n");

                // Tunggu sampai dapat lawan atau timeout
                time_t start = time(NULL);
                int found = 0;

                while (time(NULL) - start < MATCHMAKING_TIMEOUT) {
                    // Cek apakah kita sudah di-match
                    MsgBuf check;
                    if (msgrcv(msg_id, &check, 
                            sizeof(check.mtext), 
                            my_pid, IPC_NOWAIT) >= 0) {
                        if (strncmp(check.mtext, 
                                "BATTLE_START:", 13) == 0) {
                            // Parse info battle
                            char opp[64];
                            int slot, hp1, hp2;
                            sscanf(check.mtext + 13, 
                                "%[^:]:%d:%d:%d",
                                opp, &slot, &hp1, &hp2);

                            int am_p1 = strcmp(
                                arena->battles[slot].player1,
                                username) == 0;

                            do_battle(slot, am_p1, 
                                    username, player_idx);
                            found = 1;
                            break;
                        }
                    }
                    printf("\rSearching... [%lds]  ",
                        time(NULL) - start);
                    fflush(stdout);
                    sleep(1);
                }

                if (!found) {
                    // Timeout → lawan bot
                    printf("\nNo opponent found. Fighting a bot!\n");
                    // (implementasi bot bisa kreasikan sendiri)
                }

            } else if (strncmp(reply.mtext, 
                            "BATTLE_START:", 13) == 0) {
                // Langsung dapat lawan
                char opp[64];
                int slot, hp1, hp2;
                sscanf(reply.mtext + 13, "%[^:]:%d:%d:%d",
                    opp, &slot, &hp1, &hp2);

                int am_p1 = strcmp(
                    arena->battles[slot].player1, 
                    username) == 0;

                do_battle(slot, am_p1, username, player_idx);
            }

        } else if (choice == 2) {
            show_armory(username, player_idx);

        } else if (choice == 3) {
            show_history(player_idx);

        } else if (choice == 4) {
            char msg_text[256];
            MsgBuf reply;
            snprintf(msg_text, sizeof(msg_text),
                    "LOGOUT:%s::%ld", username, my_pid);
            send_to_orion(msg_text, &reply);
            printf("Goodbye, %s!\n", username);
            break;
        }
    }

    shmdt(arena);
    return 0;
}
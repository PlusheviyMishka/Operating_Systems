#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#define SERVER_FIFO "server_fifo"
#define MAX_GAMES 10
#define MAX_PLAYERS 5
#define WORD_LEN 32

typedef struct {
    pid_t pid;
} Player;

typedef struct {
    char name[32];
    char secret[WORD_LEN];
    int max_players;
    int player_count;
    int current_turn;
    Player players[MAX_PLAYERS];
} Game;

Game games[MAX_GAMES];
int game_count = 0;
pthread_mutex_t games_mutex = PTHREAD_MUTEX_INITIALIZER;

int server_fd = -1;

void log_event(const char *msg) {
    FILE *f = fopen("server.log", "a");
    if (!f) return;
    fprintf(f, "[%ld] %s\n", time(NULL), msg);
    fclose(f);
}

void cleanup_and_exit(int signum) {
    if (server_fd >= 0) close(server_fd);
    unlink(SERVER_FIFO);
    printf("\nServer exiting, FIFO removed\n");
    exit(0);
}

void send_to_client(pid_t pid, const char *msg) {
    char fifo[64];
    snprintf(fifo, sizeof(fifo), "client_%d_fifo", pid);
    int fd = open(fifo, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        close(fd);
    } else {
        char logbuf[128];
        snprintf(logbuf, sizeof(logbuf), "Failed to send msg to client %d\n", pid);
        log_event(logbuf);
    }
}

void broadcast_to_game(Game *g, const char *msg) {
    for (int i = 0; i < g->player_count; i++) {
        send_to_client(g->players[i].pid, msg);
    }
}

void random_word(char *word) {
    FILE *f = fopen("words.txt", "r");
    if (!f) {
        strcpy(word, "apple"); // fallback
        return;
    }
    char words[100][WORD_LEN];
    int count = 0;
    while (fscanf(f, "%31s", words[count]) != EOF && count < 100)
        count++;
    fclose(f);
    srand(time(NULL) ^ getpid());
    strcpy(word, words[rand() % count]);
}

Game *find_game(const char *name) {
    for (int i = 0; i < game_count; i++)
        if (!strcmp(games[i].name, name))
            return &games[i];
    return NULL;
}

void remove_player(Game *g, pid_t pid) {
    int removed_index = -1;
    for (int i = 0; i < g->player_count; i++) {
        if (g->players[i].pid == pid) {
            removed_index = i;
            break;
        }
    }
    if (removed_index == -1)
        return;
    for (int j = removed_index; j < g->player_count - 1; j++)
        g->players[j] = g->players[j + 1];
    g->player_count--;
    if (g->current_turn >= g->player_count)
        g->current_turn = 0;
}

void bulls_cows(const char *secret, const char *guess, int *b, int *c) {
    *b = *c = 0;
    int len = (int)strlen(secret);
    for (int i = 0; i < len; i++) {
        if (guess[i] == secret[i]) (*b)++;
        else {
            for (int j = 0; j < len; j++) {
                if (j != i && guess[i] == secret[j]) {
                    (*c)++;
                    break;
                }
            }
        }
    }
}

int main() {
    signal(SIGINT, cleanup_and_exit);

    unlink(SERVER_FIFO);
    if (mkfifo(SERVER_FIFO, 0666) < 0) {
        perror("mkfifo");
        return 1;
    }

    server_fd = open(SERVER_FIFO, O_RDONLY | O_NONBLOCK);
    if (server_fd < 0) {
        perror("open server fifo");
        unlink(SERVER_FIFO);
        return 1;
    }

    fd_set rfds;
    char buf[256];
    log_event("Server started");

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);

        int ret = select(server_fd + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select");
            break;
        }

        if (!FD_ISSET(server_fd, &rfds))
            continue;

        int n = read(server_fd, buf, sizeof(buf) - 1);
        if (n <= 0)
            continue;

        buf[n] = '\0';

        char game_name[32], word[WORD_LEN];
        pid_t pid;
        int max_players;

        pthread_mutex_lock(&games_mutex);

        if (sscanf(buf, "CREATE %31s %d %d", game_name, &max_players, &pid) == 3) {
            if (game_count >= MAX_GAMES) {
                send_to_client(pid, "Max games reached\n");
                pthread_mutex_unlock(&games_mutex);
                continue;
            }
            if (max_players < 2 || max_players > MAX_PLAYERS) {
                send_to_client(pid, "Invalid max players (2-5)\n");
                pthread_mutex_unlock(&games_mutex);
                continue;
            }
            if (find_game(game_name) != NULL) {
                send_to_client(pid, "Game name taken\n");
                pthread_mutex_unlock(&games_mutex);
                continue;
            }

            Game *g = &games[game_count++];
            strcpy(g->name, game_name);
            random_word(g->secret);
            g->max_players = max_players;
            g->player_count = 1;
            g->current_turn = 0;
            g->players[0].pid = pid;

            send_to_client(pid, "Game created successfully\n");
            log_event("Game created");
        }
        else if (sscanf(buf, "JOIN %31s %d", game_name, &pid) == 2) {
            Game *g = find_game(game_name);
            if (!g) {
                send_to_client(pid, "Game not found\n");
            } else if (g->player_count >= g->max_players) {
                send_to_client(pid, "Game is full\n");
            } else {
                g->players[g->player_count++].pid = pid;
                send_to_client(pid, "Joined game successfully\n");
                char msg[64];
                snprintf(msg, sizeof(msg), "Player %d joined the game.\n", pid);
                broadcast_to_game(g, msg);
                log_event("Player joined");
            }
        }
        else if (sscanf(buf, "GUESS %31s %d %31s", game_name, &pid, word) == 3) {
            Game *g = find_game(game_name);
            if (g) {
                if (g->player_count == 0) {
                    send_to_client(pid, "Game is empty\n");
                }
                else if (g->players[g->current_turn].pid != pid) {
                    send_to_client(pid, "Not your turn\n");
                }
                else if ((int)strlen(word) != (int)strlen(g->secret)) {
                    send_to_client(pid, "Wrong word length\n");
                }
                else {
                    int b, c;
                    bulls_cows(g->secret, word, &b, &c);
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Player %d guessed \"%s\": Bulls=%d, Cows=%d\n", pid, word, b, c);
                    broadcast_to_game(g, msg);

                    if (b == (int)strlen(g->secret)) {
                        snprintf(msg, sizeof(msg), "Player %d WINS the game!\n", pid);
                        broadcast_to_game(g, msg);
                        log_event("Game won");
                    }

                    g->current_turn = (g->current_turn + 1) % g->player_count;
                }
            } else {
                send_to_client(pid, "Game not found\n");
            }
        }
        else if (sscanf(buf, "EXIT %31s %d", game_name, &pid) == 2) {
            Game *g = find_game(game_name);
            if (g) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Player %d exited the game.\n", pid);
                remove_player(g, pid);
                broadcast_to_game(g, msg);
                log_event("Player exited");
            }
        }
        else if (sscanf(buf, "LIST %d", &pid) == 1) {
            if (game_count == 0) {
                send_to_client(pid, "No active games\n");
            } else {
                char msg[1024] = "Active games:\n";
                for (int i = 0; i < game_count; i++) {
                    char line[128];
                    snprintf(line, sizeof(line), "Game: %s Players: %d/%d\n", games[i].name, games[i].player_count, games[i].max_players);
                    strncat(msg, line, sizeof(msg) - strlen(msg) - 1);
                }
                send_to_client(pid, msg);
            }
        }

        pthread_mutex_unlock(&games_mutex);
    }

    close(server_fd);
    unlink(SERVER_FIFO);
    return 0;
}

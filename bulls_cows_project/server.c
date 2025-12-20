
#define _POSIX_C_SOURCE 200809L

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
#include <errno.h>

static void sleep_ms(long ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        // continue sleeping for remaining time
    }
}

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

static Game games[MAX_GAMES];
static int game_count = 0;
static pthread_mutex_t games_mutex = PTHREAD_MUTEX_INITIALIZER;

static int server_fd = -1;

// Main loop + background threads stop when this flag becomes 0.
static volatile sig_atomic_t running = 1;

// If shutdown requested by a signal, we store it here for logging in main().
static volatile sig_atomic_t shutdown_signal = 0;

static pthread_t reaper_thread;
static int reaper_started = 0;

static void log_event(const char *msg) {
    FILE *f = fopen("server.log", "a");
    if (!f) return;

    time_t now = time(NULL);
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    char dt[32];
    if (strftime(dt, sizeof(dt), "%Y-%m-%dT%H:%M:%S", &tm_local) == 0) {
        // Fallback: should be very rare
        fprintf(f, "[%ld] %s\n", (long)now, msg);
        fclose(f);
        return;
    }

    // %z produces +hhmm / -hhmm. Convert to +hh:mm for ISO-8601 readability.
    char tz_raw[8];
    tz_raw[0] = 0;
    (void)strftime(tz_raw, sizeof(tz_raw), "%z", &tm_local);

    char tz_iso[8];
    tz_iso[0] = 0;
    if (strlen(tz_raw) == 5) {
        tz_iso[0] = tz_raw[0];
        tz_iso[1] = tz_raw[1];
        tz_iso[2] = tz_raw[2];
        tz_iso[3] = ':';
        tz_iso[4] = tz_raw[3];
        tz_iso[5] = tz_raw[4];
        tz_iso[6] = 0;
    }

    fprintf(f, "[%s%s] %s\n", dt, tz_iso[0] ? tz_iso : "", msg);
    fclose(f);
}




// Best-effort cleanup for normal exit and most crashes.
// NOTE: cleanup cannot be guaranteed for SIGKILL (kill -9) or power loss.
static void server_cleanup(void) {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    unlink(SERVER_FIFO);
}

static void server_crash_handler(int sig) {
    // Async-signal-safe best effort: close fd then exit immediately.
    // We deliberately do NOT unlink the FIFO here (not async-signal-safe).
    // The next server start unlinks SERVER_FIFO before mkfifo().
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    _exit(128 + sig);
}


static void server_graceful_handler(int sig) {
    shutdown_signal = sig;
    running = 0;

    // Wake select(): close is async-signal-safe.
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
}

static void install_server_handlers(void) {
    atexit(server_cleanup);

    // Graceful-ish exits (stop loops, then main joins threads & cleans up)
    signal(SIGINT,  server_graceful_handler);
    signal(SIGTERM, server_graceful_handler);
    signal(SIGHUP,  server_graceful_handler);
    signal(SIGQUIT, server_graceful_handler);

    // Common crash signals (best-effort cleanup; cannot guarantee)
    signal(SIGSEGV, server_crash_handler);
    signal(SIGABRT, server_crash_handler);
    signal(SIGFPE,  server_crash_handler);
    signal(SIGILL,  server_crash_handler);
#ifdef SIGBUS
    signal(SIGBUS,  server_crash_handler);
#endif

    // Don't die if we write to a broken pipe/FIFO.
    signal(SIGPIPE, SIG_IGN);
}

static void remove_game_by_index(int idx) {
    if (idx < 0 || idx >= game_count) return;
    for (int i = idx; i < game_count - 1; i++) {
        games[i] = games[i + 1];
    }
    game_count--;
}

static void prune_empty_games_locked(void) {
    // Must be called with games_mutex held.
    for (int i = 0; i < game_count; ) {
        if (games[i].player_count <= 0) {
            char logbuf[128];
            snprintf(logbuf, sizeof(logbuf), "Game '%s' ended (no players)", games[i].name);
            log_event(logbuf);
            remove_game_by_index(i);
            continue;
        }
        i++;
    }
}

static Game *find_game(const char *name) {
    for (int i = 0; i < game_count; i++) {
        if (!strcmp(games[i].name, name)) return &games[i];
    }
    return NULL;
}

static int player_index(Game *g, pid_t pid) {
    for (int i = 0; i < g->player_count; i++) {
        if (g->players[i].pid == pid) return i;
    }
    return -1;
}

static void remove_player(Game *g, pid_t pid) {
    int idx = player_index(g, pid);
    if (idx < 0) return;

    for (int j = idx; j < g->player_count - 1; j++) {
        g->players[j] = g->players[j + 1];
    }

    g->player_count--;
    if (g->player_count <= 0) {
        g->current_turn = 0;
        return;
    }

    // If the removed player was before current_turn, shift current_turn left.
    if (idx < g->current_turn) {
        g->current_turn--;
        if (g->current_turn < 0) g->current_turn = 0;
    }

    if (g->current_turn >= g->player_count) {
        g->current_turn = 0;
    }
}

static void random_word(char *word) {
    FILE *f = fopen("words.txt", "r");
    if (!f) {
        strcpy(word, "apple");
        return;
    }

    char words[100][WORD_LEN];
    int count = 0;
    while (count < 100 && fscanf(f, "%31s", words[count]) == 1) {
        count++;
    }
    fclose(f);

    if (count == 0) {
        strcpy(word, "apple");
        return;
    }

    strcpy(word, words[rand() % count]);
}

// Returns 0 on success, -1 on failure.
static int send_to_client(pid_t pid, const char *msg) {
    char fifo[64];
    snprintf(fifo, sizeof(fifo), "client_%d_fifo", pid);

    int fd = open(fifo, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        (void)write(fd, msg, strlen(msg));
        close(fd);
        return 0;
    }

    // If open() fails with ENOENT/ENXIO, the client FIFO is missing or has no reader
    // (common when client crashed). Treat as dead.
    if (errno == ENOENT || errno == ENXIO) {
        (void)unlink(fifo); // best-effort cleanup of stale FIFO
    }

    char logbuf[160];
    snprintf(logbuf, sizeof(logbuf), "Failed to send to client %d (errno=%d)", pid, errno);
    log_event(logbuf);
    return -1;
}

static void broadcast_to_game(Game *g, const char *msg) {
    // If a client disappeared (Ctrl+C, crash), its FIFO will be gone.
    // We remove such players to keep game state consistent.
    pid_t dead[MAX_PLAYERS];
    int dead_count = 0;

    for (int i = 0; i < g->player_count; i++) {
        if (send_to_client(g->players[i].pid, msg) < 0) {
            dead[dead_count++] = g->players[i].pid;
        }
    }

    for (int i = 0; i < dead_count; i++) {
        remove_player(g, dead[i]);
    }
}

static void bulls_cows(const char *secret, const char *guess, int *b, int *c) {
    *b = *c = 0;
    int len = (int)strlen(secret);

    for (int i = 0; i < len; i++) {
        if (guess[i] == secret[i]) {
            (*b)++;
        } else {
            for (int j = 0; j < len; j++) {
                if (j != i && guess[i] == secret[j]) {
                    (*c)++;
                    break;
                }
            }
        }
    }
}

// Reaper: periodically removes disconnected clients and prunes empty games.
// Runs in a separate thread and exits when `running` becomes 0.
static int client_fifo_has_reader(pid_t pid) {
    char fifo[64];
    snprintf(fifo, sizeof(fifo), "client_%d_fifo", pid);

    int fd = open(fifo, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        close(fd);
        return 1;
    }

    // ENOENT: fifo doesn't exist, ENXIO: exists but no reader
    if (errno == ENOENT || errno == ENXIO) return 0;

    // On other errors, assume alive to avoid false kicks.
    return 1;
}

static void *reaper_thread_func(void *arg) {
    (void)arg;

    while (running) {
        // Sleep up to ~2 seconds, but remain responsive to shutdown.
        for (int i = 0; i < 20 && running; i++) {
            sleep_ms(100);
        }
        if (!running) break;

        pthread_mutex_lock(&games_mutex);
        for (int gi = 0; gi < game_count; gi++) {
            Game *g = &games[gi];
            for (int pi = 0; pi < g->player_count; ) {
                pid_t pid = g->players[pi].pid;
                if (!client_fifo_has_reader(pid)) {
                    char logbuf[160];
                    snprintf(logbuf, sizeof(logbuf), "Player %d removed (disconnected)", pid);
                    log_event(logbuf);
                    remove_player(g, pid);
                    continue; // keep same pi
                }
                pi++;
            }
        }
        prune_empty_games_locked();
        pthread_mutex_unlock(&games_mutex);
    }

    return NULL;
}


static void handle_command(const char *cmdline) {
    if (!cmdline) return;

    // Trim leading whitespace
    while (*cmdline == ' ' || *cmdline == '\t' || *cmdline == '\r') cmdline++;
    if (*cmdline == '\0') return;

    char buf[256];
    strncpy(buf, cmdline, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Trim trailing CR (in case of \r\n)
    size_t blen = strlen(buf);
    if (blen > 0 && buf[blen - 1] == '\r') buf[blen - 1] = '\0';

    char game_name[32], word[WORD_LEN];
    pid_t pid;
    int max_players;

    pthread_mutex_lock(&games_mutex);

    if (sscanf(buf, "CREATE %31s %d %d", game_name, &max_players, &pid) == 3) {
        if (game_count >= MAX_GAMES) {
            send_to_client(pid, "Max games reached\n");
            pthread_mutex_unlock(&games_mutex);
            return;
        }
        if (max_players < 1 || max_players > MAX_PLAYERS) {
            send_to_client(pid, "Invalid max players (1-5)\n");
            pthread_mutex_unlock(&games_mutex);
            return;
        }
        if (find_game(game_name) != NULL) {
            send_to_client(pid, "Game name taken\n");
            pthread_mutex_unlock(&games_mutex);
            return;
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
        } else if (player_index(g, pid) >= 0) {
            send_to_client(pid, "Already in this game\n");
        } else if (g->player_count >= g->max_players) {
            send_to_client(pid, "Game is full\n");
        } else {
            g->players[g->player_count++].pid = pid;
            send_to_client(pid, "Joined game successfully\n");

            char msg[96];
            snprintf(msg, sizeof(msg), "Player %d joined the game.\n", pid);
            broadcast_to_game(g, msg);
            log_event("Player joined");
        }
    }
    else if (sscanf(buf, "GUESS %31s %d %31s", game_name, &pid, word) == 3) {
        Game *g = find_game(game_name);
        if (!g) {
            send_to_client(pid, "Game not found\n");
        } else {
            int idx = player_index(g, pid);
            if (idx < 0) {
                send_to_client(pid, "You are not in this game\n");
            }
            else if (g->player_count <= 0) {
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
                    broadcast_to_game(g, "Game ended.\n");
                    log_event("Game won");

                    int gi = (int)(g - games);
                    remove_game_by_index(gi);
                    prune_empty_games_locked();
                    pthread_mutex_unlock(&games_mutex);
                    return;
                }

                if (g->player_count > 0) {
                    g->current_turn = (g->current_turn + 1) % g->player_count;
                } else {
                    g->current_turn = 0;
                }
            }
        }
    }
    else if (sscanf(buf, "EXIT %31s %d", game_name, &pid) == 2) {
        Game *g = find_game(game_name);
        if (g) {
            if (player_index(g, pid) < 0) {
                send_to_client(pid, "You are not in this game\n");
            } else {
                char msg[96];
                snprintf(msg, sizeof(msg), "Player %d exited the game.\n", pid);
                remove_player(g, pid);
                broadcast_to_game(g, msg);
                log_event("Player exited");
            }
        }
        prune_empty_games_locked();
    }
    else if (sscanf(buf, "LIST %d", &pid) == 1) {
        if (game_count == 0) {
            send_to_client(pid, "No active games\n");
        } else {
            char msg[1024] = "Active games:\n";
            for (int i = 0; i < game_count; i++) {
                char line[128];
                snprintf(line, sizeof(line), "Game: %s Players: %d/%d\n",
                         games[i].name, games[i].player_count, games[i].max_players);
                strncat(msg, line, sizeof(msg) - strlen(msg) - 1);
            }
            send_to_client(pid, msg);
        }
    }

    prune_empty_games_locked();

    pthread_mutex_unlock(&games_mutex);
}

int main(void) {
    install_server_handlers();

    // Seed RNG once (POSIX-style): random_word() should not reseed each call.
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    // If last run crashed and FIFO is still present
    unlink(SERVER_FIFO);

    if (mkfifo(SERVER_FIFO, 0666) < 0) {
        perror("mkfifo");
        return 1;
    }

    // Open as O_RDWR to avoid EOF/spin when there are no clients.
    server_fd = open(SERVER_FIFO, O_RDWR | O_NONBLOCK);
    if (server_fd < 0) {
        perror("open server fifo");
        unlink(SERVER_FIFO);
        return 1;
    }

    // Background cleanup so games end even if clients crash silently.
    if (pthread_create(&reaper_thread, NULL, reaper_thread_func, NULL) == 0) {
        reaper_started = 1;
    }

    log_event("Server started");

    fd_set rfds;

char rbuf[256];
char inbuf[2048];
size_t inlen = 0;

while (running) {
    if (server_fd < 0) break;

    FD_ZERO(&rfds);
    FD_SET(server_fd, &rfds);

    int ret = select(server_fd + 1, &rfds, NULL, NULL, NULL);
    if (ret < 0) {
        if (!running) break;       // shutdown requested
        if (errno == EBADF) break; // fd closed in signal handler
        perror("select");
        break;
    }

    if (!FD_ISSET(server_fd, &rfds)) continue;

    int n = read(server_fd, rbuf, sizeof(rbuf));
    if (n <= 0) continue;

    // Append into line buffer
    if (inlen + (size_t)n >= sizeof(inbuf)) {
        // Buffer overflow: drop accumulated data (protocol error / flood).
        inlen = 0;
    }
    memcpy(inbuf + inlen, rbuf, (size_t)n);
    inlen += (size_t)n;

        // Process complete lines (commands end with '\n')
        for (;;) {
            void *p = memchr(inbuf, '\n', inlen);
            if (!p) break;

            size_t linelen = (size_t)((char *)p - inbuf);
            char line[512];

            if (linelen >= sizeof(line)) {
                linelen = sizeof(line) - 1;
            }
            memcpy(line, inbuf, linelen);
            line[linelen] = '\0';

            // Shift buffer past this line + '\n'
            size_t consumed = (size_t)(((char *)p - inbuf) + 1);
            size_t remaining = inlen - consumed;
            memmove(inbuf, inbuf + consumed, remaining);
            inlen = remaining;

            handle_command(line);
        }

}


    // Stop background thread and exit cleanly.
    running = 0;
    if (reaper_started) {
        pthread_join(reaper_thread, NULL);
    }

    // Log server shutdown (graceful stop).
    if (shutdown_signal) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Server stopped (signal %d)", (int)shutdown_signal);
        log_event(msg);
    } else {
        log_event("Server stopped");
    }

    server_cleanup();
    return 0;
}

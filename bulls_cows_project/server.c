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

static volatile sig_atomic_t running = 1;

static volatile sig_atomic_t shutdown_signal = 0;

static pthread_t reaper_thread;
static int reaper_started = 0;

//функция печати в лог
static void log_event(const char *msg) {
    FILE *f = fopen("server.log", "a");
    if (!f) return;

    time_t now = time(NULL);
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    char dt[32];
    if (strftime(dt, sizeof(dt), "%Y-%m-%dT%H:%M:%S", &tm_local) == 0) {
        fprintf(f, "[%ld] %s\n", (long)now, msg);
        fclose(f);
        return;
    }

    //для вывода времени и даты
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




// notmal exit функция
static void server_cleanup(void) {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    unlink(SERVER_FIFO);
}

// в случае краша для закрытия дескрипторов и завершения работы
static void server_crash_handler(int sig) {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    _exit(128 + sig);
}

//штатное завершение
static void server_graceful_handler(int sig) {
    shutdown_signal = sig;
    running = 0;

    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
}

//реакция сервера на разные сигналы чтобы крашился нормально
static void install_server_handlers(void) {
    //завершение штатно -> закрыть файлы/удалить FIFO
    atexit(server_cleanup);

    //“Если нажали Ctrl+C (SIGINT) или серверу сказали завершиться
    //то не падать сразу вызвать server_graceful_handler
    signal(SIGINT,  server_graceful_handler);
    signal(SIGTERM, server_graceful_handler);
    signal(SIGHUP,  server_graceful_handler);
    signal(SIGQUIT, server_graceful_handler);

    //на случай разны ошибок
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

//убрать игру по индексу
static void remove_game_by_index(int idx) {
    if (idx < 0 || idx >= game_count) return;
    for (int i = idx; i < game_count - 1; i++) {
        games[i] = games[i + 1];
    }
    game_count--;
}

//проходит по списку игр и удаляет те, где не осталось игроков, предварительно записав это в лог
//испоьзование под мьютексом
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

//ищет игру по имени в массиве игр и возвращает указатель на неё, а если не нашёл — возвращает NULL.
static Game *find_game(const char *name) {
    for (int i = 0; i < game_count; i++) {
        if (!strcmp(games[i].name, name)) return &games[i];
    }
    return NULL;
}

//связывем pid с player
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

    // сдвиг хода после удаления игрока
    if (idx < g->current_turn) {
        g->current_turn--;
        if (g->current_turn < 0) g->current_turn = 0;
    }

    if (g->current_turn >= g->player_count) {
        g->current_turn = 0;
    }
}

//рандом слово, шерстит words.txt, если нет слов то "apple"
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

//Открывает FIFO конкретного клиента client_<pid>_fifo и отправляет туда сообщение.
//Если FIFO нет/никто не читает — пишет в лог и считает клиента “отвалившимся”.
// возврат 0 on success, -1 on failure.
static int send_to_client(pid_t pid, const char *msg) {
    char fifo[64];
    snprintf(fifo, sizeof(fifo), "client_%d_fifo", pid);

    int fd = open(fifo, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        (void)write(fd, msg, strlen(msg));
        close(fd);
        return 0;
    }

    if (errno == ENOENT || errno == ENXIO) {
        (void)unlink(fifo); // best-effort cleanup of stale FIFO
    }

    char logbuf[160];
    snprintf(logbuf, sizeof(logbuf), "Failed to send to client %d (errno=%d)", pid, errno);
    log_event(logbuf);
    return -1;
}

//Рассылает сообщение всем игрокам игры через send_to_client.
//Кого не удалось уведомить — добавляет в список “мертвых” и удаляет из игры.
//использует send_to_client()
static void broadcast_to_game(Game *g, const char *msg) {
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

//Считает результат попытки: 
//быки — буква на правильном месте, коровы — буква есть, но в другой позиции.
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

//Проверяет “жив ли клиент”: пытается открыть его FIFO на запись.
//Если FIFO не существует или нет читателя (ENOENT/ENXIO) — считает клиента отключённым.
static int client_fifo_has_reader(pid_t pid) {
    char fifo[64];
    snprintf(fifo, sizeof(fifo), "client_%d_fifo", pid);

    int fd = open(fifo, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        close(fd);
        return 1;
    }

    if (errno == ENOENT || errno == ENXIO) return 0;

    return 1;
}

//Фоновый поток сервера: 
//периодически проверяет игроков, удаляет отключившихся и удаляет пустые игры.
//Работает, пока running == 1.
static void *reaper_thread_func(void *arg) {
    (void)arg;

    while (running) {
        // спит примерно 2 мс , проходится по игрокам
        for (int i = 0; i < 20 && running; i++) {
            sleep_ms(100);
        }
        if (!running) break;

        //критическая секция -- игры
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


//Разбирает одну команду от клиента (CREATE/JOIN/GUESS/EXIT/LIST) и изменяет состояние игр.
//Делает все операции под games_mutex, чтобы не было гонок с reaper-потоком.
static void handle_command(const char *cmdline) {
    if (!cmdline) return;

    while (*cmdline == ' ' || *cmdline == '\t' || *cmdline == '\r') cmdline++;
    if (*cmdline == '\0') return;

    char buf[256];
    strncpy(buf, cmdline, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    
    size_t blen = strlen(buf);
    if (blen > 0 && buf[blen - 1] == '\r') buf[blen - 1] = '\0';

    char game_name[32], word[WORD_LEN];
    pid_t pid;
    int max_players;

    //лочим игры так юзаем очитску пустых игр после выхода игроков
    pthread_mutex_lock(&games_mutex);
    
    //анализ/парсинг строки
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

        //ответ клиенту строкой и лог
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
            //вещает всем сообщение
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

    //для рандома
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    //очистка старого FIFO и создание нового
    unlink(SERVER_FIFO);

    if (mkfifo(SERVER_FIFO, 0666) < 0) {
        perror("mkfifo");
        return 1;
    }

    server_fd = open(SERVER_FIFO, O_RDWR | O_NONBLOCK);
    if (server_fd < 0) {
        perror("open server fifo");
        unlink(SERVER_FIFO);
        return 1;
    }

    // Запуск reaper-потока очищает список игр даже если клиенты завершаются "тихо" -- без EXIT.
    if (pthread_create(&reaper_thread, NULL, reaper_thread_func, NULL) == 0) {
        reaper_started = 1;
    }

    log_event("Server started");

    fd_set rfds;

//временный буфер для одного read()
char rbuf[256];
//накопительный буфер, чтобы собирать команды построчно (\n)
char inbuf[2048];
size_t inlen = 0;

//loop пока работает
while (running) {
    //если закрыт дескриптор --> выход из цикла
    if (server_fd < 0) break;

    //select() ждёт, пока в server_fifo появятся данные
    //Таймаута нет (NULL) → ждём бесконечно, пока не придёт команда или сигнал.
    FD_ZERO(&rfds);
    FD_SET(server_fd, &rfds);

    /* 
    Обработка ошибок select
    Сценарии:
        running==0 → пришёл сигнал завершения → выходим
        EBADF → server_fd закрыли (например, из handler’а) → выходим
        прочие ошибки → печатаем и выходим 
    */
    int ret = select(server_fd + 1, &rfds, NULL, NULL, NULL);
    if (ret < 0) {
        if (!running) break;       
        if (errno == EBADF) break; 
        perror("select");
        break;
    }
 
    /*
    Читаем кусок байтов.
    Если ничего не прочитали — продолжаем цикл.
    */
    if (!FD_ISSET(server_fd, &rfds)) continue;

    int n = read(server_fd, rbuf, sizeof(rbuf));
    if (n <= 0) continue;

    // Добавляем прочитанное в inbuf
    if (inlen + (size_t)n >= sizeof(inbuf)) {
        inlen = 0;
    }
    memcpy(inbuf + inlen, rbuf, (size_t)n);
    inlen += (size_t)n;
    /*Это защита от ситуации, если клиент начнёт слать очень много мусора без \n*/
        
    //Разбор команд по строкам (\n)
    /*ищем первый \n → это конец команды
        копируем команду в line
        удаляем её из inbuf (memmove сдвигает остаток)
        вызываем handle_command(line) — сервер обрабатывает команду
        Цикл for(;;) продолжает, пока в inbuf есть ещё целые строки.*/
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


    // завершаем reaper
    running = 0;
    if (reaper_started) {
        pthread_join(reaper_thread, NULL);
    }

    // лог остановки сервера
    if (shutdown_signal) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Server stopped (signal %d)", (int)shutdown_signal);
        log_event(msg);
    } else {
        log_event("Server stopped");
    }
    
    //вызываем завершение сервера --> закрыть fd и удалить FIFO
    server_cleanup();
    return 0;
}

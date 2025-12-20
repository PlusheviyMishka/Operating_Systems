#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define SERVER_FIFO "server_fifo"
#define MAX_PLAYERS 5

static int server_fd = -1;
static int client_fd = -1;

static char game[32] = {0};
static pid_t pid;
static char client_fifo_path[64] = {0};

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t list_running = 0;
static volatile sig_atomic_t game_over = 0;

static pthread_t main_thread_id;

static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

// синхронизация для CREATE/JOIN result
static pthread_mutex_t resp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  resp_cond  = PTHREAD_COND_INITIALIZER;
static int waiting_create = 0;
static int waiting_join = 0;
static int last_result = 0; // 0=unknown, 1=success, -1=failure

//пресозд EXIT message для SIGINT handler
static char exit_msg[128] = {0};
static volatile sig_atomic_t exit_msg_len = 0;
static volatile sig_atomic_t have_game = 0;

//печать с использованием мьютекса для защиты
static void safe_print(const char *msg) {
    pthread_mutex_lock(&print_mutex);
    fputs(msg, stdout);
    //сразу скидываем 
    fflush(stdout);
    pthread_mutex_unlock(&print_mutex);
}

//очистка fifo, закрытие дескрипторов
static void client_cleanup(void) {
    if (client_fd >= 0) {
        close(client_fd);
        client_fd = -1;
    }
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    if (client_fifo_path[0] != '\0') {
        unlink(client_fifo_path);
    }
}

static void on_signal(int sig) {
    (void)sig;
    running = 0;
    list_running = 0;

    // сообщ что выходим из игры
    if (have_game && server_fd >= 0 && exit_msg_len > 0) {
        (void)write(server_fd, exit_msg, (size_t)exit_msg_len);
    }

    client_cleanup();
    _exit(128 + sig);
}


static void wake_stdin_handler(int sig) {
    (void)sig;
}

static void install_client_handlers(void) {
    atexit(client_cleanup);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP,  on_signal);
    signal(SIGQUIT, on_signal);

    // crash signals
    signal(SIGSEGV, on_signal);
    signal(SIGABRT, on_signal);
    signal(SIGFPE,  on_signal);
    signal(SIGILL,  on_signal);
#ifdef SIGBUS
    signal(SIGBUS,  on_signal);
#endif

    
    signal(SIGPIPE, SIG_IGN);

    // для потока читателя если игра закончится
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = wake_stdin_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    sigaction(SIGUSR1, &sa, NULL);
}

static void maybe_signal_waiters(const char *msg) {
    // вызов из reader thread.
    pthread_mutex_lock(&resp_mutex);

    if (waiting_create) {
        if (strstr(msg, "Game created successfully") != NULL) {
            last_result = 1;
            waiting_create = 0;
            pthread_cond_signal(&resp_cond);
        } else if (strstr(msg, "Max games") || strstr(msg, "Invalid") || strstr(msg, "taken")) {
            last_result = -1;
            waiting_create = 0;
            pthread_cond_signal(&resp_cond);
        }
    }

    if (waiting_join) {
        if (strstr(msg, "Joined game successfully") != NULL) {
            last_result = 1;
            waiting_join = 0;
            pthread_cond_signal(&resp_cond);
        } else if (strstr(msg, "Game is full") || strstr(msg, "Game not found") || strstr(msg, "Already in") ) {
            last_result = -1;
            waiting_join = 0;
            pthread_cond_signal(&resp_cond);
        }
    }

    pthread_mutex_unlock(&resp_mutex);
}

static void maybe_handle_game_over(const char *msg) {
    if (game_over) return;

    // Server транслирует это если игра finished.
    if (strstr(msg, "Game ended") != NULL || strstr(msg, "WINS the game") != NULL) {
        game_over = 1;
        have_game = 0;
        exit_msg_len = 0;
        list_running = 0;

        safe_print("\n[Client] Game finished. Exiting input mode...\n");

        // на случай блока reader thread.
        pthread_kill(main_thread_id, SIGUSR1);
    }
}

static void *read_thread_func(void *arg) {
    (void)arg;
    char buf[256];

    while (running) {
        int n = read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';

            maybe_signal_waiters(buf);
            maybe_handle_game_over(buf);

            pthread_mutex_lock(&print_mutex);
            printf("\r%s\n> ", buf);
            fflush(stdout);
            pthread_mutex_unlock(&print_mutex);
        } else {
            usleep(100000);
        }
    }

    return NULL;
}

//print список игр с промежутком
static void *list_thread_func(void *arg) {
    (void)arg;
    char cmd[64];

    while (running && list_running) {
        int len = snprintf(cmd, sizeof(cmd), "LIST %d\n", pid);
        if (len > 0 && server_fd >= 0) {
            (void)write(server_fd, cmd, (size_t)len);
        }

        // sleep 2 seconds but allow quick stop
        for (int i = 0; i < 20 && running && list_running; i++) {
            usleep(1000000);
        }
    }

    return NULL;
}

static int wait_result(int is_join) {
    // return 1 success, 0 failure/timeout
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5; 

    pthread_mutex_lock(&resp_mutex);
    last_result = 0;
    if (is_join) waiting_join = 1;
    else waiting_create = 1;

    while (last_result == 0) {
        int rc = pthread_cond_timedwait(&resp_cond, &resp_mutex, &ts);
        if (rc == ETIMEDOUT) break;
    }

    int ok = (last_result == 1);

    // если timed out, не ждет флаги.
    if (is_join) waiting_join = 0;
    else waiting_create = 0;

    pthread_mutex_unlock(&resp_mutex);
    return ok;
}

int main(void) {
    pid = getpid();
    install_client_handlers();

    main_thread_id = pthread_self();

    snprintf(client_fifo_path, sizeof(client_fifo_path), "client_%d_fifo", pid);

    // на случай краша пред клиента закрываем открытый fifo
    unlink(client_fifo_path);

    if (mkfifo(client_fifo_path, 0666) < 0) {
        perror("mkfifo client");
        return 1;
    }
    
    //пишем серверу
    server_fd = open(SERVER_FIFO, O_WRONLY | O_NONBLOCK);
    if (server_fd < 0) {
        perror("open server fifo");
        client_cleanup();
        return 1;
    }
    
    //сервер отвечает нам лично
    client_fd = open(client_fifo_path, O_RDONLY | O_NONBLOCK);
    if (client_fd < 0) {
        perror("open client fifo");
        client_cleanup();
        return 1;
    }
    
    //для разделения ввода пользователя и вывода в реальном времени инфы от сервера
    pthread_t reader_thread;
    pthread_create(&reader_thread, NULL, read_thread_func, NULL);

    int choice;
    safe_print("1. Create game\n2. Join game\n> ");
    if (scanf("%d", &choice) != 1) {
        running = 0;
        pthread_join(reader_thread, NULL);
        client_cleanup();
        return 1;
    }

    pthread_t list_thread;
    int list_thread_started = 0;

    // анализ ввода и создания msg
    if (choice == 1) {
        int max;
        safe_print("Game name: ");
        scanf("%31s", game);
        safe_print("Max players (1-5): ");
        scanf("%d", &max);

        if (max < 1 || max > MAX_PLAYERS) {
            safe_print("Invalid number of players.\n");
            running = 0;
            pthread_join(reader_thread, NULL);
            client_cleanup();
            return 1;
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "CREATE %s %d %d\n", game, max, pid);
        (void)write(server_fd, msg, strlen(msg));

        if (!wait_result(0)) {
            safe_print("Create failed (or timed out).\n");
            running = 0;
            pthread_join(reader_thread, NULL);
            client_cleanup();
            return 1;
        }

        exit_msg_len = (sig_atomic_t)snprintf(exit_msg, sizeof(exit_msg), "EXIT %s %d\n", game, pid);
        have_game = 1;
    }
    else if (choice == 2) {
        safe_print("\nAvailable games list refreshes every 20 seconds.\n");

        list_running = 1;
        pthread_create(&list_thread, NULL, list_thread_func, NULL);
        list_thread_started = 1;

        safe_print("Enter game name to join: ");
        scanf("%31s", game);

        list_running = 0;
        pthread_join(list_thread, NULL);

        char msg[128];
        snprintf(msg, sizeof(msg), "JOIN %s %d\n", game, pid);
        (void)write(server_fd, msg, strlen(msg));

        if (!wait_result(1)) {
            safe_print("Join failed (or timed out).\n");
            running = 0;
            pthread_join(reader_thread, NULL);
            client_cleanup();
            return 1;
        }

        exit_msg_len = (sig_atomic_t)snprintf(exit_msg, sizeof(exit_msg), "EXIT %s %d\n", game, pid);
        have_game = 1;
    }
    else {
        safe_print("Invalid choice\n");
        running = 0;
        pthread_join(reader_thread, NULL);
        client_cleanup();
        return 1;
    }

    // clear stdin buffer
    int c; while ((c = getchar()) != '\n' && c != EOF) {}

    
    char word[32];
    while (running && !game_over) {
        safe_print("> ");
        if (!fgets(word, sizeof(word), stdin)) {
            // если игра закончена просто выход из loop.
            if (game_over) break;
            if (errno == EINTR) continue;
            break;
        }

        word[strcspn(word, "\n")] = 0;

        if (game_over) break;
        
        if (strcmp(word, "EXIT") == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "EXIT %s %d\n", game, pid);
            (void)write(server_fd, msg, strlen(msg));
            break;
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "GUESS %s %d %s\n", game, pid, word);
        (void)write(server_fd, msg, strlen(msg));
    }

    running = 0;
    if (list_thread_started) {
        list_running = 0;
        pthread_join(list_thread, NULL);
    }
    pthread_join(reader_thread, NULL);

    client_cleanup();
    return 0;
}

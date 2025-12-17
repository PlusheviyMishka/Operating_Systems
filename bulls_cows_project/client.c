#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>

#define SERVER_FIFO "server_fifo"
#define MAX_PLAYERS 5

int server_fd;
int client_fd;
char game[32];
pid_t pid;

volatile int running = 1;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

void safe_print(const char *msg) {
    pthread_mutex_lock(&print_mutex);
    printf("%s", msg);
    fflush(stdout);
    pthread_mutex_unlock(&print_mutex);
}

void *read_thread_func(void *arg) {
    char buf[256];
    while (running) {
        int n = read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            pthread_mutex_lock(&print_mutex);
            printf("\r%s\n> ", buf);
            fflush(stdout);
            pthread_mutex_unlock(&print_mutex);
        } else {
            usleep(100000); // 100 ms sleep to reduce CPU load
        }
    }
    return NULL;
}

int main() {
    pid = getpid();
    char fifo[64];
    snprintf(fifo, sizeof(fifo), "client_%d_fifo", pid);
    mkfifo(fifo, 0666);

    server_fd = open(SERVER_FIFO, O_WRONLY);
    if (server_fd < 0) {
        perror("open server fifo");
        unlink(fifo);
        return 1;
    }
    client_fd = open(fifo, O_RDONLY | O_NONBLOCK);
    if (client_fd < 0) {
        perror("open client fifo");
        close(server_fd);
        unlink(fifo);
        return 1;
    }

    pthread_t reader_thread;
    pthread_create(&reader_thread, NULL, read_thread_func, NULL);

    int choice;
    safe_print("1. Create game\n2. Join game\n> ");
    scanf("%d", &choice);

    if (choice == 1) {
        int max;
        safe_print("Game name: ");
        scanf("%31s", game);
        safe_print("Max players (2-5): ");
        scanf("%d", &max);
        if (max < 2 || max > MAX_PLAYERS) {
            safe_print("Invalid number of players.\n");
            running = 0;
            pthread_join(reader_thread, NULL);
            close(server_fd);
            close(client_fd);
            unlink(fifo);
            return 1;
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "CREATE %s %d %d", game, max, pid);
        write(server_fd, msg, strlen(msg));
    }
    else if (choice == 2) {
        char list_cmd[64];
        snprintf(list_cmd, sizeof(list_cmd), "LIST %d", pid);
        write(server_fd, list_cmd, strlen(list_cmd));

        sleep(1); // wait a bit for server response

        safe_print("\nEnter game name to join: ");
        scanf("%31s", game);

        char msg[128];
        snprintf(msg, sizeof(msg), "JOIN %s %d", game, pid);
        write(server_fd, msg, strlen(msg));
    }
    else {
        safe_print("Invalid choice\n");
        running = 0;
        pthread_join(reader_thread, NULL);
        close(server_fd);
        close(client_fd);
        unlink(fifo);
        return 1;
    }

    // Clear stdin buffer after scanf
    int c; while ((c = getchar()) != '\n' && c != EOF) {}

    char word[32];

    while (running) {
        safe_print("> ");
        if (fgets(word, sizeof(word), stdin) == NULL) break;

        word[strcspn(word, "\n")] = 0; // remove trailing newline

        if (strcmp(word, "EXIT") == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "EXIT %s %d", game, pid);
            write(server_fd, msg, strlen(msg));
            running = 0;
            break;
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "GUESS %s %d %s", game, pid, word);
            write(server_fd, msg, strlen(msg));
        }
    }

    pthread_join(reader_thread, NULL);
    close(server_fd);
    close(client_fd);
    unlink(fifo);
    return 0;
}

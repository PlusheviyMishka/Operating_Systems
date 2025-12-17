#include "common.h"

static cmd_data_t  *cmd  = NULL;
static resp_data_t *resp = NULL;

static volatile sig_atomic_t sigusr1_seen = 0;

static void sigusr1_handler(int signum) {
    (void)signum;
    sigusr1_seen = 1;
}

static void* map_file(const char* name, size_t size, int create) {
    int fd = open(name, O_RDWR | (create ? (O_CREAT | O_TRUNC) : 0), 0600);
    if (fd < 0) return NULL;

    if (create) {
        if (ftruncate(fd, (off_t)size) == -1) {
            close(fd);
            return NULL;
        }
    }

    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return (p == MAP_FAILED) ? NULL : p;
}

static char* read_line(void) {
    size_t cap = 256, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;

    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (len + 1 >= cap) {
            char* nb = (char*)realloc(buf, cap * 2);
            if (!nb) { free(buf); return NULL; }
            buf = nb; cap *= 2;
        }
        buf[len++] = (char)c;
    }

    if (c == EOF && len == 0) { free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

static int sem_wait_intr(sem_t* s) {
    for (;;) {
        if (sem_wait(s) == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

int main(void) {
    // Обработчик SIGUSR1 
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("parent: sigaction");
        return 1;
    }

    // Создаем file-backed mapping
    cmd  = (cmd_data_t*) map_file(CMD_FILE,  sizeof(cmd_data_t),  1);
    resp = (resp_data_t*)map_file(RESP_FILE, sizeof(resp_data_t), 1);
    if (!cmd || !resp) {
        fprintf(stderr, "parent: ошибка создания mapping\n");
        return 1;
    }

    // Инициализация общей памяти (делает только parent)
    memset(cmd,  0, sizeof(*cmd));
    memset(resp, 0, sizeof(*resp));

    if (sem_init(&cmd->sem_cmd,  1, 0) == -1) { perror("sem_init sem_cmd");  return 1; }
    if (sem_init(&cmd->sem_resp, 1, 0) == -1) { perror("sem_init sem_resp"); return 1; }

    printf("Файл для результатов: ");
    fflush(stdout);
    char* fname = read_line();
    if (!fname) {
        fprintf(stderr, "parent: ошибка чтения имени файла\n");
        return 1;
    }
    strncpy(cmd->command, fname, BUF_SIZE - 1);
    cmd->command[BUF_SIZE - 1] = '\0';
    free(fname);

    // Запускаем child
    pid_t child = fork();
    if (child == -1) {
        perror("parent: fork");
        return 1;
    }
    if (child == 0) {
        execl("./child", "child", NULL);
        perror("parent: execl");
        _exit(1);
    }

    cmd->child_pid = child;

    // Ждем READY от ребенка (через семафор ответа)
    if (sem_wait_intr(&cmd->sem_resp) == -1) {
        perror("parent: sem_wait READY");
        return 1;
    }
    printf("Child says: %s\n", resp->response);

    printf("Дочерний PID: %d\n", child);
    printf("Вводите числа через пробел ('exit' для выхода):\n");

    int exit_requested = 0;

    while (!exit_requested) {
        printf("> ");
        fflush(stdout);

        char* line = read_line();
        if (!line) {
            strncpy(cmd->command, "exit", BUF_SIZE - 1);
            cmd->command[BUF_SIZE - 1] = '\0';
            exit_requested = 1;
        } else if (strlen(line) == 0) {
            free(line);
            continue;
        } else {
            strncpy(cmd->command, line, BUF_SIZE - 1);
            cmd->command[BUF_SIZE - 1] = '\0';
            free(line);
        }

        // Сообщаем ребенку что команда готова
        if (sem_post(&cmd->sem_cmd) == -1) {
            perror("parent: sem_post sem_cmd");
            break;
        }

        sigusr1_seen = 0;
        kill(child, SIGUSR1);

        // Ждем ответ
        if (sem_wait_intr(&cmd->sem_resp) == -1) {
            perror("parent: sem_wait sem_resp");
            break;
        }

        printf("Ответ: %s\n", resp->response);

        if (resp->critical) {
            printf("\n!!! ОШИБКА: ДЕЛЕНИЕ НА НОЛЬ !!!\n");
            printf("Завершение работы...\n");
            kill(child, SIGTERM);
            waitpid(child, NULL, 0);
            break;
        }

        if (strcmp(cmd->command, "exit") == 0) {
            exit_requested = 1;
            waitpid(child, NULL, 0);
        }
    }

    // очистка
    sem_destroy(&cmd->sem_cmd);
    sem_destroy(&cmd->sem_resp);

    munmap(cmd,  sizeof(*cmd));
    munmap(resp, sizeof(*resp));

    unlink(CMD_FILE);
    unlink(RESP_FILE);

    printf("Родительский процесс завершен.\n");
    return 0;
}

#include "common.h"

static cmd_data_t  *cmd  = NULL;
static resp_data_t *resp = NULL;
static FILE *out = NULL;

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

static int sem_wait_intr(sem_t* s) {
    for (;;) {
        if (sem_wait(s) == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

int main(void) {
    // SIGUSR1 уведомление
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("child: sigaction");
        return 1;
    }

    // Открывает mapping, созданные parent’ом 
    cmd  = (cmd_data_t*) map_file(CMD_FILE,  sizeof(cmd_data_t),  0);
    resp = (resp_data_t*)map_file(RESP_FILE, sizeof(resp_data_t), 0);
    if (!cmd || !resp) {
        fprintf(stderr, "child: ошибка открытия mapping\n");
        return 1;
    }

    // Открывает файл результатов 
    out = fopen(cmd->command, "w");
    if (!out) {
        snprintf(resp->response, BUF_SIZE, "ERROR: не могу открыть файл");
        resp->critical = 0;
        sem_post(&cmd->sem_resp);
        kill(getppid(), SIGUSR1);
        return 1;
    }

    fprintf(out, "Дочерний процесс PID: %d\n\n", getpid());
    fflush(out);

    // Сообщаем READY
    snprintf(resp->response, BUF_SIZE, "READY");
    resp->critical = 0;
    sem_post(&cmd->sem_resp);
    kill(getppid(), SIGUSR1);

    // Основной цикл
    while (1) {
        // ожидание команды от родителя
        if (sem_wait_intr(&cmd->sem_cmd) == -1) {
            perror("child: sem_wait sem_cmd");
            break;
        }

        // Обработка команды
        if (strcmp(cmd->command, "exit") == 0) {
            fprintf(out, "Получена команда выхода\n");
            fflush(out);

            snprintf(resp->response, BUF_SIZE, "EXIT");
            resp->critical = 0;
            sem_post(&cmd->sem_resp);
            kill(getppid(), SIGUSR1);
            break;
        }

        fprintf(out, "Команда: %s\n", cmd->command);

        // Парсим числа
        int numbers[100], count = 0;
        char buffer[BUF_SIZE];
        strncpy(buffer, cmd->command, BUF_SIZE - 1);
        buffer[BUF_SIZE - 1] = '\0';

        char* token = strtok(buffer, " \t\n");
        while (token && count < 100) {
            int valid = 1;
            for (int i = 0; token[i] != '\0'; i++) {
                if (!isdigit((unsigned char)token[i]) &&
                    !(i == 0 && (token[i] == '+' || token[i] == '-'))) {
                    valid = 0;
                    break;
                }
            }
            if (valid) numbers[count++] = atoi(token);
            token = strtok(NULL, " \t\n");
        }

        if (count < 2) {
            fprintf(out, "Ошибка: нужно минимум 2 числа\n");
            snprintf(resp->response, BUF_SIZE, "ERROR: нужно 2+ числа");
            resp->critical = 0;
        } else {
            int dividend = numbers[0];
            int error = 0;

            fprintf(out, "Вычисления:\n");
            for (int i = 1; i < count; i++) {
                if (numbers[i] == 0) {
                    fprintf(out, "ОШИБКА: деление %d на 0\n", dividend);
                    snprintf(resp->response, BUF_SIZE, "CRITICAL: деление на ноль");
                    resp->critical = 1;
                    error = 1;
                    break;
                }
                int result = (int)dividend / numbers[i];
                fprintf(out, "  %d / %d = %d\n", dividend, numbers[i], result);
                dividend = (int)result;
            }

            if (!error) {
                fprintf(out, "--- Успешно ---\n");
                snprintf(resp->response, BUF_SIZE, "OK");
                resp->critical = 0;
            }
        }

        fflush(out);

        // Отдаю ответ родителю
        sem_post(&cmd->sem_resp);
        kill(getppid(), SIGUSR1);

        if (resp->critical) break;
    }

    fclose(out);
    munmap(cmd,  sizeof(*cmd));
    munmap(resp, sizeof(*resp));
    return 0;
}

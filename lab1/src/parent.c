#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024
#define INITIAL_BUFFER_SIZE 256

// Функция для динамического чтения строки
char* read_dynamic_string(FILE* stream) {
    size_t capacity = INITIAL_BUFFER_SIZE;
    char* buffer = malloc(capacity);
    if (!buffer) return NULL;
    
    size_t pos = 0;
    int c;
    
    while ((c = fgetc(stream)) != EOF && c != '\n') {
        // Увеличиваем буфер если нужно
        if (pos >= capacity - 1) {
            capacity *= 2;
            char* new_buffer = realloc(buffer, capacity);
            if (!new_buffer) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }
        
        buffer[pos++] = (char)c;
    }
    
    // Обработка EOF без данных
    if (c == EOF && pos == 0) {
        free(buffer);
        return NULL;
    }
    
    buffer[pos] = '\0';
    return buffer;
}

int main() {
    int pipe1[2]; // Родитель -> Дочерний
    int pipe2[2]; // Дочерний -> Родитель
    pid_t pid;
    char filename[100];
    char* dynamic_buffer = NULL;
    int status;

    if (pipe(pipe1) == -1 || pipe(pipe2) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    printf("Введите имя файла для дочернего процесса: ");
    if (fgets(filename, sizeof(filename), stdin) == NULL) {
        perror("fgets");
        exit(EXIT_FAILURE);
    }
    
    filename[strcspn(filename, "\n")] = 0;

    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        close(pipe1[1]); 
        close(pipe2[0]); 
        
        dup2(pipe1[0], STDIN_FILENO);
        close(pipe1[0]);
        
        dup2(pipe2[1], STDOUT_FILENO);
        close(pipe2[1]);
        
        execl("./child", "child", filename, NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    } else {
        close(pipe1[0]); 
        close(pipe2[1]); 
        
        printf("Дочерний процесс создан. Вводите команды (числа через пробел):\n");
        
        while (1) {
            printf("> ");
            fflush(stdout);
            
            // Используем динамическое чтение вместо fgets
            dynamic_buffer = read_dynamic_string(stdin);
            if (dynamic_buffer == NULL) {
                break;
            }
            
            if (strlen(dynamic_buffer) <= 0) {
                free(dynamic_buffer);
                continue;
            }
            
            // Добавляем символ новой строки для дочернего процесса
            write(pipe1[1], dynamic_buffer, strlen(dynamic_buffer));
            write(pipe1[1], "\n", 1);
            
            free(dynamic_buffer);
            dynamic_buffer = NULL;
            
            if (waitpid(pid, &status, WNOHANG) == pid) {
                printf("Дочерний процесс завершился\n");
                break;
            }
            
            fd_set readfds;
            struct timeval timeout;
            
            FD_ZERO(&readfds);
            FD_SET(pipe2[0], &readfds);
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000; // 100ms
            
            if (select(pipe2[0] + 1, &readfds, NULL, NULL, &timeout) > 0) {
                if (FD_ISSET(pipe2[0], &readfds)) {
                    char temp_buffer[BUFFER_SIZE];
                    ssize_t bytes_read = read(pipe2[0], temp_buffer, sizeof(temp_buffer) - 1);
                    if (bytes_read > 0) {
                        temp_buffer[bytes_read] = '\0';
                        printf("Дочерний процесс: %s", temp_buffer);
                        
                        if (strstr(temp_buffer, "ERROR: Division by zero") != NULL) {
                            printf("Завершение работы из-за деления на ноль\n");
                            break;
                        }
                    }
                }
            }
        }
        
        // Освобождаем память, если она еще не освобождена
        if (dynamic_buffer != NULL) {
            free(dynamic_buffer);
        }
        
        close(pipe1[1]);
        close(pipe2[0]);
        waitpid(pid, &status, 0);
        
        printf("Родительский процесс завершен\n");
    }
    
    return 0;
}
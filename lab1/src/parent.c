#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024

int main() {
    int pipe1[2]; // Родитель -> Дочерний
    int pipe2[2]; // Дочерний -> Родитель
    pid_t pid;
    char filename[100];
    char buffer[BUFFER_SIZE];
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
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                break;
            }
            
            if (strlen(buffer) <= 1) {
                continue;
            }
            
            write(pipe1[1], buffer, strlen(buffer));
            
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
                    ssize_t bytes_read = read(pipe2[0], buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("Дочерний процесс: %s", buffer);
                        
                        if (strstr(buffer, "ERROR: Division by zero") != NULL) {
                            printf("Завершение работы из-за деления на ноль\n");
                            break;
                        }
                    }
                }
            }
        }
        
        close(pipe1[1]);
        close(pipe2[0]);
        waitpid(pid, &status, 0);
        
        printf("Родительский процесс завершен\n");
    }
    
    return 0;
}
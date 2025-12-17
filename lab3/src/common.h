#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <semaphore.h>

#define CMD_FILE  "cmd.bin"
#define RESP_FILE "resp.bin"
#define BUF_SIZE 1024

typedef struct {
    
    char  command[BUF_SIZE];
    pid_t child_pid;

    // Межпроцессные семафоры (pshared=1)
    sem_t sem_cmd;   // родитель -> ребенок (команда готова)
    sem_t sem_resp;  // ребенок -> родитель (ответ готов)
} cmd_data_t;

typedef struct {
    char response[BUF_SIZE];
    int  critical;   // 1 = критическая ошибка деление на ноль
} resp_data_t;

#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#define BUFFER_SIZE 1024
#define MAX_NUMBERS 100

int main(int argc, char *argv[]) {
    char buffer[BUFFER_SIZE];
    int numbers[MAX_NUMBERS];
    int count;
    FILE *output_file;
    
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <имя_файла>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    output_file = fopen(argv[1], "w");
    if (output_file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    
    fprintf(output_file, "=== Начало работы дочернего процесса ===\n");
    fflush(output_file);
    
    printf("Дочерний процесс готов. Файл: %s\n", argv[1]);
    fflush(stdout);
    
    while (1) {
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }
        
        count = 0;
        char *token = strtok(buffer, " \t\n");
        
        while (token != NULL && count < MAX_NUMBERS) {
            // Проверяем, что токен состоит только из цифр и знака
            int valid = 1;
            for (int i = 0; token[i] != '\0'; i++) {
                if (!isdigit(token[i]) && !(i == 0 && (token[i] == '+' || token[i] == '-'))) {
                    valid = 0;
                    break;
                }
            }
            
            if (valid) {
                numbers[count++] = atoi(token);
            }
            token = strtok(NULL, " \t\n");
        }
        
        if (count < 2) {
            fprintf(output_file, "Ошибка: нужно как минимум 2 числа\n");
            printf("ERROR: Need at least 2 numbers\n");
            fflush(output_file);
            fflush(stdout);
            continue;
        }
        
        fprintf(output_file, "Входные данные: ");
        for (int i = 0; i < count; i++) {
            fprintf(output_file, "%d ", numbers[i]);
        }
        fprintf(output_file, "\n");
        
        int dividend = numbers[0];
        int error = 0;
        
        fprintf(output_file, "Результаты деления:\n");
        
        for (int i = 1; i < count; i++) {
            if (numbers[i] == 0) {
                fprintf(output_file, "ОШИБКА: деление на ноль (%d / %d)\n", dividend, numbers[i]);
                printf("ERROR: Division by zero detected\n");
                fflush(output_file);
                fflush(stdout);
                error = 1;
                break;
            }
            
            double result = (double)dividend / numbers[i];
            fprintf(output_file, "  %d / %d = %.2f\n", dividend, numbers[i], result);
        }
        
        fprintf(output_file, "---\n");
        fflush(output_file);
        
        if (error) {
            break;
        }
        
        printf("OK: Command processed successfully\n");
        fflush(stdout);
    }
    
    fprintf(output_file, "=== Дочерний процесс завершен ===\n");
    fclose(output_file);
    
    return 0;
}
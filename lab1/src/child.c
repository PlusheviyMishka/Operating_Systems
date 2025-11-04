#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#define BUFFER_SIZE 1024
#define INITIAL_NUMBERS_CAPACITY 10

int main(int argc, char *argv[]) {
    char buffer[BUFFER_SIZE];
    int *numbers = NULL;
    int count = 0;
    int capacity = 0;
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
        if (numbers == NULL) {
            capacity = INITIAL_NUMBERS_CAPACITY;
            numbers = malloc(capacity * sizeof(int));
            if (numbers == NULL) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
        }
        
        char *token = strtok(buffer, " \t\n");
        
        while (token != NULL) {
            int valid = 1;
            for (int i = 0; token[i] != '\0'; i++) {
                if (!isdigit(token[i]) && !(i == 0 && (token[i] == '+' || token[i] == '-'))) {
                    valid = 0;
                    break;
                }
            }
            
            if (valid) {
                if (count >= capacity) {
                    capacity *= 2;
                    int *new_numbers = realloc(numbers, capacity * sizeof(int));
                    if (new_numbers == NULL) {
                        perror("realloc");
                        free(numbers);
                        exit(EXIT_FAILURE);
                    }
                    numbers = new_numbers;
                }
                numbers[count++] = atoi(token);
            }
            token = strtok(NULL, " \t\n");
        }
        
        if (count < 2) {
            fprintf(output_file, "Ошибка: нужно как минимум 2 числа\n");
            printf("ERROR: Need at least 2 numbers\n");
            fflush(output_file);
            fflush(stdout);
            
            free(numbers);
            numbers = NULL;
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
        
        free(numbers);
        numbers = NULL;
        
        if (error) {
            break;
        }
        
        printf("OK: Command processed successfully\n");
        fflush(stdout);
    }
    
    if (numbers != NULL) {
        free(numbers);
    }
    
    fprintf(output_file, "=== Дочерний процесс завершен ===\n");
    fclose(output_file);
    
    return 0;
}
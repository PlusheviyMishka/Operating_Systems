#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Прототипы функций из библиотеки
extern char* translation(long x);
extern float SinIntegral(float A, float B, float e);

int main() {
    char input[256];
    int cmd;
    
    printf("=== Программа с скомпилированными библиотеками ===\n");
    printf("Команды:\n");
    printf("  0 - выход\n");
    printf("  1 число - перевод числа\n");
    printf("  2 A B e - интеграл sin(x)\n\n");
    
    while (1) {
        printf("> ");
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        // Парсинг команды
        if (sscanf(input, "%d", &cmd) != 1) {
            printf("Ошибка: введите число команды\n");
            continue;
        }
        
        if (cmd == 0) {
            printf("Выход\n");
            break;
        }
        else if (cmd == 1) {
            long number;
            if (sscanf(input, "%*d %ld", &number) != 1) {
                printf("Ошибка: введите число после команды 1\n");
                continue;
            }
            
            char* result = translation(number);
            printf("Результат: %s\n", result);
            free(result);
        }
        else if (cmd == 2) {
            float A, B, e;
            if (sscanf(input, "%*d %f %f %f", &A, &B, &e) != 3) {
                printf("Ошибка: нужны 3 числа A B e\n");
                continue;
            }
            
            float result = SinIntegral(A, B, e);
            printf("Результат интеграла: %.6f\n", result);
        }
        else {
            printf("Неизвестная команда: %d\n", cmd);
        }
    }
    
    return 0;
}
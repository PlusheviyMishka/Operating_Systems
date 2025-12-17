#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

// Определение типов функций
typedef char* (*translation_func)(long);
typedef float (*sinintegral_func)(float, float, float);

int main() {
    char current_lib[50] = "./libimpl1.so";
    void* lib_handle = dlopen(current_lib, RTLD_LAZY);
    
    if (!lib_handle) {
        fprintf(stderr, "Ошибка загрузки библиотеки %s: %s\n", current_lib, dlerror());
        return 1;
    }
    
    // Получение указателей на функции
    translation_func translation = (translation_func)dlsym(lib_handle, "translation");
    sinintegral_func SinIntegral = (sinintegral_func)dlsym(lib_handle, "SinIntegral");
    
    // Проверка наличия функций
    if (!translation) {
        fprintf(stderr, "Ошибка: не найдена функция 'translation'\n");
        dlclose(lib_handle);
        return 1;
    }
    
    if (!SinIntegral) {
        fprintf(stderr, "Ошибка: не найдена функция 'SinIntegral'\n");
        dlclose(lib_handle);
        return 1;
    }
    
    printf("=== Программа с динамической загрузкой библиотек ===\n");
    printf("Текущая библиотека: %s\n", current_lib);
    printf("Команды:\n");
    printf("  0 - переключить библиотеку\n");
    printf("  1 число - перевод числа (translation)\n");
    printf("  2 A B e - интеграл sin(x) (SinIntegral)\n");
    printf("  9 - выход\n\n");
    
    char input[256];
    
    while (1) {
        printf("> ");
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        int cmd;
        if (sscanf(input, "%d", &cmd) != 1) {
            printf("Ошибка: введите команду\n");
            continue;
        }
        
        if (cmd == 0) {
            // Переключение библиотеки
            dlclose(lib_handle);
            
            if (strcmp(current_lib, "./libimpl1.so") == 0) {
                strcpy(current_lib, "./libimpl2.so");
            } else {
                strcpy(current_lib, "./libimpl1.so");
            }
            
            lib_handle = dlopen(current_lib, RTLD_LAZY);
            if (!lib_handle) {
                fprintf(stderr, "Ошибка загрузки библиотеки %s: %s\n", current_lib, dlerror());
                return 1;
            }
            
            translation = (translation_func)dlsym(lib_handle, "translation");
            SinIntegral = (sinintegral_func)dlsym(lib_handle, "SinIntegral");
            
            if (!translation) {
                fprintf(stderr, "Ошибка: не найдена функция 'translation' в %s\n", current_lib);
                dlclose(lib_handle);
                return 1;
            }
            
            if (!SinIntegral) {
                fprintf(stderr, "Ошибка: не найдена функция 'SinIntegral' в %s\n", current_lib);
                dlclose(lib_handle);
                return 1;
            }
            
            printf("Переключено на библиотеку: %s\n", current_lib);
        }
        else if (cmd == 1) {
            long number;
            if (sscanf(input, "%*d %ld", &number) != 1) {
                printf("Ошибка: введите число после команды 1\n");
                continue;
            }
            
            char* result = translation(number);
            if (result) {
                printf("translation(%ld) = %s\n", number, result);
                free(result);  // Освобождение памяти, выделенной в библиотеке
            } else {
                printf("Ошибка: функция translation вернула NULL\n");
            }
        }
        else if (cmd == 2) {
            float A, B, e;
            if (sscanf(input, "%*d %f %f %f", &A, &B, &e) != 3) {
                printf("Ошибка: введите три числа A B e после команды 2\n");
                printf("Пример: 2 0 3.14159 0.01\n");
                continue;
            }
            
            float result = SinIntegral(A, B, e);
            printf("SinIntegral(%.2f, %.2f, %.4f) = %.6f\n", A, B, e, result);
        }
        else if (cmd == 9) {
            printf("Выход из программы\n");
            break;
        }
        else {
            printf("Неизвестная команда: %d\n", cmd);
            printf("Доступные команды: 0, 1, 2, 9\n");
        }
    }
    
    // Закрытие библиотеки перед выходом
    if (lib_handle) {
        dlclose(lib_handle);
    }
    
    return 0;
}
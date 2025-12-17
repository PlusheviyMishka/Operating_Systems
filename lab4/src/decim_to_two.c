#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char* translation(long x) {
    // определние размера буфера: 64 бита + знак + завершающий ноль
    char* result = (char*)malloc(65 * sizeof(char));
    if (result == NULL) {
        return NULL;
    }
    
    if (x == 0) {
        strcpy(result, "0");
        return result;
    }
    
    unsigned long num;
    int is_negative = 0;
    
    if (x < 0) {
        is_negative = 1;
        // для отрицательных чисел  дополнительный код
        num = (unsigned long)(-x);
        num = ~num + 1;
    } else {
        num = (unsigned long)x;
    }
    
    int pos = 0;
    
    if (is_negative) {
        result[pos++] = '1'; 
        int started = 0;
        for (int i = 62; i >= 0; i--) {  
            int bit = (num >> i) & 1;
            if (!started && bit == 1) {
                started = 1;
            }
            if (started) {
                result[pos++] = bit ? '1' : '0';
            }
        }
    } else {
        int started = 0;
        for (int i = 63; i >= 0; i--) {  // 64 бита
            int bit = (num >> i) & 1;
            if (!started && bit == 1) {
                started = 1;
            }
            if (started) {
                result[pos++] = bit ? '1' : '0';
            }
        }
    }
    
    if (pos == 0) {
        result[pos++] = '0';
    }
    
    result[pos] = '\0';  // Завершение строки
    
    return result;
}
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char* translation(long x) {
    if (x == 0) {
        char* zero = (char*)malloc(2 * sizeof(char));
        if (zero == NULL) return NULL;
        strcpy(zero, "0");
        return zero;
    }
    
    // Определяем знак
    int is_negative = 0;
    unsigned long num;
    
    if (x < 0) {
        is_negative = 1;
        num = (unsigned long)(-x);
    } else {
        num = (unsigned long)x;
    }
    
    // Вычисляем длину троичного представления
    unsigned long temp = num;
    int length = 0;
    
    while (temp > 0) {
        temp /= 3;
        length++;
    }
    
    if (length == 0) length = 1;  // для x=0
    
    // Добавляем место для знака и завершающего нуля
    char* result = (char*)malloc((length + is_negative + 1) * sizeof(char));
    if (result == NULL) {
        return NULL;
    }
    
    // Заполняем строку с конца
    int pos = length + is_negative;
    result[pos] = '\0';
    pos--;
    
    temp = num;
    
    if (temp == 0) {
        result[0] = '0';
        if (is_negative) {
            // Сдвиг "0" и добавляем минус
            result[1] = '\0';
            result[0] = '-';
            result[1] = '0';
        }
        return result;
    }
    
    while (temp > 0) {
        int digit = temp % 3;
        result[pos] = '0' + digit;
        temp /= 3;
        pos--;
    }
    
    if (is_negative) {
        result[0] = '-';
    }
    
    return result;
}
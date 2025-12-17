#include <math.h>
#include <stdio.h>

float SinIntegral(float A, float B, float e) {
    printf("[Библиотека 2] Метод ТРАПЕЦИЙ\n");
    
    if (e <= 0.000001f) e = 0.001f;
    if (A >= B) {
        float tmp = A;
        A = B;
        B = tmp;
    }
    
    int n = (int)((B - A) / e);
    if (n < 10) n = 10;
    if (n > 10000) n = 10000;
    
    float step = (B - A) / n;
    
    // Метод трапеций
    float sum = (sinf(A) + sinf(B)) / 2.0f;
    for (int i = 1; i < n; i++) {
        float x = A + i * step;
        sum += sinf(x);
    }
    
    return sum * step;
}
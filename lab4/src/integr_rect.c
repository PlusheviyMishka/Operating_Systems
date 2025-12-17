#include <math.h>
#include <stdio.h>

float SinIntegral(float A, float B, float e) {
    printf("[Библиотека 1] Метод ПРЯМОУГОЛЬНИКОВ\n");
    
    if (e <= 0.000001f) e = 0.001f;//задаю минимальный шаг если он почти 0
    if (A >= B) {
        float tmp = A;
        A = B;
        B = tmp;
    }
    
    int n = (int)((B - A) / e);
    if (n < 10) n = 10;
    
    float step = (B - A) / n;
    float integral = 0.0f;
    
    for (int i = 0; i < n; i++) {
        float x_mid = A + (i + 0.5f) * step;
        integral += sinf(x_mid);
    }
    integral *= step;
    
    return integral;
}
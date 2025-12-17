#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// Структура для результатов поиска
typedef struct {
    int *positions;        // массив для найденных позиций
    int count;             // количество найденных
    int capacity;          // вместимость массива
    pthread_mutex_t mutex; // мьютекс для этого результата
} search_results_t;

// Структура для передачи данных в поток
typedef struct {
    const char *text_chunk;    // часть текста для поиска
    int chunk_length;          // длина этой части
    int global_offset;         // смещение в исходном тексте
    const char *pattern;       // образец для поиска
    int pattern_len;           // длина образца
    search_results_t *results; // указатель на общие результаты
} thread_data_t;

// Конструктор результатов
void init_results(search_results_t *results, int initial_capacity) {
    results->positions = malloc(initial_capacity * sizeof(int));
    if (results->positions == NULL) {
        perror("malloc failed");
        exit(1);
    }
    results->count = 0;
    results->capacity = initial_capacity;
    pthread_mutex_init(&results->mutex, NULL);
}

// Деструктор результатов
void free_results(search_results_t *results) {
    free(results->positions);
    pthread_mutex_destroy(&results->mutex);
}

// Добавление позиции в результаты (с синхронизацией)
void add_position(search_results_t *results, int position) {
    pthread_mutex_lock(&results->mutex);
    
    // Если массив заполнен, увеличиваем его
    if (results->count >= results->capacity) {
        results->capacity *= 2;
        int *new_positions = realloc(results->positions, 
                                   results->capacity * sizeof(int));
        if (new_positions == NULL) {
            perror("realloc failed");
            pthread_mutex_unlock(&results->mutex);
            return;
        }
        results->positions = new_positions;
    }
    
    results->positions[results->count++] = position;
    pthread_mutex_unlock(&results->mutex);
}

// Функция потока для поиска (наивный алгоритм)
void* search_thread(void *arg) {
    thread_data_t *data = (thread_data_t*)arg;
    
    // Наивный алгоритм поиска подстроки
    for (int i = 0; i <= data->chunk_length - data->pattern_len; i++) {
        int j;
        for (j = 0; j < data->pattern_len; j++) {
            if (data->text_chunk[i + j] != data->pattern[j]) {
                break;
            }
        }
        
        // Если весь образец совпал
        if (j == data->pattern_len) {
            int global_pos = data->global_offset + i;
            add_position(data->results, global_pos);
        }
    }
    
    return NULL;
}

// Главная функция многопоточного поиска
void parallel_string_search(const char *text, const char *pattern, int num_threads) {
    size_t text_len = strlen(text);
    int pattern_len = strlen(pattern);
    
    if (pattern_len == 0) {
        printf("Pattern is empty\n");
        return;
    }
    
    if (text_len < pattern_len) {
        printf("Text is shorter than pattern\n");
        return;
    }
    
    // Инициализация результатов
    search_results_t results;
    init_results(&results, 100);
    
    // Выделяем память для массивов потоков и данных
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    thread_data_t *thread_data = malloc(num_threads * sizeof(thread_data_t));
    
    if (threads == NULL || thread_data == NULL) {
        perror("malloc failed");
        free_results(&results);
        return;
    }
    
    // Разделяем текст на части с перекрытием
    size_t chunk_size = text_len / num_threads;
    
    printf("Starting search with %d threads\n", num_threads);
    printf("Text length: %zu, Pattern: '%s' (%d chars)\n", 
           text_len, pattern, pattern_len);
    printf("Chunk size: %zu bytes\n", chunk_size);
    
    // Создаем потоки
    for (int i = 0; i < num_threads; i++) {
        // Определяем границы блока с перекрытием
        size_t start = i * chunk_size;
        size_t end;
        
        if (i == num_threads - 1) {
            // Последний поток получает до конца текста
            end = text_len;
        } else {
            // Обычный блок + перекрытие для поиска на границах
            end = start + chunk_size + pattern_len - 1;
            if (end > text_len) end = text_len;
        }
        
        thread_data[i].text_chunk = text + start;
        thread_data[i].chunk_length = end - start;
        thread_data[i].global_offset = start;
        thread_data[i].pattern = pattern;
        thread_data[i].pattern_len = pattern_len;
        thread_data[i].results = &results;
        
        if (pthread_create(&threads[i], NULL, search_thread, &thread_data[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
        }
    }
    
    // Ожидание завершения всех потоков
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Вывод результатов
    printf("\nSearch completed. Found %d occurrences:\n", results.count);
    for (int i = 0; i < results.count; i++) {
        printf("Position %d\n", results.positions[i]);
    }
    
    // Освобождаем ресурсы
    free(threads);
    free(thread_data);
    free_results(&results);
}

// Функция для измерения времени
double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {  
        printf("Usage: %s -t <num_threads> <pattern> <text>\n", argv[0]);
        printf("Example: %s -t 4 \"hello\" \"hello world hello there\"\n", argv[0]);
        return 1;
    }
    
    // Парсинг аргументов
    if (strcmp(argv[1], "-t") != 0) {
        printf("Error: expected -t flag\n");
        return 1;
    }
    
    int num_threads = atoi(argv[2]);
    const char *pattern = argv[3];
    const char *text = argv[4];  
    
    if (num_threads <= 0) {
        printf("Error: number of threads must be positive\n");
        return 1;
    }
    
    //printf("Input text: '%s'\n", text);
    //printf("Search pattern: '%s'\n", pattern);
    printf("Number of threads: %d\n", num_threads);
    
    // Замер времени выполнения
    double start_time = get_time();
    
    // Выполняется многопоточный поиск
    parallel_string_search(text, pattern, num_threads);
    
    double end_time = get_time();
    printf("\nExecution time: %.6f seconds\n", end_time - start_time);
    
    return 0;
}
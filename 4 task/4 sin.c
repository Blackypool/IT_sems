/* Многопоточное вычисление sin(x) через ряд Тейлора. */
#define _POSIX_C_SOURCE 200809L

//_____________________________________________________BIBLIOTECS__________________________________________________________________________//
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________PARAMETERS__________________________________________________________________________//
#define MAX_THREADS    64
#define MAX_TERMS      100000
#define MAX_REPEATS    100000

#define ERROR_IN_FUN   -1
#define ALL_RIGHT       1
//_________________________________________________________________________________________________________________________________________//


typedef struct
{
    long double x;
    long double first_term;
    long double result;
    size_t first;
    size_t count;
} worker_data_t;


//_____________________________________________________SERIES_____________________________________________________________________________//
static void* sum_terms(void* argument)
{
    worker_data_t* data = (worker_data_t*)argument;
    long double term = data->first_term;
    long double sum = 0.0L;
    long double correction = 0.0L;

    for(size_t index = data->first; index < data->first + data->count; ++index)
    {
        // Компенсация Кэхэна уменьшает потерю младших разрядов при сложении.
        long double adjusted = term - correction;
        long double next_sum = sum + adjusted;
        correction = (next_sum - sum) - adjusted;
        sum = next_sum;

        // t[k+1] = -t[k] * x^2 / ((2k+2) * (2k+3)).
        long double denominator = (2.0L * index + 2.0L) *
                                  (2.0L * index + 3.0L);
        term *= -(data->x * data->x) / denominator;
    }

    data->result = sum;
    return NULL;
}


static int calculate(long double x, size_t terms, size_t threads, long double* result)
{
    pthread_t thread_ids[MAX_THREADS] = {0};
    worker_data_t data[MAX_THREADS] = {{0}};
    size_t created = 0;
    size_t position = 0;
    long double first_term = x;

    for(size_t thread = 0; thread < threads; ++thread)
    {
        size_t count = terms / threads + (thread < terms % threads);

        data[thread].x = x;
        data[thread].first_term = first_term;
        data[thread].result = 0.0L;
        data[thread].first = position;
        data[thread].count = count;

        // Готовим начальный член следующего блока без факториалов и pow().
        for(size_t index = position; index < position + count; ++index)
        {
            long double denominator = (2.0L * index + 2.0L) *
                                      (2.0L * index + 3.0L);
            first_term *= -(x * x) / denominator;
        }

        position += count;
        int error = pthread_create(&thread_ids[thread], NULL, sum_terms, &data[thread]);

        if(error != 0)
        {
            errno = error;
            perror("pthread_create");
            break;
        }

        ++created;
    }

    for(size_t thread = 0; thread < created; ++thread)
    {
        int error = pthread_join(thread_ids[thread], NULL);

        if(error != 0)
        {
            errno = error;
            perror("pthread_join");
            return ERROR_IN_FUN;
        }
    }

    if(created != threads)
        return ERROR_IN_FUN;

    long double sum = 0.0L;
    long double correction = 0.0L;

    // Частичные суммы объединяет только главный поток.
    for(size_t thread = 0; thread < threads; ++thread)
    {
        long double adjusted = data[thread].result - correction;
        long double next_sum = sum + adjusted;
        correction = (next_sum - sum) - adjusted;
        sum = next_sum;
    }

    *result = sum;
    return ALL_RIGHT;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________ARGUMENTS___________________________________________________________________________//
static int parse_count(const char* text, size_t limit, size_t* value)
{
    char* end_ptr = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end_ptr, 10);

    if(errno != 0 || *text == '\0' || *end_ptr != '\0' ||
       parsed == 0 || parsed > limit)
        return ERROR_IN_FUN;

    *value = (size_t)parsed;
    return ALL_RIGHT;
}


static long double elapsed_seconds(const struct timespec* start,
                                   const struct timespec* end)
{
    return (long double)(end->tv_sec - start->tv_sec) +
           (long double)(end->tv_nsec - start->tv_nsec) / 1000000000.0L;
}
//_________________________________________________________________________________________________________________________________________//


//_____________________________________________________MAIN________________________________________________________________________________//
int main(int argc, char* argv[])
{
    if(argc != 5)
    {
        fprintf(stderr, "Использование: %s X ЧЛЕНОВ_РЯДА МАКС_ПОТОКОВ ПОВТОРОВ\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* end_ptr = NULL;
    size_t terms = 0;
    size_t max_threads = 0;
    size_t repeats = 0;
    errno = 0;
    long double x = strtold(argv[1], &end_ptr);

    if(errno != 0 || *argv[1] == '\0' || *end_ptr != '\0' || !isfinite(x) ||
       parse_count(argv[2], MAX_TERMS, &terms) < 0 ||
       parse_count(argv[3], MAX_THREADS, &max_threads) < 0 ||
       parse_count(argv[4], MAX_REPEATS, &repeats) < 0)
    {
        fprintf(stderr, "Некорректные аргументы\n");
        return EXIT_FAILURE;
    }

    if(max_threads > terms)
        max_threads = terms;

    // Приведение аргумента ускоряет сходимость ряда.
    long double reduced_x = remainderl(x, 2.0L * acosl(-1.0L));
    long double reference = sinl(x);

    printf("x=%.12Lg, после приведения=%.12Lg, sinl(x)=%.18Lg\n",
           x, reduced_x, reference);
    puts("Потоки | Результат             | Ошибка       | Среднее время, мкс");

    for(size_t threads = 1; threads <= max_threads; ++threads)
    {
        long double value = 0.0L;
        long double total_seconds = 0.0L;

        for(size_t run = 0; run < repeats; ++run)
        {
            struct timespec start = {0};
            struct timespec end = {0};

            if(clock_gettime(CLOCK_MONOTONIC, &start) < 0)
            {
                perror("clock_gettime");
                return EXIT_FAILURE;
            }

            if(calculate(reduced_x, terms, threads, &value) < 0)
                return EXIT_FAILURE;

            if(clock_gettime(CLOCK_MONOTONIC, &end) < 0)
            {
                perror("clock_gettime");
                return EXIT_FAILURE;
            }

            total_seconds += elapsed_seconds(&start, &end);
        }

        printf("%6zu | % .18Lg | %.3Le | %.3Lf\n",
               threads, value, fabsl(value - reference),
               total_seconds * 1000000.0L / repeats);
    }

    return EXIT_SUCCESS;
}
//_________________________________________________________________________________________________________________________________________//

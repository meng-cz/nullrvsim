#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

pthread_mutex_t mutex;
unsigned int current;

#define LOOP_COUNT  (10)

void *thread_func(void* param) {
    int id = (unsigned long)param;
    for(int i = 0; i < LOOP_COUNT; i++) {
        pthread_mutex_lock(&mutex);
        unsigned int n = current;
        current ++;
        printf("Thread %d: %d\n", id, n);
        pthread_mutex_unlock(&mutex);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s thread_num\n", argv[0]);
        exit(-1);
    }
    int thread_num = atoi(argv[1]);
    pthread_t *ths = (pthread_t*)malloc(sizeof(pthread_t) * thread_num);
    pthread_mutex_init(&mutex, NULL);
    current = 0;

    for(int i = 0; i < thread_num; i++) {
        pthread_create(ths + i, NULL, thread_func, (void*)((long)i));
    }

    for(int i = 0; i < thread_num; i++) {
        pthread_join(ths[i], NULL);
    }

    free(ths);
    pthread_mutex_destroy(&mutex);

    return 0;
}

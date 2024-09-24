

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <sys/mman.h>

int * data = 0;
int pn = 4;
int cnt = 1024;

pthread_mutex_t mutex;

void *th_func(void *_p) {
    int my_id = (long)_p;
    pthread_mutex_lock(&(mutex));
    printf("Process %d Start\n", my_id);
    fflush(stdout);
    pthread_mutex_unlock(&(mutex));
    for(int i = cnt * my_id; i < cnt * (my_id + 1); i++) {
        data[i] = i;
    }
    pthread_mutex_lock(&(mutex));
    printf("Process %d Finished\n", my_id);
    fflush(stdout);
    pthread_mutex_unlock(&(mutex));
    return 0;
}

int main(int argc, char *argv[]) {

    if(argc < 2) {
        printf("Usage: %s thnum datasz\n", argv[0]);
        exit(-1);
    }

    if(argc >= 2) {
        pn = atoi(argv[1]);
    }

    if(argc >= 3) {
        cnt = atoi(argv[2]);
    }

    printf("Number of Thread: %d, Data size : %d\n", pn, cnt);

    data = (int*)mmap(0, sizeof(int) * pn * cnt, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    pthread_mutex_init(&mutex, NULL);

    printf("Init\n");
    fflush(stdout);

    pthread_t * ths = (pthread_t *)malloc(sizeof(pthread_t) * pn);
    for(int i = 0; i < pn; i++) {
        pthread_create(ths + i, 0, th_func, (void*)((long)i));
    }

    for(int i = 0; i < pn; i++) {
        pthread_join(ths[i], 0);
    }

    printf("Check\n");

    int pass = 1;
    for(int i = 0; i < cnt * pn; i++) {
        if(data[i] != i) {
            pass = 0;
            break;
        }
    }

    if(pass) {
        printf("Pass!!!\n");
    }
    else {
        printf("Failed!!!\n");
    }
    fflush(stdout);

    pthread_mutex_destroy(&mutex);
    munmap(data, sizeof(int) * pn * cnt);

    return 0;
}





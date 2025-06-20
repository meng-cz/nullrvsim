
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <sys/mman.h>

int * data = 0;
#define ROUND (20)
#define WIDTH (64)
#define CNT (WIDTH * WIDTH)

void *th_func(void *_p) {
    int my_id = (long)_p;
    int * my_data = data + (my_id * CNT * 3);
    int * mata = my_data;
    int * matb = my_data + (CNT);
    int * matc = my_data + (CNT * 2);

    if(my_id == 0) {
        printf("Start\n");
        fflush(stdout);
    }
    for(int r = 0; r < ROUND; r++) {
        for(int i = 0; i < CNT; i++) {
            mata[i] = i*2 + r;
            matb[i] = i*3 + r*2;
            matc[i] = i*4 + r*3;
        }
        for(int m = 0; m < WIDTH; m++) {
            for(int n = 0; n < WIDTH; n++) {
                for(int k = 0; k < WIDTH; k++) {
                    matc[m*WIDTH+n] += mata[m*WIDTH+k] * matb[k*WIDTH+n];
                }
            }
        }
        if(my_id == 0) {
            printf("Round %d\n", r);
            fflush(stdout);
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {

    if(argc < 2) {
        printf("Usage: %s thnum\n", argv[0]);
        exit(-1);
    }

    int pn = atoi(argv[1]);

    printf("Number of Thread: %d, Data width : %d\n", pn, WIDTH);

    data = (int*)mmap(0, sizeof(int) * pn * CNT * 3, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    printf("Init\n");
    fflush(stdout);

    pthread_t * ths = (pthread_t *)malloc(sizeof(pthread_t) * pn);
    for(int i = 1; i < pn; i++) {
        pthread_create(ths + i, 0, th_func, (void*)((long)i));
    }

    th_func(0);

    for(int i = 1; i < pn; i++) {
        pthread_join(ths[i], 0);
    }

    printf("Exit\n");

    munmap(data, sizeof(int) * pn * CNT * 3);

    return 0;
}






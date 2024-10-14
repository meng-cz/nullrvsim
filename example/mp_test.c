
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <sys/mman.h>

typedef struct {
    int total_pn;
    int finished_pc;
    pthread_mutex_t mutex;
    pthread_mutexattr_t mutexattr;
} SharedStruct;

int main(int argc, char *argv[]) {

    if(argc < 2) {
        printf("Usage: %s np datasz\n", argv[0]);
        exit(-1);
    }

    int pn = 4;
    if(argc >= 2) {
        pn = atoi(argv[1]);
    }

    int cnt = 1024;
    if(argc >= 3) {
        cnt = atoi(argv[2]);
    }

    printf("Number of Process: %d, Data size : %d\n", pn, cnt);

    int *a = (int*)mmap(0, sizeof(int) * pn * cnt, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);

    SharedStruct *ss = (SharedStruct*)mmap(0, sizeof(SharedStruct), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);

    memset(ss, 0, sizeof(SharedStruct));
    ss->total_pn = pn;
    pthread_mutexattr_init(&ss->mutexattr);
    pthread_mutexattr_setpshared(&ss->mutexattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&ss->mutex, &ss->mutexattr);

    printf("Init\n");
    fflush(stdout);

    int my_id = 0;

    for(int i = 1; i < pn; i++) {
        long res = fork();
        if(res < 0) {
            printf("Fork Error %ld\n", res);
            exit(-1);
        }
        else if(res == 0) {
            my_id = i;
            break;
        }
    }

    pthread_mutex_lock(&(ss->mutex));
    printf("Process %d Start\n", my_id);
    fflush(stdout);
    pthread_mutex_unlock(&(ss->mutex));

    for(int i = cnt * my_id; i < cnt * (my_id + 1); i++) {
        a[i] = i;
    }

    pthread_mutex_lock(&(ss->mutex));
    printf("Process %d Finished\n", my_id);
    fflush(stdout);
    ss->finished_pc++;
    int is_last = (ss->finished_pc == ss->total_pn);
    pthread_mutex_unlock(&(ss->mutex));

    if(!is_last) {
        return 0;
    }

    printf("Process %d : Check\n", my_id);

    int pass = 1;
    for(int i = 0; i < cnt * pn; i++) {
        if(a[i] != i) {
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

    pthread_mutexattr_destroy(&ss->mutexattr);
    pthread_mutex_destroy(&ss->mutex);
    munmap(ss, sizeof(SharedStruct));
    munmap(a, sizeof(int) * pn * cnt);

    return 0;
}


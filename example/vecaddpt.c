
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

void load_array(const char *filename, int *buf, int num) {
    FILE *fp = fopen(filename, "r");
    char line[256];
    for(int i = 0; i < num; i++) {
        assert(!feof(fp));
        assert(fgets(line, 256, fp));
        buf[i] = atoi(line);
    }
    fclose(fp);
}

struct ThreadParam {
    int *a, *b, *c;
    int sz;
};

void *thread_func(void *param) {
    struct ThreadParam *p = (struct ThreadParam*)param;
    
    for(int i = 0; i < p->sz; i++) {
        p->c[i] = p->a[i] + p->b[i];
    }

    return 0;
}

#define CEIL_DIV(x,y) (((x) + (y) - 1) / (y))

int main(int argc, char* argv[]) {

    if(argc < 5) {
        printf("Usage: %s thnum num a.txt b.txt c.txt\n", argv[0]);
        return -1;
    }

    srand(time(0));

    int tn = atoi(argv[1]);
    int sz = atoi(argv[2]);
    int *a = (int*)malloc(sizeof(float) * sz);
    int *b = (int*)malloc(sizeof(float) * sz);
    int *c = (int*)malloc(sizeof(float) * sz);
    int *ic = (int*)malloc(sizeof(float) * sz);

    printf("Init...\n");
    fflush(stdout);

    load_array(argv[3], a, sz);
    load_array(argv[4], b, sz);
    load_array(argv[5], ic, sz);

    printf("Start...\n");
    fflush(stdout);

    pthread_t *ths = (pthread_t*)malloc(sizeof(pthread_t) * tn);
    struct ThreadParam *tp = (struct ThreadParam *)malloc(sizeof(struct ThreadParam) * tn);

    int cur = 0;
    int step = CEIL_DIV(sz, tn);
    for(int i = 0; i < tn; i++) {
        if(cur + step > sz) step = sz - cur;
        tp[i].a = a + cur;
        tp[i].b = b + cur;
        tp[i].c = c + cur;
        tp[i].sz = step;
        cur += step;
        assert(0 == pthread_create(ths + i, 0, thread_func, tp + i));
    }
    
    for(int i = 0; i < tn; i++) {
        assert(0 == pthread_join(ths[i], 0));
    }

    printf("Check...\n");
    fflush(stdout);

    int succ = 1;
    for(int i = 0; i < 10; i++) {
        int index = rand() % sz;
        if(c[index] != ic[index]) {
            printf("%d: required %d, real %d\n", index, ic[index], c[index]);
            succ = 0;
        }
    }

    if(succ) printf("Success!!\n");
    else printf("Check Failed!!\n");

    free(ths);
    free(tp);
    free(a);
    free(b);
    free(c);

    return 0;
} 

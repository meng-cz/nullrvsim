
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>

#include <sys/mman.h>

int main(int argc, char* argv[]) {

    if(argc < 5) {
        printf("Usage: %s num a.bin b.bin c.bin\n", argv[0]);
        return -1;
    }

    printf("Start: %s %s\n", argv[0], argv[1]);
    fflush(stdout);

    srand(time(0));

    int sz = atoi(argv[1]);
    int *c = (int*)malloc(sizeof(float) * sz);

    printf("Init...\n");
    fflush(stdout);

    FILE *fpa = fopen(argv[2], "r");
    int *a = (int*) mmap (0, sz * sizeof(int), PROT_READ, MAP_PRIVATE, fileno(fpa), 0);
    fclose(fpa);
    FILE *fpb = fopen(argv[3], "r");
    int *b = (int*) mmap (0, sz * sizeof(int), PROT_READ, MAP_PRIVATE, fileno(fpb), 0);
    fclose(fpb);
    FILE *fpc = fopen(argv[4], "r");
    int *ic = (int*) mmap (0, sz * sizeof(int), PROT_READ, MAP_PRIVATE, fileno(fpc), 0);
    fclose(fpc);

    printf("Start...\n");
    fflush(stdout);

    for(int i = 0; i < sz; i++) {
        c[i] = a[i] + b[i];
    }

    printf("Check...\n");
    fflush(stdout);

    // FILE *fp = fopen("out.txt", "w");
    // char buf[256];
    // for(int i = 0; i < sz; i++) {
    //     sprintf(buf, "%d\n", c[i]);
    //     fwrite(buf, 1, strlen(buf), fp);
    // }
    // fclose(fp);

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

    munmap(a, sz * sizeof(int));
    munmap(b, sz * sizeof(int));
    munmap(ic, sz * sizeof(int));
    free(c);

    return 0;
} 


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>

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

int main(int argc, char* argv[]) {

    if(argc < 5) {
        printf("Usage: %s num a.txt b.txt c.txt\n", argv[0]);
        return -1;
    }

    printf("Start: %s %s\n", argv[0], argv[1]);
    fflush(stdout);

    srand(time(0));

    int sz = atoi(argv[1]);
    int *a = (int*)malloc(sizeof(float) * sz);
    int *b = (int*)malloc(sizeof(float) * sz);
    int *c = (int*)malloc(sizeof(float) * sz);
    int *ic = (int*)malloc(sizeof(float) * sz);

    printf("Init...\n");
    fflush(stdout);

    load_array(argv[2], a, sz);
    load_array(argv[3], b, sz);
    load_array(argv[4], ic, sz);

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

    return 0;
} 

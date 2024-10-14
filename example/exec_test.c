
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {

    if(argc == 1) {
        printf("Usage: %s program [args, ...]\n", argv[0]);
        exit(0);
    }

    printf("This is a exec loader\n");

    printf("Target program : %s\n", argv[1]);

    char ** tmp = (char**)malloc(sizeof(char*) * argc);
    for(int i = 1; i < argc; i++) {
        tmp[i-1] = argv[i];
    }
    tmp[argc - 1] = 0;

    int ret = execv(argv[1], tmp);

    if(ret) {
        printf("exec failed %d\n", ret);
    }

    return 0;
}


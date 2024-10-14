
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <sys/select.h>
#include <sys/socket.h>

int sockfd[2];

void func1() {
    
    close(sockfd[1]);

    printf("P0: Start wait\n");

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd[0], &readfds);

    struct timespec tmo;
    tmo.tv_sec = 10;
    tmo.tv_nsec = 0;

    int ret = pselect(sockfd[0] + 1, &readfds, 0, 0, &tmo, 0);

    if(ret > 0 && FD_ISSET(sockfd[0], &readfds)) {
        printf("P0: Read select success\n");

        char buf[256];
        int len = read(sockfd[0], buf, 256);
        buf[len] = 0;
        printf("P0 Recv: %s\n", buf);
    }
    else {
        printf("P0: Select return %d\n", ret);
    }
}

void func2() {
    
    close(sockfd[0]);

    printf("P1: Wait 1s\n");

    sleep(1);

    char msg[] = "helloworld";
    int len = write(sockfd[1], msg, sizeof(msg));

    if(len == sizeof(msg)) {
        printf("P1 Send: %s\n", msg);
    }
    else {
        printf("P1 Send return %d\n", len);
    }

}

int main(int argc, char *argv[]) {

    int ret = socketpair(1, 1, 0, sockfd);
    if(ret != 0) {
        printf("Socketpair error %d\n", ret);
        exit(0);
    }

    if(fork()) {
        func1();
    }
    else {
        func2();
    }

    return 0;
}


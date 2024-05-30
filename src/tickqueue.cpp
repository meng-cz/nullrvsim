#include "tickqueue.h"

namespace test {

bool test_simpletickqueue() {
    
    uint64_t next_push = 0;
    uint64_t next_pop = 0;

    const uint32_t op_tick = 0;
    const uint32_t op_push = 1;
    const uint32_t op_pop = 2;

    SimpleTickQueue<uint64_t> tq(1, 1, 0);

    uint64_t round = 10000000UL;
    printf("pop 0");
    for(uint64_t __n = 0; __n < round; __n ++) {
        uint32_t op = RAND(0, 3);
        if(op == op_tick) {
            tq.apply_next_tick();
        }
        else if(op == op_push) {
            if(tq.push(next_push)) next_push ++;
        }
        else if(tq.can_pop()) {
            uint64_t v = tq.top();
            tq.pop();
            assert(v == next_pop);
            next_pop ++;
            if(v % 512 == 0) {
                printf("\rpop %ld", v);
            }
        }
    }
    printf("\nTest Tick Queue Passed!!!\n");

    return true;
}

bool test_tickqueue() {
    return test_simpletickqueue();

    // OneTickQueue<uint64_t, 4, 3, 4> q;
    // std::list<uint64_t> buf;
    // uint64_t recv = 0;
    // uint64_t send = 0;
    // uint64_t total = 32;
    // uint64_t tick = 0;
    // while(recv < total) {
    //     uint32_t pushcnt = q.can_push_cnt();
    //     uint32_t popcnt = q.can_pop_cnt();
    //     printf("%ld: queue can push %d, can pop %d, Push buf: ", tick, pushcnt, popcnt);
    //     for(int i = 0; i < q.input_width() - pushcnt;i++) printf("%ld ", q.push_buf[i]);
    //     printf(", Top buf: ");
    //     for(int i = 0; i < popcnt;i++) printf("%ld ", q.top_buf[i]);
    //     printf(", Queue: ");
    //     q.debug_get_queue(&buf);
    //     for(auto d : buf) printf("%ld ", d);
    //     printf("\n");

    //     if(pushcnt && send <= total) {
    //         uint32_t do_push = ((pushcnt > 1)?RAND(1,pushcnt + 1):1);
    //         printf("Do push: ");
    //         for(int i = 0; i < do_push; i++) {
    //             q.push(send);
    //             printf("%ld ", send);
    //             send ++;
    //         }
    //         printf("\n");
    //     }
    //     if(popcnt) {
    //         uint32_t do_pop = ((popcnt > 1)?RAND(1,popcnt + 1):1);
    //         printf("Do pop: ");
    //         for(int i = 0; i < do_pop; i++) {
    //             assert((q.top_buf[i] == 0 && recv == 0) || q.top_buf[i] == recv + 1);
    //             recv = q.top_buf[i];
    //             q.pop();
    //             printf("%ld ", recv);
    //         }
    //         printf("\n");
    //     }

    //     q.on_current_tick();
    //     q.apply_next_tick();
    // }

    return true;
}

}



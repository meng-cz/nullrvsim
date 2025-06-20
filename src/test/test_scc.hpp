
#include "cache/simplecc/sccl1.h"
#include "cache/simplecc/sccllc.h"

using simcache::SimpleCoherentL1;
using simcache::SimpleCoherentLLCWithMem;
using simcache::CacheParam;

namespace test {

bool test_scc_1l24l1_seq_wr() {

    const bool debug = false;

    CacheParam l1param, l2param;

    l1param.set_offset = 5;
    l1param.way_cnt = 4;

    l2param.set_offset = 8;
    l2param.way_cnt = 8;

    uint64_t memsz = 128UL*1024UL*1024UL;
    uint64_t memsz64 = memsz / 8UL;
    unique_ptr<SimpleCoherentLLCWithMem> l2 = make_unique<SimpleCoherentLLCWithMem>(l2param, 4, 8, memsz, 0, 20);
    l2->debug = debug;

    vector<unique_ptr<SimpleCoherentL1>> l1s;
    l1s.resize(4);
    for(int32_t i = 0; i < 4; i++) {
        l1s[i] = make_unique<SimpleCoherentL1>(l1param, l2->get_port(i));
        l1s[i]->debug = debug;
    }

    uint64_t tick = 0;
    auto next_tick = [&]()->void {
        for(int i = 0; i < 4; i++) l1s[i]->on_current_tick();
        l2->on_current_tick();
        for(int i = 0; i < 4; i++) l1s[i]->apply_next_tick();
        l2->apply_next_tick();
        tick++;
        simroot::set_current_tick(tick);
    };

    vector<uint64_t> global_mem;
    global_mem.resize(memsz64);

    for(SizeT i = 0; i < memsz; i += sizeof(uint64_t) * 2UL) {
        uint64_t d0 = i;
        uint64_t d1 = i + sizeof(uint64_t);
        bool succ0 = false, succ1 = false;
        while(!(succ0 && succ1)) {
            if(!succ0) succ0 = (l1s[0]->store(d0, 8, &d0, false) == SimError::success);
            if(!succ1) succ1 = (l1s[1]->store(d1, 8, &d1, false) == SimError::success);
            next_tick();
        }
    }

    for(SizeT i = 0; i < memsz; i += sizeof(uint64_t) * 2UL) {
        uint64_t a0 = i, a1 = i + sizeof(uint64_t);
        uint64_t d0 = 0, d1 = 0;
        bool succ0 = false, succ1 = false;
        while(!(succ0 && succ1)) {
            if(!succ0) succ0 = (l1s[2]->load(a0, 8, &d0, false) == SimError::success);
            if(!succ1) succ1 = (l1s[3]->load(a1, 8, &d1, false) == SimError::success);
            next_tick();
        }
        assert(d0 == a0);
        assert(d1 == a1);
    }

    printf("Succ 1\n");

    for(SizeT i = 0; i < memsz; i += sizeof(uint64_t) * 2UL) {
        uint64_t a0 = i, a1 = i + sizeof(uint64_t);
        uint64_t d0 = memsz - a0, d1 = memsz - a1;
        bool succ0 = false, succ1 = false;
        while(!(succ0 && succ1)) {
            if(!succ0) succ0 = (l1s[0]->store(a0, 8, &d0, false) == SimError::success);
            if(!succ1) succ1 = (l1s[1]->store(a1, 8, &d1, false) == SimError::success);
            next_tick();
        }
    }

    for(SizeT i = 0; i < memsz; i += sizeof(uint64_t) * 2UL) {
        uint64_t a0 = i, a1 = i + sizeof(uint64_t);
        uint64_t e0 = memsz - a0, e1 = memsz - a1;
        uint64_t d0 = 0, d1 = 0;
        bool succ0 = false, succ1 = false;
        while(!(succ0 && succ1)) {
            if(!succ0) succ0 = (l1s[2]->load(a0, 8, &d0, false) == SimError::success);
            if(!succ1) succ1 = (l1s[3]->load(a1, 8, &d1, false) == SimError::success);
            next_tick();
        }
        assert(d0 == e0);
        assert(d1 == e1);
    }

    printf("Succ 2\n");

    printf("Pass test_scc_1l24l1_seq_wr() !!!\n");

    return true;
}


bool test_scc_1l24l1_rand_wr() {

    const bool debug = false;

    CacheParam l1param, l2param;

    l1param.set_offset = 5;
    l1param.way_cnt = 4;

    l2param.set_offset = 8;
    l2param.way_cnt = 8;

    uint64_t memsz = 128UL*1024UL*1024UL;
    uint64_t memsz64 = memsz / 8UL;
    unique_ptr<SimpleCoherentLLCWithMem> l2 = make_unique<SimpleCoherentLLCWithMem>(l2param, 4, 8, memsz, 0, 20);
    l2->debug = debug;

    vector<unique_ptr<SimpleCoherentL1>> l1s;
    l1s.resize(4);
    for(int32_t i = 0; i < 4; i++) {
        l1s[i] = make_unique<SimpleCoherentL1>(l1param, l2->get_port(i));
        l1s[i]->debug = debug;
    }

    uint64_t tick = 0;
    auto next_tick = [&]()->void {
        for(int i = 0; i < 4; i++) l1s[i]->on_current_tick();
        l2->on_current_tick();
        for(int i = 0; i < 4; i++) l1s[i]->apply_next_tick();
        l2->apply_next_tick();
        tick++;
        simroot::set_current_tick(tick);
    };

    vector<uint64_t> global_mem;
    global_mem.assign(memsz64, 0);
    uint8_t *host_mem = (uint8_t*)(global_mem.data());

    auto perform_cache_op = [](SimpleCoherentL1 *c, bool write, uint64_t addr, uint64_t *buf) -> bool {
        if(write) {
            return (c->store(addr, 8, buf, false) == SimError::success);
        }
        else {
            return (c->load(addr, 8, buf, false) == SimError::success);
        }
    };

    uint64_t round = 1024UL * 1024UL;
    uint64_t output_interval = 1024UL;

    printf("Test 0/%ld", round);
    for(uint64_t __n = 0; __n < round; __n++) {
        if((__n % output_interval) == output_interval - 1) {
            printf("\rTest %ld/%ld", __n + 1, round);
            fflush(stdout);
        }

        bool succ[4] = {false, false, false, false};
        bool do_write[4] = {false, false, false, false};
        uint64_t datas[4];
        std::vector<uint64_t> addrs;

        for(int i = 0; i < 4; i++) {
            do_write[i] = RAND(0, 2);
            datas[i] = rand_long();
            uint64_t a = ((rand_long() % memsz) & (~7UL));
            while(find_iteratable(addrs.begin(), addrs.end(), a) != addrs.end()) a = ((rand_long() % memsz) & (~7UL));
            addrs.push_back(a);
        }

        while(!(succ[0] && succ[1] && succ[2] && succ[3])) {
            if(!succ[0]) succ[0] = perform_cache_op(l1s[0].get(), do_write[0], addrs[0], datas+0);
            if(!succ[1]) succ[1] = perform_cache_op(l1s[1].get(), do_write[1], addrs[1], datas+1);
            if(!succ[2]) succ[2] = perform_cache_op(l1s[2].get(), do_write[2], addrs[2], datas+2);
            if(!succ[3]) succ[3] = perform_cache_op(l1s[3].get(), do_write[3], addrs[3], datas+3);
            next_tick();
        }

        for(int i = 0; i < 4; i++) {
            if(do_write[i]) {
                *((uint64_t*)(host_mem + addrs[i])) = datas[i];
            }
            else {
                if(*((uint64_t*)(host_mem + addrs[i])) != datas[i]) {
                    printf("CORE %d ERROR: @0x%lx, line @0x%lx\n", i, addrs[i], addr_to_line_index(addrs[i]));
                    simroot_assert(0);
                }
            }
        }
    }

    printf("\n");

    printf("Pass test_scc_1l24l1_rand_wr() !!!\n");

    return true;
}




}

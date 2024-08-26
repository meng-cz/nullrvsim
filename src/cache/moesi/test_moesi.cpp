
#include "test_moesi.h"

#include "common.h"

#include "lastlevelcache.h"
#include "l1cachev2.h"
#include "l1l2v2.h"
#include "dmaasl1.h"
#include "memnode.h"

#include "bus/symmulcha.h"
#include "bus/routetable.h"

#include "simroot.h"
#include "configuration.h"

namespace test {

using simbus::BusInterfaceV2;
using simbus::SymmetricMultiChannelBus;
using simbus::BusNodeT;
using simbus::BusPortT;
using simbus::BusPortMapping;
using simbus::BusRouteTable;

using namespace simcache::moesi;

using simcache::MemCtrlLineAddrMap;


class TestL1CacheBusMapping : public BusPortMapping {
public:
    virtual bool get_homenode_port(LineIndexT line, BusPortT *out) {
        if(out) *out = 0;
        return true;
    };
    virtual bool get_subnode_port(LineIndexT line, BusPortT *out) { return false; };

    virtual bool get_reqnode_port(uint32_t index, BusPortT *out) { return false; };
    virtual bool get_reqnode_index(BusPortT port, uint32_t *out) { return false; };
};

class TestL1CacheBus : public simbus::BusInterfaceV2 {
public:

    TestL1CacheBus(uint8_t* psimmem, uint8_t *phostmem) : simmem(psimmem), hostmem(phostmem) {
        this->debug_log = conf::get_int("l1cache", "debug_log", 0);
        recv_queue.resize(CHANNEL_CNT);
    };

    bool debug_log = false;

    uint8_t *simmem = nullptr;
    uint8_t *hostmem = nullptr;

    virtual uint16_t get_port_num() {return 2;};
    virtual uint16_t get_bus_width() {return CACHE_LINE_LEN_BYTE;};

    virtual void can_send(BusPortT port, vector<bool> &out) {out.assign(CHANNEL_CNT, true);};
    virtual bool can_send(BusPortT port, uint32_t channel) {return true;};
    virtual bool send(BusPortT port, BusPortT dst_port, uint32_t channel, vector<uint8_t> &data) {
        CacheCohenrenceMsg msg;
        parse_msg_pack(data, msg);
        LineIndexT lindex = msg.line;
        PhysAddrT addr = line_index_to_line_addr(lindex);
        if(msg.type == MSG_GETS) {
            msg.data.assign(CACHE_LINE_LEN_BYTE, 0);
            cache_line_copy(msg.data.data(), simmem + addr);
            msg.type = MSG_GETS_RESP;
            msg.arg = 0;
            recv_queue[CHANNEL_RESP].push_back(msg);
            if(debug_log) {
                printf("sys: send shared line @0x%lx:", lindex);
                for(int i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
                    printf(" %2x", msg.data[i]);
                }
                printf("\n");
            }
        }
        else if(msg.type == MSG_GETM) {
            msg.data.assign(CACHE_LINE_LEN_BYTE, 0);
            cache_line_copy(msg.data.data(), simmem + addr);
            msg.type = MSG_GETM_RESP;
            msg.arg = 1;
            recv_queue[CHANNEL_RESP].push_back(msg);
            if(debug_log) {
                printf("sys: send modified line @0x%lx:", lindex);
                for(int i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
                    printf(" %2x", msg.data[i]);
                }
                printf("\n");
            }
        }
        else if(msg.type == MSG_PUTM) {
            uint64_t *host = (uint64_t*)(hostmem + addr);
            uint64_t *sim = (uint64_t*)(simmem + addr);
            assert(msg.data.size() == CACHE_LINE_LEN_BYTE);
            if(debug_log) {
                printf("sys: put line @0x%lx:", lindex);
                for(int i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
                    printf(" %2x", msg.data[i]);
                }
                printf("\n");
            }
            cache_line_copy(sim, msg.data.data());
            // for(int i = 0; i < CACHE_LINE_LEN_BYTE / sizeof(uint64_t); i++) {
            //     simroot_assert(host[i] == sim[i]);
            // }
            msg.type = MSG_PUT_ACK;
            msg.data.clear();
            recv_queue[CHANNEL_ACK].push_back(msg);
        }
        else if(msg.type == MSG_PUTE || msg.type == MSG_PUTS) {
            msg.type = MSG_PUT_ACK;
            msg.data.clear();
            recv_queue[CHANNEL_ACK].push_back(msg);
        }
        else {
            printf("Unexpected msg type:%d\n", (int)(msg.type));
            assert(0);
        }
        return true;
    }

    virtual void can_recv(BusPortT port, vector<bool> &out) {
        out.assign(CHANNEL_CNT, false);
        for(uint32_t c = 0; c < CHANNEL_CNT; c++) {
            out[c] = (!(recv_queue[c].empty()));
        }
    }
    virtual bool can_recv(BusPortT port, uint32_t channel) {return !recv_queue[channel].empty();};
    virtual bool recv(BusPortT port, uint32_t channel, vector<uint8_t> &buf) {
        if(recv_queue[channel].size()) {
            buf.clear();
            construct_msg_pack(recv_queue[channel].front(), buf);
            recv_queue[channel].pop_front();
            return true;
        }
        return false;
    };

    vector<list<CacheCohenrenceMsg>> recv_queue;
};



bool test_moesi_l1_cache() {
    simcache::CacheParam param;

    uint64_t memsz = 1024UL * 1024UL * 16UL;

    uint8_t *hostmem = new uint8_t[memsz];
    uint8_t *simmem = new uint8_t[memsz];

    memset(hostmem, 0, memsz);
    memset(simmem, 0, memsz);

    TestL1CacheBus bus(simmem, hostmem);

    TestL1CacheBusMapping busmap;
    param.set_offset = 5;
    param.way_cnt = 8;
    param.mshr_num = 6;
    param.index_latency = 2;
    param.index_width = 1;
    L1CacheMoesiDirNoiV2 l1(param, &bus, 1, &busmap, "l1");
    simroot::add_sim_object(&l1, "l1", 1);

    uint64_t round = 1024UL * 1024UL;
    uint64_t log_interval = 1024UL;
    printf("(0/%ld)", round);
    for(uint64_t _n = 0; _n < round; _n++) {
        if(_n % log_interval == 0) {
            printf("\r(%ld/%ld)", _n, round);
            fflush(stdout);
        }

        bool do_write = RAND(0, 2);
        PhysAddrT addr = ((rand_long() % memsz) & (~(7UL)));
        uint64_t data = rand_long();
        bool succ = false;

        while(!succ) {
            if(do_write) {
                succ = (l1.store(addr, 8, &data, false) == SimError::success);
            }
            else {
                succ = (l1.load(addr, 8, &data, false) == SimError::success);
            }
            l1.on_current_tick();
            l1.apply_next_tick();
        }

        if(do_write) {
            *((uint64_t*)(hostmem + addr)) = data;
        }
        else {
            assert(*((uint64_t*)(hostmem + addr)) == data);
        }
    }

    printf("\nPass!!!\n");
    return true;
}


class TestMOESIDMAHandler : public simcache::DMACallBackHandler {
public:
    TestMOESIDMAHandler(std::set<uint64_t> *cbids) : cbids(cbids) {};
    std::set<uint64_t> *cbids;
    virtual void dma_complete_callback(uint64_t callbackid) {
        cbids->erase(callbackid);
    }
};

using simcache::DMARequest;
using simcache::DMAReqType;

bool test_moesi_l1_dma() {
    uint64_t memsz = 1024UL * 1024UL * 16UL;

    uint8_t *hostmem = new uint8_t[memsz];
    uint8_t *simmem = new uint8_t[memsz];

    memset(hostmem, 0, memsz);
    memset(simmem, 0, memsz);

    TestL1CacheBus bus(simmem, hostmem);

    TestL1CacheBusMapping busmap;
    simcache::moesi::DMAL1MoesiDirNoi l1(&bus, 1, &busmap);
    simroot::add_sim_object(&l1, "l1", 1);

    std::set<uint64_t> cbids;
    TestMOESIDMAHandler handler(&cbids);
    l1.set_handler(&handler);

    uint64_t round = 4UL;
    uint64_t log_interval = 4UL;
    printf("(0/%ld)", round);
    for(uint64_t _n = 0; _n < round; _n++) {
        if(_n % log_interval == 0) {
            printf("\r(%ld/%ld)", _n, round);
            fflush(stdout);
        }

        PhysAddrT randstart = rand_long() % memsz;
        PhysAddrT randend = rand_long() % memsz;
        if(randend == randstart) continue;
        if(randstart > randend) std::swap(randstart, randend);
        uint64_t sz = randend - randstart;

        uint8_t *buf = new uint8_t[sz];
        // if(rand() & 1) {
            for(uint64_t i = 0; i < sz; i++) {
                buf[i] = RAND(0,256);
            }
            cbids.insert(1);
            DMARequest req(DMAReqType::host_memory_read, randstart, buf, sz, 1);
            req.vp2pp = std::make_shared<std::unordered_map<VPageIndexT, PageIndexT>>();
            req.pp2vp = std::make_shared<std::unordered_map<PageIndexT, VPageIndexT>>();
            for(VPageIndexT vpi = (randstart >> PAGE_ADDR_OFFSET); vpi < CEIL_DIV(randend, PAGE_LEN_BYTE); vpi++) {
                req.vp2pp->emplace(vpi, vpi);
                req.pp2vp->emplace(vpi, vpi);
            }
            l1.push_dma_request(req);

            while(!cbids.empty()) {
                l1.on_current_tick();
                l1.apply_next_tick();
            }

            for(uint64_t i = 0; i < sz; i++) {
                assert(simmem[randstart + i] == buf[i]);
            }
        // }
        delete[] buf;
    }

    printf("\nPass!!!\n");
    return true;
}


class BusMapping1L24L1 : public BusPortMapping {
public:
    virtual bool get_homenode_port(LineIndexT line, BusPortT *out) {
        if(out) *out = 0;
        return true;
    }
    virtual bool get_subnode_port(LineIndexT line, BusPortT *out) {
        if(out) *out = 1;
        return true;
    }
    virtual bool get_reqnode_port(uint32_t index, BusPortT *out) {
        if(out) *out = index + 2;
        return index < 4;
    };
    virtual bool get_reqnode_index(BusPortT port, uint32_t *out) {
        if(out) *out = port - 2;
        return (port > 1 && port < 6);
    };
};

class MemAddrCtrl1L24L1 : public MemCtrlLineAddrMap {
public:
    MemAddrCtrl1L24L1(uint64_t sz) : sz(sz) {};
    virtual uint64_t get_local_mem_offset(LineIndexT lindex) {
        return (lindex << CACHE_LINE_ADDR_OFFSET);
    };
    virtual bool is_responsible(LineIndexT lindex) {
        return (lindex < (sz >> CACHE_LINE_ADDR_OFFSET));
    };
private:
    uint64_t sz = 0;
};

bool test_cache_1l24l1_seq_wr() {

    vector<BusPortT> nodes;
    for(int i = 0; i < 6; i++) {
        nodes.push_back(i);
    }
    vector<uint32_t> cha_width;
    cha_width.assign(CHANNEL_CNT, 32);
    BusRouteTable route;
    simbus::genroute_double_ring(nodes, route);

    SymmetricMultiChannelBus *bus = new SymmetricMultiChannelBus(nodes, nodes, cha_width, route, "bus");

    const SizeT memsz = 1024UL * 1024UL * 16UL;
    uint8_t *pmem = new uint8_t[memsz];
    MemAddrCtrl1L24L1 mem_addr_ctrl(memsz);
    MemoryNode *mem = new MemoryNode(pmem, &mem_addr_ctrl, bus, 1, 32);

    BusMapping1L24L1 bus_mapping;

    simcache::CacheParam param;
    param.set_offset = param.dir_set_offset = 7;
    param.way_cnt = 8;
    param.dir_way_cnt = 32;
    param.mshr_num = 8;
    param.index_latency = 4;
    param.index_width = 1;

    LLCMoesiDirNoi *l2 = new LLCMoesiDirNoi(
        param,
        bus,
        0,
        &bus_mapping,
        string("l2cache")
    );

    param.set_offset = 5;
    param.way_cnt = 8;
    param.mshr_num = 4;
    param.index_latency = 1;
    param.index_width = 1;
    L1CacheMoesiDirNoiV2 *l1s[4];
    for(int i = 0; i < 4; i++) {
        l1s[i] = new L1CacheMoesiDirNoiV2(
            param, 
            bus,
            i+2,
            &bus_mapping,
            string("l1-") + to_string(i)
        );
    }

    uint64_t tick = 0;
    auto next_tick = [&]()->void {
        for(int i = 0; i < 4; i++) l1s[i]->on_current_tick();
        l2->on_current_tick();
        bus->on_current_tick();
        mem->on_current_tick();
        for(int i = 0; i < 4; i++) l1s[i]->apply_next_tick();
        l2->apply_next_tick();
        bus->apply_next_tick();
        mem->apply_next_tick();
        tick++;
    };

    simroot::add_sim_object(bus, "bus");
    simroot::add_sim_object(l2, "l2");
    simroot::add_sim_object(mem, "mem");
    for(int i = 0; i < 4; i++) simroot::add_sim_object(l1s[i], string("l1-") + to_string(i));

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

    printf("Pass test_cache_1l24l1_seq_wr() !!!\n");

    delete mem;
    delete bus;
    delete l2;
    for(int i = 0; i < 4; i++) delete l1s[i];

    return true;
}

bool test_cache_1l24l1_rand_wr() {

    vector<BusPortT> nodes;
    for(int i = 0; i < 6; i++) {
        nodes.push_back(i);
    }
    vector<uint32_t> cha_width;
    cha_width.assign(CHANNEL_CNT, 32);
    BusRouteTable route;
    simbus::genroute_double_ring(nodes, route);

    SymmetricMultiChannelBus *bus = new SymmetricMultiChannelBus(nodes, nodes, cha_width, route, "bus");


    const SizeT memsz = 1024UL * 1024UL * 16UL;
    uint8_t *pmem = new uint8_t[memsz];
    MemAddrCtrl1L24L1 mem_addr_ctrl(memsz);
    MemoryNode *mem = new MemoryNode(pmem, &mem_addr_ctrl, bus, 1, 32);

    BusMapping1L24L1 bus_mapping;
    
    simcache::CacheParam param;
    param.set_offset = param.dir_set_offset = 7;
    param.way_cnt = 8;
    param.dir_way_cnt = 32;
    param.mshr_num = 8;
    param.index_latency = 4;
    param.index_width = 1;

    LLCMoesiDirNoi *l2 = new LLCMoesiDirNoi(
        param,
        bus,
        0,
        &bus_mapping,
        string("l2cache")
    );

    param.set_offset = 5;
    param.way_cnt = 8;
    param.mshr_num = 4;
    param.index_latency = 1;
    param.index_width = 1;
    L1CacheMoesiDirNoiV2 *l1s[4];
    for(int i = 0; i < 4; i++) {
        l1s[i] = new L1CacheMoesiDirNoiV2(
            param, 
            bus,
            i+2,
            &bus_mapping,
            string("l1-") + to_string(i)
        );
    }
    
    uint64_t tick = 0;
    auto next_tick = [&]()->void {
        for(int i = 0; i < 4; i++) l1s[i]->on_current_tick();
        l2->on_current_tick();
        bus->on_current_tick();
        mem->on_current_tick();
        for(int i = 0; i < 4; i++) l1s[i]->apply_next_tick();
        l2->apply_next_tick();
        bus->apply_next_tick();
        mem->apply_next_tick();
        tick++;
    };

    simroot::add_sim_object(bus, "bus");
    simroot::add_sim_object(l2, "l2");
    simroot::add_sim_object(mem, "mem");
    for(int i = 0; i < 4; i++) simroot::add_sim_object(l1s[i], string("l1-") + to_string(i));


    uint64_t round = 1024UL * 1024UL;
    uint64_t output_interval = 1024UL;

    uint8_t *host_mem = new uint8_t[memsz];
    memset(host_mem, 0, memsz);

    auto perform_cache_op = [](L1CacheMoesiDirNoiV2 *c, bool write, uint64_t addr, uint64_t *buf) -> bool {
        if(write) {
            return (c->store(addr, 8, buf, false) == SimError::success);
        }
        else {
            return (c->load(addr, 8, buf, false) == SimError::success);
        }
    };

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
            if(!succ[0]) succ[0] = perform_cache_op(l1s[0], do_write[0], addrs[0], datas+0);
            if(!succ[1]) succ[1] = perform_cache_op(l1s[1], do_write[1], addrs[1], datas+1);
            if(!succ[2]) succ[2] = perform_cache_op(l1s[2], do_write[2], addrs[2], datas+2);
            if(!succ[3]) succ[3] = perform_cache_op(l1s[3], do_write[3], addrs[3], datas+3);
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

    printf("Pass test_cache_1l24l1_rand_wr() !!!\n");
    fflush(stdout);

    delete mem;
    delete bus;
    delete l2;
    for(int i = 0; i < 4; i++) delete l1s[i];

    return true;
}


bool test_moesi_cache_rand() {
    return test_cache_1l24l1_rand_wr();
}

bool test_moesi_cache_seq() {
    return test_cache_1l24l1_seq_wr();
}



bool test_moesi_l1l2_dcache() {

    simcache::CacheParam param_l1i, param_l1d, param_l2;

    uint64_t memsz = 1024UL * 1024UL * 16UL;

    uint8_t *hostmem = new uint8_t[memsz];
    uint8_t *simmem = new uint8_t[memsz];

    memset(hostmem, 0, memsz);
    memset(simmem, 0, memsz);

    TestL1CacheBus bus(simmem, hostmem);

    bus.debug_log = false;
    // bus.debug_log = true;

    TestL1CacheBusMapping busmap;

    param_l1d.set_offset = 5;
    param_l1d.way_cnt = 8;
    param_l1d.mshr_num = 6;
    param_l1d.index_latency = 2;
    param_l1d.index_width = 1;

    param_l1i.set_offset = 5;
    param_l1i.way_cnt = 4;
    param_l1i.mshr_num = 4;
    param_l1i.index_latency = 1;
    param_l1i.index_width = 2;

    param_l2.set_offset = 7;
    param_l2.way_cnt = 8;
    param_l2.mshr_num = 16;
    param_l2.index_latency = 4;
    param_l2.index_width = 1;

    PrivL1L2Moesi l1l2(param_l2, param_l1d, param_l1i, &bus, 1, &busmap, "l1l2");
    simroot::add_sim_object(&l1l2, "l1", 1);

    l1l2.log_info = bus.debug_log;

    PrivL1L2MoesiL1IPort l1i(&l1l2);
    PrivL1L2MoesiL1DPort l1d(&l1l2);

    uint64_t round = 1024UL * 1024UL;
    uint64_t log_interval = 1024;
    printf("(0/%ld)", round);
    for(uint64_t _n = 0; _n < round; _n++) {
        if(_n % log_interval == 0) {
            printf("\r(%ld/%ld)", _n, round);
            fflush(stdout);
        }

        bool do_write = RAND(0, 2);
        PhysAddrT addr = ((rand_long() % memsz) & (~(7UL)));
        uint64_t data = rand_long();
        bool succ = false;

        if(bus.debug_log) printf("Addr: 0x%lx, Line: 0x%lx, %s\n", addr, addr_to_line_index(addr), do_write?"write":"read");

        while(!succ) {
            if(do_write) {
                succ = (l1d.store(addr, 8, &data, false) == SimError::success);
            }
            else {
                succ = (l1d.load(addr, 8, &data, false) == SimError::success);
            }
            l1l2.on_current_tick();
            l1l2.apply_next_tick();
        }

        if(do_write) {
            *((uint64_t*)(hostmem + addr)) = data;
        }
        else {
            simroot_assert(*((uint64_t*)(hostmem + addr)) == data);
        }
    }

    printf("\nPass!!!\n");
    return true;
    
}

bool test_moesi_l1l2_icache() {

    simcache::CacheParam param_l1i, param_l1d, param_l2;

    uint64_t memsz = 1024UL * 1024UL * 16UL;

    uint8_t *hostmem = new uint8_t[memsz];
    uint8_t *simmem = new uint8_t[memsz];

    memset(hostmem, 0, memsz);
    memset(simmem, 0, memsz);

    for(uint64_t i = 0; i < memsz; i+=8) {
        *((uint64_t*)(hostmem+i)) = rand_long();
    }
    memcpy(simmem, hostmem, memsz);

    TestL1CacheBus bus(simmem, hostmem);

    bus.debug_log = false;
    // bus.debug_log = true;

    TestL1CacheBusMapping busmap;

    param_l1d.set_offset = 5;
    param_l1d.way_cnt = 8;
    param_l1d.mshr_num = 6;
    param_l1d.index_latency = 2;
    param_l1d.index_width = 1;

    param_l1i.set_offset = 5;
    param_l1i.way_cnt = 4;
    param_l1i.mshr_num = 4;
    param_l1i.index_latency = 1;
    param_l1i.index_width = 2;

    param_l2.set_offset = 7;
    param_l2.way_cnt = 8;
    param_l2.mshr_num = 16;
    param_l2.index_latency = 4;
    param_l2.index_width = 1;

    PrivL1L2Moesi l1l2(param_l2, param_l1d, param_l1i, &bus, 1, &busmap, "l1l2");
    simroot::add_sim_object(&l1l2, "l1", 1);

    l1l2.log_info = bus.debug_log;

    PrivL1L2MoesiL1IPort l1i(&l1l2);
    PrivL1L2MoesiL1DPort l1d(&l1l2);

    uint64_t round = 1024UL * 1024UL;
    uint64_t log_interval = 1024;
    printf("(0/%ld)", round);
    for(uint64_t _n = 0; _n < round; _n++) {
        if(_n % log_interval == 0) {
            printf("\r(%ld/%ld)", _n, round);
            fflush(stdout);
        }

        PhysAddrT addr = ((rand_long() % memsz) & (~(7UL)));
        uint64_t data = 0;
        bool succ = false;

        while(!succ) {
            succ = (l1i.load(addr, 8, &data, false) == SimError::success);

            l1l2.on_current_tick();
            l1l2.apply_next_tick();
        }

        simroot_assert(*((uint64_t*)(hostmem + addr)) == data);
    }

    printf("\nPass!!!\n");
    return true;
    
}

bool test_moesi_l1l2_cache() {
    printf("Test L1-I:\n");
    assert(test_moesi_l1l2_icache());
    printf("Test L1-D:\n");
    assert(test_moesi_l1l2_dcache());
    return true;
}




bool test_moesi_cache_l1l2l3_seq() {

    vector<BusPortT> nodes;
    for(int i = 0; i < 6; i++) {
        nodes.push_back(i);
    }
    vector<uint32_t> cha_width;
    cha_width.assign(CHANNEL_CNT, 32);
    BusRouteTable route;
    simbus::genroute_double_ring(nodes, route);

    SymmetricMultiChannelBus *bus = new SymmetricMultiChannelBus(nodes, nodes, cha_width, route, "bus");

    const SizeT memsz = 1024UL * 1024UL * 16UL;
    uint8_t *pmem = new uint8_t[memsz];
    MemAddrCtrl1L24L1 mem_addr_ctrl(memsz);
    MemoryNode *mem = new MemoryNode(pmem, &mem_addr_ctrl, bus, 1, 32);

    BusMapping1L24L1 bus_mapping;

    simcache::CacheParam param;
    param.set_offset = param.dir_set_offset = 10;
    param.way_cnt = 8;
    param.dir_way_cnt = 32;
    param.mshr_num = 8;
    param.index_latency = 10;
    param.index_width = 1;

    LLCMoesiDirNoi *l3 = new LLCMoesiDirNoi(
        param,
        bus,
        0,
        &bus_mapping,
        string("l3cache")
    );

    // l3->do_log = true;

    simcache::CacheParam param_l1i, param_l1d, param_l2;

    param_l1d.set_offset = 5;
    param_l1d.way_cnt = 8;
    param_l1d.mshr_num = 6;
    param_l1d.index_latency = 2;
    param_l1d.index_width = 1;

    param_l1i.set_offset = 5;
    param_l1i.way_cnt = 4;
    param_l1i.mshr_num = 4;
    param_l1i.index_latency = 1;
    param_l1i.index_width = 2;

    param_l2.set_offset = 7;
    param_l2.way_cnt = 8;
    param_l2.mshr_num = 16;
    param_l2.index_latency = 4;
    param_l2.index_width = 1;

    PrivL1L2Moesi *l1l2s[4];
    PrivL1L2MoesiL1IPort *l1is[4];
    PrivL1L2MoesiL1DPort *l1ds[4];
    for(int i = 0; i < 4; i++) {
        l1l2s[i] = new PrivL1L2Moesi(
            param_l2, param_l1d, param_l1i, 
            bus,
            i+2,
            &bus_mapping,
            string("l1-") + to_string(i)
        );
        // l1l2s[i]->log_info = true;
        l1is[i] = new PrivL1L2MoesiL1IPort(l1l2s[i]);
        l1ds[i] = new PrivL1L2MoesiL1DPort(l1l2s[i]);
    }

    uint64_t tick = 0;
    auto next_tick = [&]()->void {
        for(int i = 0; i < 4; i++) l1l2s[i]->on_current_tick();
        l3->on_current_tick();
        bus->on_current_tick();
        mem->on_current_tick();
        for(int i = 0; i < 4; i++) l1l2s[i]->apply_next_tick();
        l3->apply_next_tick();
        bus->apply_next_tick();
        mem->apply_next_tick();
        tick++;
    };

    simroot::add_sim_object(bus, "bus");
    simroot::add_sim_object(l3, "l3");
    simroot::add_sim_object(mem, "mem");
    for(int i = 0; i < 4; i++) simroot::add_sim_object(l1l2s[i], string("l1-") + to_string(i));

    for(SizeT i = 0; i < memsz; i += sizeof(uint64_t) * 2UL) {
        uint64_t d0 = i;
        uint64_t d1 = i + sizeof(uint64_t);
        bool succ0 = false, succ1 = false;
        while(!(succ0 && succ1)) {
            if(!succ0) succ0 = (l1ds[0]->store(d0, 8, &d0, false) == SimError::success);
            if(!succ1) succ1 = (l1ds[1]->store(d1, 8, &d1, false) == SimError::success);
            next_tick();
        }
    }

    for(SizeT i = 0; i < memsz; i += sizeof(uint64_t) * 2UL) {
        uint64_t a0 = i, a1 = i + sizeof(uint64_t);
        uint64_t d0 = 0, d1 = 0;
        bool succ0 = false, succ1 = false;
        while(!(succ0 && succ1)) {
            if(!succ0) succ0 = (l1ds[2]->load(a0, 8, &d0, false) == SimError::success);
            if(!succ1) succ1 = (l1ds[3]->load(a1, 8, &d1, false) == SimError::success);
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
            if(!succ0) succ0 = (l1ds[0]->store(a0, 8, &d0, false) == SimError::success);
            if(!succ1) succ1 = (l1ds[1]->store(a1, 8, &d1, false) == SimError::success);
            next_tick();
        }
    }

    for(SizeT i = 0; i < memsz; i += sizeof(uint64_t) * 2UL) {
        uint64_t a0 = i, a1 = i + sizeof(uint64_t);
        uint64_t e0 = memsz - a0, e1 = memsz - a1;
        uint64_t d0 = 0, d1 = 0;
        bool succ0 = false, succ1 = false;
        while(!(succ0 && succ1)) {
            if(!succ0) succ0 = (l1ds[2]->load(a0, 8, &d0, false) == SimError::success);
            if(!succ1) succ1 = (l1ds[3]->load(a1, 8, &d1, false) == SimError::success);
            next_tick();
        }
        assert(d0 == e0);
        assert(d1 == e1);
    }

    printf("Succ 2\n");

    printf("Pass test_moesi_cache_l1l2l3_seq() !!!\n");

    delete mem;
    delete bus;
    delete l3;
    for(int i = 0; i < 4; i++) {
        delete l1is[i];
        delete l1ds[i];
        delete l1l2s[i];
    }

    return true;
}

bool test_moesi_cache_l1l2l3_rand() {

    
    vector<BusPortT> nodes;
    for(int i = 0; i < 6; i++) {
        nodes.push_back(i);
    }
    vector<uint32_t> cha_width;
    cha_width.assign(CHANNEL_CNT, 32);
    BusRouteTable route;
    simbus::genroute_double_ring(nodes, route);

    SymmetricMultiChannelBus *bus = new SymmetricMultiChannelBus(nodes, nodes, cha_width, route, "bus");

    const SizeT memsz = 1024UL * 1024UL * 16UL;
    uint8_t *pmem = new uint8_t[memsz];
    MemAddrCtrl1L24L1 mem_addr_ctrl(memsz);
    MemoryNode *mem = new MemoryNode(pmem, &mem_addr_ctrl, bus, 1, 32);

    BusMapping1L24L1 bus_mapping;

    simcache::CacheParam param;
    param.set_offset = param.dir_set_offset = 10;
    param.way_cnt = 8;
    param.dir_way_cnt = 32;
    param.mshr_num = 8;
    param.index_latency = 10;
    param.index_width = 1;

    LLCMoesiDirNoi *l3 = new LLCMoesiDirNoi(
        param,
        bus,
        0,
        &bus_mapping,
        string("l3cache")
    );

    simcache::CacheParam param_l1i, param_l1d, param_l2;

    param_l1d.set_offset = 5;
    param_l1d.way_cnt = 8;
    param_l1d.mshr_num = 6;
    param_l1d.index_latency = 2;
    param_l1d.index_width = 1;

    param_l1i.set_offset = 5;
    param_l1i.way_cnt = 4;
    param_l1i.mshr_num = 4;
    param_l1i.index_latency = 1;
    param_l1i.index_width = 2;

    param_l2.set_offset = 7;
    param_l2.way_cnt = 8;
    param_l2.mshr_num = 16;
    param_l2.index_latency = 4;
    param_l2.index_width = 1;

    PrivL1L2Moesi *l1l2s[4];
    PrivL1L2MoesiL1IPort *l1is[4];
    PrivL1L2MoesiL1DPort *l1ds[4];
    for(int i = 0; i < 4; i++) {
        l1l2s[i] = new PrivL1L2Moesi(
            param_l2, param_l1d, param_l1i, 
            bus,
            i+2,
            &bus_mapping,
            string("l1-") + to_string(i)
        );
        l1is[i] = new PrivL1L2MoesiL1IPort(l1l2s[i]);
        l1ds[i] = new PrivL1L2MoesiL1DPort(l1l2s[i]);
        // l1l2s[i]->log_info = true;
    }

    uint64_t tick = 0;
    auto next_tick = [&]()->void {
        for(int i = 0; i < 4; i++) l1l2s[i]->on_current_tick();
        l3->on_current_tick();
        bus->on_current_tick();
        mem->on_current_tick();
        for(int i = 0; i < 4; i++) l1l2s[i]->apply_next_tick();
        l3->apply_next_tick();
        bus->apply_next_tick();
        mem->apply_next_tick();
        tick++;
    };

    simroot::add_sim_object(bus, "bus");
    simroot::add_sim_object(l3, "l3");
    simroot::add_sim_object(mem, "mem");
    for(int i = 0; i < 4; i++) simroot::add_sim_object(l1l2s[i], string("l1-") + to_string(i));


    uint64_t round = 1024UL * 1024UL;
    uint64_t output_interval = 1024UL;

    uint8_t *host_mem = new uint8_t[memsz];
    memset(host_mem, 0, memsz);

    auto perform_cache_op = [](PrivL1L2MoesiL1DPort *c, bool write, uint64_t addr, uint64_t *buf) -> bool {
        if(write) {
            return (c->store(addr, 8, buf, false) == SimError::success);
        }
        else {
            return (c->load(addr, 8, buf, false) == SimError::success);
        }
    };

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
            if(!succ[0]) succ[0] = perform_cache_op(l1ds[0], do_write[0], addrs[0], datas+0);
            if(!succ[1]) succ[1] = perform_cache_op(l1ds[1], do_write[1], addrs[1], datas+1);
            if(!succ[2]) succ[2] = perform_cache_op(l1ds[2], do_write[2], addrs[2], datas+2);
            if(!succ[3]) succ[3] = perform_cache_op(l1ds[3], do_write[3], addrs[3], datas+3);
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

    printf("Pass test_moesi_cache_l1l2l3_rand() !!!\n");

    delete mem;
    delete bus;
    delete l3;
    for(int i = 0; i < 4; i++) {
        delete l1is[i];
        delete l1ds[i];
        delete l1l2s[i];
    }

    return true;
}



class BusMapping1L24L1NUCA : public BusPortMapping {
public:
    virtual bool get_homenode_port(LineIndexT line, BusPortT *out) {
        if(out) *out = (line % 4UL);
        return true;
    }
    virtual bool get_subnode_port(LineIndexT line, BusPortT *out) {
        if(out) *out = 4;
        return true;
    }
    virtual bool get_reqnode_port(uint32_t index, BusPortT *out) {
        if(out) *out = index + 5;
        return index < 4;
    };
    virtual bool get_reqnode_index(BusPortT port, uint32_t *out) {
        if(out) *out = port - 5;
        return (port > 4 && port < 9);
    };
};

bool test_moesi_cache_l3nuca_rand() {

    
    vector<BusNodeT> nodes;
    for(int i = 0; i < 5; i++) {
        nodes.push_back(i);
    }
    vector<uint32_t> cha_width;
    cha_width.assign(CHANNEL_CNT, 32);
    BusRouteTable route;
    simbus::genroute_double_ring(nodes, route);

    vector<BusPortT> ports;
    vector<BusNodeT> port2node;
    for(int i = 0; i < 4; i++) {
        ports.push_back(i);
        port2node.push_back(i);
        ports.push_back(i+5);
        port2node.push_back(i);
    }
    ports.push_back(4);
    port2node.push_back(4);

    SymmetricMultiChannelBus *bus = new SymmetricMultiChannelBus(ports, port2node, cha_width, route, "bus");

    const SizeT memsz = 1024UL * 1024UL * 16UL;
    uint8_t *pmem = new uint8_t[memsz];
    MemAddrCtrl1L24L1 mem_addr_ctrl(memsz);
    MemoryNode *mem = new MemoryNode(pmem, &mem_addr_ctrl, bus, 4, 32);

    BusMapping1L24L1NUCA bus_mapping;

    simcache::CacheParam param;
    param.set_offset = param.dir_set_offset = 8;
    param.way_cnt = 8;
    param.dir_way_cnt = 32;
    param.mshr_num = 8;
    param.index_latency = 10;
    param.index_width = 1;

    LLCMoesiDirNoi *l3s[4];
    for(int i = 0; i < 4; i++) {
        param.nuca_num = 4;
        param.nuca_index = i;
        l3s[i] = new LLCMoesiDirNoi(
            param,
            bus,
            i,
            &bus_mapping,
            string("l3-") + to_string(i)
        );
    }
    
    simcache::CacheParam param_l1i, param_l1d, param_l2;

    param_l1d.set_offset = 5;
    param_l1d.way_cnt = 8;
    param_l1d.mshr_num = 6;
    param_l1d.index_latency = 2;
    param_l1d.index_width = 1;

    param_l1i.set_offset = 5;
    param_l1i.way_cnt = 4;
    param_l1i.mshr_num = 4;
    param_l1i.index_latency = 1;
    param_l1i.index_width = 2;

    param_l2.set_offset = 7;
    param_l2.way_cnt = 8;
    param_l2.mshr_num = 16;
    param_l2.index_latency = 4;
    param_l2.index_width = 1;

    PrivL1L2Moesi *l1l2s[4];
    PrivL1L2MoesiL1IPort *l1is[4];
    PrivL1L2MoesiL1DPort *l1ds[4];
    for(int i = 0; i < 4; i++) {
        l1l2s[i] = new PrivL1L2Moesi(
            param_l2, param_l1d, param_l1i, 
            bus,
            i+5,
            &bus_mapping,
            string("l1-") + to_string(i)
        );
        l1is[i] = new PrivL1L2MoesiL1IPort(l1l2s[i]);
        l1ds[i] = new PrivL1L2MoesiL1DPort(l1l2s[i]);
        // l1l2s[i]->log_info = true;
    }

    uint64_t tick = 0;
    auto next_tick = [&]()->void {
        for(int i = 0; i < 4; i++) l1l2s[i]->on_current_tick();
        for(int i = 0; i < 4; i++) l3s[i]->on_current_tick();
        bus->on_current_tick();
        mem->on_current_tick();
        for(int i = 0; i < 4; i++) l1l2s[i]->apply_next_tick();
        for(int i = 0; i < 4; i++) l3s[i]->apply_next_tick();
        bus->apply_next_tick();
        mem->apply_next_tick();
        tick++;
    };

    simroot::add_sim_object(bus, "bus");
    simroot::add_sim_object(mem, "mem");
    for(int i = 0; i < 4; i++) simroot::add_sim_object(l1l2s[i], string("l1-") + to_string(i));
    for(int i = 0; i < 4; i++) simroot::add_sim_object(l3s[i], string("l3-") + to_string(i));


    uint64_t round = 1024UL * 1024UL;
    uint64_t output_interval = 1024UL;

    uint8_t *host_mem = new uint8_t[memsz];
    memset(host_mem, 0, memsz);

    auto perform_cache_op = [](PrivL1L2MoesiL1DPort *c, bool write, uint64_t addr, uint64_t *buf) -> bool {
        if(write) {
            return (c->store(addr, 8, buf, false) == SimError::success);
        }
        else {
            return (c->load(addr, 8, buf, false) == SimError::success);
        }
    };

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
            if(!succ[0]) succ[0] = perform_cache_op(l1ds[0], do_write[0], addrs[0], datas+0);
            if(!succ[1]) succ[1] = perform_cache_op(l1ds[1], do_write[1], addrs[1], datas+1);
            if(!succ[2]) succ[2] = perform_cache_op(l1ds[2], do_write[2], addrs[2], datas+2);
            if(!succ[3]) succ[3] = perform_cache_op(l1ds[3], do_write[3], addrs[3], datas+3);
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

    printf("Pass test_moesi_cache_l3nuca_rand() !!!\n");

    delete mem;
    delete bus;
    for(int i = 0; i < 4; i++) {
        delete l1is[i];
        delete l1ds[i];
        delete l1l2s[i];
        delete l3s[i];
    }

    return true;
}




}


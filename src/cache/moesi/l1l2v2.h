#ifndef RVSIM_CACHE_MOESI_L1L2_V2_H
#define RVSIM_CACHE_MOESI_L1L2_V2_H

#include "protocal.h"

#include "cache/cacheinterface.h"
#include "cache/cachecommon.h"

#include "bus/businterface.h"

#include "common.h"
#include "tickqueue.h"

namespace simcache {
namespace moesi {

using simbus::BusInterfaceV2;
using simbus::BusPortT;
using simbus::BusPortMapping;

class PrivL1L2Moesi : public SimObject {

public:

    PrivL1L2Moesi(
        CacheParam &l2_param,
        CacheParam &l1d_param,
        CacheParam &l1i_param,
        BusInterfaceV2 *bus,
        BusPortT my_port_id,
        BusPortMapping *busmap,
        string logname
    );

    // SimObject
    virtual void on_current_tick() { main_on_current_tick(); };
    virtual void apply_next_tick() { main_apply_next_tick(); };

    virtual void dump_core(std::ofstream &ofile);

    friend class PrivL1L2MoesiL1DPort;
    friend class PrivL1L2MoesiL1IPort;
protected:

// ------------- Bus Port ---------------

    BusInterfaceV2 *bus = nullptr;
    uint16_t my_port_id = 0;
    BusPortMapping *busmap;

    // bool has_recieved = false;
    // bool has_processed = false;
    CacheCohenrenceMsg *recv_msg = nullptr;

    typedef struct {
        vector<uint8_t>     msg;
        BusPortT            dst;
        uint32_t            cha;
    } ReadyToSend;

    std::list<ReadyToSend> send_buf;
    uint32_t send_buf_size = 8;
    inline void push_send_buf(BusPortT dst, uint32_t channel, uint32_t type, LineIndexT line, uint32_t arg) {
        send_buf.emplace_back();
        auto &send = send_buf.back();
        send.dst = dst;
        send.cha = channel;
        CacheCohenrenceMsg tmp;
        tmp.type = type;
        tmp.line = line;
        tmp.arg = arg;
        construct_msg_pack(tmp, send.msg);
    }
    inline void push_send_buf_with_line(BusPortT dst, uint32_t channel, uint32_t type, LineIndexT line, uint32_t arg, void* linebuf) {
        send_buf.emplace_back();
        auto &send = send_buf.back();
        send.dst = dst;
        send.cha = channel;
        CacheCohenrenceMsg tmp;
        tmp.type = type;
        tmp.line = line;
        tmp.arg = arg;
        tmp.data.resize(CACHE_LINE_LEN_BYTE);
        cache_line_copy(tmp.data.data(), linebuf);
        construct_msg_pack(tmp, send.msg);
    }

    inline void cur_recieve_msg() {
        if(recv_msg) return;
        vector<uint8_t> buf;
        for(uint32_t c = 0; c < CHANNEL_CNT; c++) {
            if(!bus->can_recv(my_port_id, c)) continue;
            simroot_assert(bus->recv(my_port_id, c, buf));
            recv_msg = new CacheCohenrenceMsg;
            parse_msg_pack(buf, *recv_msg);
            // has_recieved = true;
            break;
        }
    }
    inline void cur_send_msg() {
        if(send_buf.empty()) return;
        vector<bool> can_send;
        bus->can_send(my_port_id, can_send);
        for(auto iter = send_buf.begin(); iter != send_buf.end(); ) {
            if(can_send[iter->cha]) {
                simroot_assert(bus->send(my_port_id, iter->dst, iter->cha, iter->msg));
                can_send[iter->cha] = false;
                iter = send_buf.erase(iter);
            }
            else {
                iter++;
            }
        }
    }

// ------------- L2 Cache Block ---------------

    const uint32_t L2FLG_IN_I           = (1<<0);
    const uint32_t L2FLG_IN_D           = (1<<1);
    const uint32_t L2FLG_IN_D_W         = (1<<2);

    typedef struct {
        uint64_t    data[CACHE_LINE_LEN_I64];
        uint32_t    state;
        uint32_t    flag;
    } TagedCacheLine;

    const uint32_t MSHR_FIFLG_L1IREQ    = (1<<0);
    const uint32_t MSHR_FIFLG_L1DREQS   = (1<<1);
    const uint32_t MSHR_FIFLG_L1DREQM   = (1<<2);

    typedef struct {
        uint64_t line_buf[CACHE_LINE_LEN_I64];
        
        uint32_t state = 0;
        uint32_t line_flag = 0;
        uint32_t finish_flag = 0;

        uint8_t get_data_ready = 0;
        uint8_t get_ack_cnt_ready = 0;
        uint16_t need_invalid_ack = 0;
        uint16_t invalid_ack = 0;
    } MSHREntry;

    unique_ptr<GenericLRUCacheBlock<TagedCacheLine>> block;
    unique_ptr<MSHRArray<MSHREntry>> mshrs;

// ------------- L2 Main Pipeline ---------------

    set<LineIndexT>             processing_line;    // 对每一个CacheLine同时只能有一个事务
    vector<CacheCohenrenceMsg*> waiting_msg;        // 等待当前事务处理完成
    uint32_t                    waiting_buf_size = 8;

    uint32_t index_cycle = 4;

    typedef struct {
        LineIndexT              lindex = 0;
        CacheCohenrenceMsg*     msg = nullptr;
        uint32_t                index_cycle = 0;
    } ProcessingPackage;

    unique_ptr<SimpleTickQueue<ProcessingPackage*>> queue_index;

    void p1_fetch();
    void p2_index();

    void handle_new_line_nolock(LineIndexT lindex, MSHREntry *mshr, uint32_t init_state);

    void insert_to_l1i(LineIndexT lindex, void * line_buf);
    void insert_to_l1d(LineIndexT lindex, void * line_buf, bool writable);

    void snoop_l1d_and_set_readonly(LineIndexT lindex, void * l2_line_buf);
    void snoop_l1_and_invalid(LineIndexT lindex, void * l2_line_buf);

    void main_on_current_tick();
    void main_apply_next_tick();
    bool is_main_cur = true;    // 保证main_on_current_tick和main_apply_next_tick每周期被交替调用一次

// ------------- L1 Types ---------------

    const uint32_t L1FLG_WRITE      = (1<<0);
    const uint32_t L1FLG_DIRTY      = (1<<1);

    const uint32_t L1REQ_GETS       = 1;
    const uint32_t L1REQ_GETM       = 2;
    // const uint32_t L1REQ_PUT_CLEAN  = 3;
    // const uint32_t L1REQ_PUT_DIRTY  = 4;

    typedef struct {
        uint32_t        type = 0;
        bool            indexing = false;
        LineIndexT      lindex = 0;
        vector<uint8_t> data;
    } L1Request;

// ------------- L1I Cache ---------------

    unique_ptr<GenericLRUCacheBlock<TagedCacheLine>> l1i_block;

    L1Request   l1i_req;

    uint32_t    l1i_busy_cycle = 0; // 由于L2Cache通过总线访问L1 Cache Block对L1流水线产生的阻塞

    uint32_t l1i_query_width;
    std::vector<SimpleTickQueue<CacheOP*>> l1i_ld_queues;
    vector<ArrivalLine> l1i_newlines;

    SimError l1i_load(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid);

    void l1i_clear_ld(std::list<CacheOP*> *to_free);
    bool l1i_is_empty();

    void l1i_on_current_tick() { main_on_current_tick(); };
    void l1i_apply_next_tick() { main_apply_next_tick(); };

// ------------- L1D Cache ---------------

    unique_ptr<GenericLRUCacheBlock<TagedCacheLine>> l1d_block;

    bool reserved_address_valid = false;
    PhysAddrT reserved_address = 0;

    L1Request   l1d_req;

    uint32_t    l1d_busy_cycle = 0; // 由于L2Cache通过总线访问L1 Cache Block对L1流水线产生的阻塞

    uint32_t l1d_query_width;
    std::vector<SimpleTickQueue<CacheOP*>> l1d_ld_queues;
    std::vector<SimpleTickQueue<CacheOP*>> l1d_st_queues;
    std::vector<SimpleTickQueue<CacheOP*>> l1d_amo_queues;
    std::vector<SimpleTickQueue<CacheOP*>> l1d_misc_queues;
    vector<ArrivalLine> l1d_newlines;

    SimError l1d_load(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid);
    SimError l1d_store(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid);
    SimError l1d_amo(PhysAddrT paddr, uint32_t len, void *buf, isa::RV64AMOOP5 amoop);
    SimError l1d_load_reserved(PhysAddrT paddr, uint32_t len, void *buf);
    SimError l1d_store_conditional(PhysAddrT paddr, uint32_t len, void *buf);

    void l1d_clear_ld(std::list<CacheOP*> *to_free);
    void l1d_clear_st(std::list<CacheOP*> *to_free);
    void l1d_clear_amo(std::list<CacheOP*> *to_free);
    void l1d_clear_misc(std::list<CacheOP*> *to_free);
    bool l1d_is_empty();

    void l1d_on_current_tick() { main_on_current_tick(); };
    void l1d_apply_next_tick() { main_apply_next_tick(); };

// ------------- Log ---------------

    struct {
        uint64_t    l1i_hit_count = 0;
        uint64_t    l1i_miss_count = 0;
        uint64_t    l1d_hit_count = 0;
        uint64_t    l1d_miss_count = 0;

        uint64_t    l2_hit_count = 0;
        uint64_t    l2_miss_count = 0;
    } statistic;

    string logname;
    char log_buf[512];
public:
    bool log_info = false;
};

class PrivL1L2MoesiL1DPort : public CacheInterfaceV2 {
public:
    PrivL1L2MoesiL1DPort(PrivL1L2Moesi *l1l2_controler) : l1l2_controler(l1l2_controler) {
        ld_input = &(l1l2_controler->l1d_ld_queues.front());
        ld_output = &(l1l2_controler->l1d_ld_queues.back());
        st_input = &(l1l2_controler->l1d_st_queues.front());
        st_output = &(l1l2_controler->l1d_st_queues.back());
        amo_input = &(l1l2_controler->l1d_amo_queues.front());
        amo_output = &(l1l2_controler->l1d_amo_queues.back());
        misc_input = &(l1l2_controler->l1d_misc_queues.front());
        misc_output = &(l1l2_controler->l1d_misc_queues.back());
    }

    // CacheInterface
    virtual SimError load(PhysAddrT paddr, uint32_t len, void *buf, bool noblock) {
        vector<bool> tmp;
        return l1l2_controler->l1d_load(paddr, len, buf, tmp);
    }
    virtual SimError store(PhysAddrT paddr, uint32_t len, void *buf, bool noblock) {
        vector<bool> tmp;
        return l1l2_controler->l1d_store(paddr, len, buf, tmp);
    }
    virtual SimError load_reserved(PhysAddrT paddr, uint32_t len, void *buf) {
        return l1l2_controler->l1d_load_reserved(paddr, len, buf);
    }
    virtual SimError store_conditional(PhysAddrT paddr, uint32_t len, void *buf) {
        return l1l2_controler->l1d_store_conditional(paddr, len, buf);
    }
    virtual uint32_t arrival_line(vector<ArrivalLine> *out) {
        uint32_t ret = arrivals.size();
        if(out) out->insert(out->end(), arrivals.begin(), arrivals.end());
        return ret;
    }

    // CacheInterfaceV2
    virtual void clear_ld(std::list<CacheOP*> *to_free) {
        l1l2_controler->l1d_clear_ld(to_free);
    }
    virtual void clear_st(std::list<CacheOP*> *to_free) {
        l1l2_controler->l1d_clear_st(to_free);
    }
    virtual void clear_amo(std::list<CacheOP*> *to_free) {
        l1l2_controler->l1d_clear_amo(to_free);
    }
    virtual void clear_misc(std::list<CacheOP*> *to_free) {
        l1l2_controler->l1d_clear_misc(to_free);
    }

    virtual bool is_empty() {
        return l1l2_controler->l1d_is_empty();
    }

    // SimObject
    virtual void on_current_tick() {
        l1l2_controler->l1d_on_current_tick();
    }
    virtual void apply_next_tick() {
        l1l2_controler->l1d_apply_next_tick();
        arrivals.swap(l1l2_controler->l1d_newlines);
        l1l2_controler->l1d_newlines.clear();
    }

private:
    PrivL1L2Moesi *l1l2_controler;
};

class PrivL1L2MoesiL1IPort : public CacheInterfaceV2 {
public:
    PrivL1L2MoesiL1IPort(PrivL1L2Moesi *l1l2_controler) : l1l2_controler(l1l2_controler) {
        ld_input = &(l1l2_controler->l1i_ld_queues.front());
        ld_output = &(l1l2_controler->l1i_ld_queues.back());
        st_input = nullptr;
        st_output = nullptr;
        amo_input = nullptr;
        amo_output = nullptr;
        misc_input = nullptr;
        misc_output = nullptr;
    }

    // CacheInterface
    virtual SimError load(PhysAddrT paddr, uint32_t len, void *buf, bool noblock) {
        vector<bool> tmp;
        return l1l2_controler->l1i_load(paddr, len, buf, tmp);
    }
    virtual SimError store(PhysAddrT paddr, uint32_t len, void *buf, bool noblock) {
        simroot_assertf(0, "Cannot perform STORE on L1ICache!!!");
        return SimError::success;
    }
    virtual SimError load_reserved(PhysAddrT paddr, uint32_t len, void *buf) {
        simroot_assertf(0, "Cannot perform AMO-LR on L1ICache!!!");
        return SimError::success;
    }
    virtual SimError store_conditional(PhysAddrT paddr, uint32_t len, void *buf) {
        simroot_assertf(0, "Cannot perform AMO-SC on L1ICache!!!");
        return SimError::success;
    }
    virtual uint32_t arrival_line(vector<ArrivalLine> *out) {
        uint32_t ret = arrivals.size();
        if(out) out->insert(out->end(), arrivals.begin(), arrivals.end());
        return ret;
    }

    // CacheInterfaceV2
    virtual void clear_ld(std::list<CacheOP*> *to_free) {
        l1l2_controler->l1i_clear_ld(to_free);
    }
    virtual void clear_st(std::list<CacheOP*> *to_free) {}
    virtual void clear_amo(std::list<CacheOP*> *to_free) {}
    virtual void clear_misc(std::list<CacheOP*> *to_free) {}

    virtual bool is_empty() {
        return l1l2_controler->l1i_is_empty();
    }

    // SimObject
    virtual void on_current_tick() {
        l1l2_controler->l1i_on_current_tick();
    }
    virtual void apply_next_tick() {
        l1l2_controler->l1i_apply_next_tick();
        arrivals.swap(l1l2_controler->l1i_newlines);
        l1l2_controler->l1i_newlines.clear();
    }

private:
    PrivL1L2Moesi *l1l2_controler;
};

}}

#endif

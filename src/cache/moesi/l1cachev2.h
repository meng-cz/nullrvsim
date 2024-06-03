#ifndef RVSIM_CACHE_MOESI_L1_V2_H
#define RVSIM_CACHE_MOESI_L1_V2_H

#include "cache/cacheinterface.h"
#include "cache/cachecommon.h"
#include "cache/coherence.h"

#include "bus/businterface.h"

#include "common.h"
#include "tickqueue.h"

namespace simcache {
namespace moesi {

using simbus::BusInterface;
using simbus::BusPortT;
using simbus::BusPortMapping;

class L1CacheMoesiDirNoiV2 : public CacheInterfaceV2 {

public:

    L1CacheMoesiDirNoiV2(
        uint32_t set_addr_offset,
        uint32_t line_per_set,
        uint32_t mshr_num,
        uint32_t query_cycle,
        uint32_t query_width,
        BusInterface *bus,
        BusPortT my_port_id,
        BusPortMapping *busmap,
        string logname
    );

    // CacheInterface
    virtual void clear_ld(std::list<CacheOP*> *to_free);
    virtual void clear_st(std::list<CacheOP*> *to_free);
    virtual void clear_amo(std::list<CacheOP*> *to_free);
    virtual void clear_misc(std::list<CacheOP*> *to_free);

    virtual bool is_empty();

    // SimObject
    virtual void on_current_tick();
    virtual void apply_next_tick();

    virtual void dump_core(std::ofstream &ofile);

    bool do_log = false;

protected:
    SimError load(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid);
    inline SimError load(PhysAddrT paddr, uint32_t len, void *buf) {
        vector<bool> tmp;
        return load(paddr, len, buf, tmp);
    }

    SimError store(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid);
    inline SimError store(PhysAddrT paddr, uint32_t len, void *buf) {
        vector<bool> tmp;
        return store(paddr, len, buf, tmp);
    }

    SimError load_reserved(PhysAddrT paddr, uint32_t len, void *buf);

    SimError store_conditional(PhysAddrT paddr, uint32_t len, void *buf);

    SimError amo(PhysAddrT paddr, uint32_t len, void *buf, isa::RV64AMOOP5 amoop);

    bool has_recieved = false;
    bool has_processed = false;
    CacheCohenrenceMsg msgbuf;

    BusInterface *bus = nullptr;
    uint16_t my_port_id = 0;
    BusPortMapping *busmap;

    uint32_t query_width;
    std::vector<SimpleTickQueue<CacheOP*>> ld_queues;
    std::vector<SimpleTickQueue<CacheOP*>> st_queues;
    std::vector<SimpleTickQueue<CacheOP*>> amo_queues;

    std::list<std::pair<BusPortT, CacheCohenrenceMsg>> send_buf;
    uint32_t send_buf_size = 4;
    inline void push_send_buf(BusPortT dst, CCMsgType type, LineIndexT line, uint32_t arg) {
        send_buf.emplace_back();
        send_buf.back().first = dst;
        auto &send = send_buf.back().second;
        send.type = type;
        send.line = line;
        send.arg = arg;
    }
    inline void push_send_buf_with_line(BusPortT dst, CCMsgType type, LineIndexT line, uint32_t arg, uint64_t* linebuf) {
        push_send_buf(dst, type, line, arg);
        auto &d = send_buf.back().second.data;
        d.assign(CACHE_LINE_LEN_BYTE, 0);
        cache_line_copy(d.data(), linebuf);
    }

    GenericLRUCacheBlock<DefaultCacheLine> block;

    MSHRArray mshrs;

    void recieve_msg_nolock();
    void handle_received_msg_nolock();
    void send_msg_nolock();

    vector<ArrivalLine> newlines;
    void handle_new_line_nolock(LineIndexT lindex, MSHREntry *mshr, CacheLineState init_state);

    bool reserved_address_valid = false;
    PhysAddrT reserved_address = 0;

    struct {
        uint64_t l1_hit_count = 0;
        uint64_t l1_miss_count = 0;
        uint64_t l2_request_count = 0;
        double   l2_avg_respond_cycle = 0;
    } statistic;

    string log_name;
    char log_buf[128];

    bool debug_log = false;

};

}}

#endif

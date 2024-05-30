#ifndef RVSIM_CACHE_MOESI_L1_H
#define RVSIM_CACHE_MOESI_L1_H

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

class L1CacheMoesiDirNoi : public CacheInterface {

public:

    L1CacheMoesiDirNoi(
        uint32_t set_addr_offset,
        uint32_t line_per_set,
        uint32_t mshr_num,
        BusInterface *bus,
        BusPortT my_port_id,
        BusPortMapping *busmap,
        string logname
    );

    // CacheInterface
    virtual SimError load(PhysAddrT paddr, uint32_t len, void *buf, bool is_amo = false);

    virtual SimError store(PhysAddrT paddr, uint32_t len, void *buf, bool is_amo = false);

    virtual SimError load_reserved(PhysAddrT paddr, uint32_t len, void *buf);

    virtual SimError store_conditional(PhysAddrT paddr, uint32_t len, void *buf);

    virtual uint32_t arrival_line(vector<ArrivalLine> *out);

    // SimObject
    virtual void on_current_tick();
    virtual void apply_next_tick();

    virtual void dump_core(std::ofstream &ofile);

    bool do_log = false;

protected:

    SpinLock lock;
    bool has_recieved = false;
    bool has_processed = false;
    CacheCohenrenceMsg msgbuf;

    BusInterface *bus = nullptr;
    uint16_t my_port_id = 0;
    BusPortMapping *busmap;

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

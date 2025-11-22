// MIT License

// Copyright (c) 2024 Meng Chengzhen, in Shandong University

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef RVSIM_CACHE_MOESI_L1_V2_H
#define RVSIM_CACHE_MOESI_L1_V2_H

#include "cache/cacheinterface.h"
#include "cache/cachecommon.h"

#include "protocal.h"

#include "bus/businterface.h"

#include "common.h"
#include "tickqueue.h"

namespace simcache {
namespace moesi {

using simbus::BusInterfaceV2;
using simbus::BusPortT;
using simbus::BusPortMapping;

class L1CacheMoesiDirNoiV2 : public CacheInterfaceV2 {

public:

    L1CacheMoesiDirNoiV2(
        CacheParam &param,
        BusInterfaceV2 *bus,
        BusPortT my_port_id,
        BusPortMapping *busmap,
        string logname
    );

    // CacheInterface
    virtual SimError load(PhysAddrT paddr, uint32_t len, void *buf, bool noblock) {
        return load(paddr, len, buf);
    }
    virtual SimError store(PhysAddrT paddr, uint32_t len, void *buf, bool noblock) {
        return store(paddr, len, buf);
    }
    virtual SimError load_reserved(PhysAddrT paddr, uint32_t len, void *buf);
    virtual SimError store_conditional(PhysAddrT paddr, uint32_t len, void *buf);
    virtual uint32_t arrival_line(vector<ArrivalLine> *out) {
        uint32_t ret = arrivals.size();
        if(out) out->insert(out->end(), arrivals.begin(), arrivals.end());
        return ret;
    }

    // CacheInterfaceV2
    virtual void clear_ld(std::list<CacheOP*> *to_free);
    virtual void clear_st(std::list<CacheOP*> *to_free);
    virtual void clear_amo(std::list<CacheOP*> *to_free);
    virtual void clear_misc(std::list<CacheOP*> *to_free);

    virtual bool is_empty();

    // SimObject
    virtual void on_current_tick();
    virtual void apply_next_tick();

    virtual void print_statistic(std::ofstream &ofile);
    virtual void print_setup_info(std::ofstream &ofile);

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

    SimError amo(PhysAddrT paddr, uint32_t len, void *buf, isa::RV64AMOOP5 amoop);

    bool has_recieved = false;
    bool has_processed = false;
    CacheCohenrenceMsg msgbuf;

    CacheParam param;

    BusInterfaceV2 *bus = nullptr;
    uint16_t my_port_id = 0;
    BusPortMapping *busmap;

    uint32_t query_width;
    std::vector<SimpleTickQueue<CacheOP*>> ld_queues;
    std::vector<SimpleTickQueue<CacheOP*>> st_queues;
    std::vector<SimpleTickQueue<CacheOP*>> amo_queues;
    std::vector<SimpleTickQueue<CacheOP*>> misc_queues;

    typedef struct {
        vector<uint8_t>     msg;
        BusPortT            dst;
        uint32_t            cha;
    } ReadyToSend;

    std::list<ReadyToSend> send_buf;
    uint32_t send_buf_size = 4;
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

    typedef struct {
        uint64_t    data[CACHE_LINE_LEN_I64];
        uint32_t    state;
    } TagedCacheLine;
        
    typedef struct {
        uint64_t line_buf[CACHE_LINE_LEN_I64];
        
        uint32_t state = 0;

        uint8_t get_data_ready = 0;
        uint8_t get_ack_cnt_ready = 0;
        uint16_t need_invalid_ack = 0;
        uint16_t invalid_ack = 0;

        uint64_t log_start_cycle = 0;
    } MSHREntry;

    unique_ptr<GenericLRUCacheBlock<TagedCacheLine>> block;

    unique_ptr<MSHRArray<MSHREntry>> mshrs;

    void recieve_msg_nolock();
    void handle_received_msg_nolock();
    void send_msg_nolock();

    vector<ArrivalLine> newlines;
    void handle_new_line_nolock(LineIndexT lindex, MSHREntry *mshr, uint32_t init_state);

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

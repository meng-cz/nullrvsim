#ifndef RVSIM_CACHE_MOESI_LLC_H
#define RVSIM_CACHE_MOESI_LLC_H

#include "cache/cacheinterface.h"
#include "cache/cachecommon.h"
#include "cache/coherence.h"

#include "bus/businterface.h"

#include "common.h"
#include "tickqueue.h"

namespace simcache {
namespace moesi {

using simbus::BusInterface;
using simbus::BusPortMapping;
using simbus::BusPortT;

class LLCMoesiDirNoi : public SimObject {

public:
    LLCMoesiDirNoi(
        uint32_t set_addr_offset,
        uint32_t line_per_set,
        uint32_t dir_set_addr_offset,
        uint32_t dir_line_per_set,
        BusInterface *bus,
        BusPortT my_port_id,
        BusPortMapping *busmap,
        string logname
    );

    virtual void on_current_tick();
    virtual void apply_next_tick();
    
    virtual void dump_core(std::ofstream &ofile);

    bool do_log = false;
    string logname;

protected:
    char log_buf[256];

    BusInterface *bus = nullptr;
    BusPortT my_port_id;
    BusPortMapping *busmap = nullptr;

    std::list<CacheCohenrenceMsg> recv_buf;
    uint32_t recv_buf_size = 4;

    std::set<LineIndexT> processing_lindex;

    typedef struct {
        std::set<uint32_t>      exists;
        uint32_t                owner = 0;
        bool                    dirty = false;
    } DirEntry;

    GenericLRUCacheBlock<DefaultCacheLine> block;
    GenericLRUCacheBlock<DirEntry> directory;

    typedef struct {
        CCMsgType       type;
        LineIndexT      lindex;
        uint32_t        arg;

        bool            blk_hit = false;
        uint8_t         line_buf[CACHE_LINE_LEN_BYTE];

        bool            dir_hit = false;
        DirEntry        entry;

        bool            blk_replaced = false;
        LineIndexT      lindex_replaced = 0;

        std::list<std::pair<BusPortT, CacheCohenrenceMsg>>   need_send;

        bool            delay_commit = false;
        bool            dir_evict = false;

        inline void push_send_buf(BusPortT dst, CCMsgType type, LineIndexT line, uint32_t arg) {
            need_send.emplace_back();
            need_send.back().first = dst;
            auto &send = need_send.back().second;
            send.type = type;
            send.line = line;
            send.arg = arg;
        }
        inline void push_send_buf_with_line(BusPortT dst, CCMsgType type, LineIndexT line, uint32_t arg, uint64_t* linebuf) {
            push_send_buf(dst, type, line, arg);
            auto &d = need_send.back().second.data;
            d.assign(CACHE_LINE_LEN_BYTE, 0);
            cache_line_copy(d.data(), linebuf);
        }
    } RequestPackage;


    SimpleTickQueue<RequestPackage*> queue_index;
    SimpleTickQueue<RequestPackage*> queue_writeback;
    SimpleTickQueue<RequestPackage*> queue_index_result;

    uint32_t index_current_cycle = 1;
    uint32_t index_cycle_count = 2;

    std::list<RequestPackage*> process_buf;
    uint32_t process_buf_size = 4;

    void p1_fetch();
    void p2_index();
    void p3_process();
};


}
}


#endif

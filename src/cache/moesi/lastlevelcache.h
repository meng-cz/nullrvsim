#ifndef RVSIM_CACHE_MOESI_LLC_H
#define RVSIM_CACHE_MOESI_LLC_H

#include "cache/cacheinterface.h"
#include "cache/cachecommon.h"
#include "cache/trace.h"

#include "protocal.h"

#include "bus/businterface.h"

#include "common.h"
#include "tickqueue.h"

namespace simcache {
namespace moesi {

using simbus::BusInterfaceV2;
using simbus::BusPortMapping;
using simbus::BusPortT;

class LLCMoesiDirNoi : public SimObject {

public:
    LLCMoesiDirNoi(
        CacheParam &param,
        BusInterfaceV2 *bus,
        BusPortT my_port_id,
        BusPortMapping *busmap,
        string logname,
        CacheEventTrace *trace
    );

    virtual void on_current_tick();
    virtual void apply_next_tick();
    
    virtual void print_statistic(std::ofstream &ofile);
    virtual void print_setup_info(std::ofstream &ofile);

    virtual void dump_core(std::ofstream &ofile);

    bool do_log = false;
    string logname;

protected:
    char log_buf[256];

    CacheParam param;

    BusInterfaceV2 *bus = nullptr;
    BusPortT my_port_id;
    BusPortMapping *busmap = nullptr;

    uint64_t nuca_num = 1;
    uint64_t nuca_index = 0;
    inline bool nuca_check(LineIndexT lindex) {
        return ((lindex % nuca_num) == nuca_index);
    }
    inline LineIndexT lindex_to_nuca_tag(LineIndexT lindex) {
        return lindex / nuca_num;
    }
    inline LineIndexT nuca_tag_to_lindex(LineIndexT nuca_tag) {
        return (nuca_tag * nuca_num) + nuca_index;
    }

    uint32_t index_cycle = 4;

    std::list<CacheCohenrenceMsg> recv_buf;
    uint32_t recv_buf_size = 4;

    std::set<LineIndexT> processing_lindex;

    typedef struct {
        std::set<uint32_t>      exists;
        uint32_t                owner = 0;
        bool                    dirty = false;
    } DirEntry;

    unique_ptr<GenericLRUCacheBlock<CacheLineT>> block;
    unique_ptr<GenericLRUCacheBlock<DirEntry>> directory;

    typedef struct {
        vector<uint8_t>     msg;
        BusPortT            dst;
        uint32_t            cha;
    } ReadyToSend;

    typedef struct {
        uint32_t        type;
        LineIndexT      lindex;
        uint32_t        arg;
        uint32_t        transid;

        uint32_t        index_cycle;

        bool            blk_hit = false;
        uint8_t         line_buf[CACHE_LINE_LEN_BYTE];

        bool            dir_hit = false;
        DirEntry        entry;

        bool            blk_replaced = false;
        LineIndexT      lindex_replaced = 0;

        std::list<ReadyToSend>   need_send;

        bool            delay_commit = false;
        bool            dir_evict = false;

        inline void push_send_buf(BusPortT dst, uint32_t channel, uint32_t type, LineIndexT line, uint32_t arg, uint32_t transid) {
            need_send.emplace_back();
            auto &send = need_send.back();
            send.dst = dst;
            send.cha = channel;
            CacheCohenrenceMsg tmp;
            tmp.type = type;
            tmp.line = line;
            tmp.arg = arg;
            tmp.transid = transid;
            construct_msg_pack(tmp, send.msg);
        }
        inline void push_send_buf_with_line(BusPortT dst, uint32_t channel, uint32_t type, LineIndexT line, uint32_t arg, uint8_t* linebuf, uint32_t transid) {
            need_send.emplace_back();
            auto &send = need_send.back();
            send.dst = dst;
            send.cha = channel;
            CacheCohenrenceMsg tmp;
            tmp.type = type;
            tmp.line = line;
            tmp.arg = arg;
            tmp.transid = transid;
            tmp.data.resize(CACHE_LINE_LEN_BYTE);
            cache_line_copy(tmp.data.data(), linebuf);
            construct_msg_pack(tmp, send.msg);
        }
    } RequestPackage;

    SimpleTickQueue<RequestPackage*> queue_index;
    SimpleTickQueue<RequestPackage*> queue_writeback;
    SimpleTickQueue<RequestPackage*> queue_index_result;

    std::list<RequestPackage*> process_buf;
    uint32_t process_buf_size = 4;

    void p1_fetch();
    void p2_index();
    void p3_process();

    CacheEventTrace *trace = nullptr;

    struct {
        uint64_t llc_hit_count = 0;
        uint64_t llc_miss_count = 0;
    } statistic;

};


}
}


#endif

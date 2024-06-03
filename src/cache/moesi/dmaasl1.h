#ifndef RVSIM_CACHE_MOESI_DMA_AS_L1_H
#define RVSIM_CACHE_MOESI_DMA_AS_L1_H

#include "cache/cacheinterface.h"
#include "cache/cachecommon.h"
#include "cache/coherence.h"
#include "cache/dmainterface.h"

#include "bus/businterface.h"

#include "common.h"
#include "spinlocks.h"

namespace simcache {
namespace moesi {

using simbus::BusInterface;
using simbus::BusPortT;
using simbus::BusPortMapping;

class DMAL1MoesiDirNoi : public SimDMADevice, public SimObject {

public:

    DMAL1MoesiDirNoi(
        BusInterface *bus,
        BusPortT my_port_id,
        BusPortMapping *busmap
    );

    // SimDMADevice
    virtual void set_handler(DMACallBackHandler *handler) {
        this->handler = handler;
    }
    virtual void push_dma_requests(std::list<DMARequest> &req) {
        // printf("Add %ld DMA Reqs\n", req.size());
        push_lock.lock();
        arrival_reqs.splice(arrival_reqs.end(), req);
        push_lock.unlock();
    }

    // SimObject
    virtual void on_current_tick();
    virtual void apply_next_tick() {
        dma_req_queue.splice(dma_req_queue.end(), arrival_reqs);
        arrival_reqs.clear();
    }

    bool do_log = false;

protected:

    BusInterface *bus = nullptr;
    uint16_t my_port_id = 0;
    BusPortMapping *busmap;

    DMACallBackHandler *handler = nullptr;

    SpinLock push_lock;
    std::list<DMARequest> arrival_reqs;
    std::list<DMARequest> dma_req_queue;

    ProcessingDMAReq *current = nullptr;
    std::unordered_map<LineIndexT, ProcessingDMAReq*> wait_lines;

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

    MSHRArray mshrs;

    void handle_recv_msg(CacheCohenrenceMsg &msg);

    char log_buf[128];

    bool debug_log = false;

};

}}


#endif

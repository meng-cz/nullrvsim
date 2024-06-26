#ifndef RVSIM_CACHE_MOESI_DMA_AS_L1_H
#define RVSIM_CACHE_MOESI_DMA_AS_L1_H

#include "cache/cacheinterface.h"
#include "cache/cachecommon.h"
#include "cache/dmainterface.h"

#include "protocal.h"

#include "bus/businterface.h"

#include "common.h"
#include "spinlocks.h"

namespace simcache {
namespace moesi {

using simbus::BusInterfaceV2;
using simbus::BusPortT;
using simbus::BusPortMapping;

class DMAL1MoesiDirNoi : public SimDMADevice, public SimObject {

public:

    DMAL1MoesiDirNoi(
        BusInterfaceV2 *bus,
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

    BusInterfaceV2 *bus = nullptr;
    uint16_t my_port_id = 0;
    BusPortMapping *busmap;

    DMACallBackHandler *handler = nullptr;

    SpinLock push_lock;
    std::list<DMARequest> arrival_reqs;
    std::list<DMARequest> dma_req_queue;

    ProcessingDMAReq *current = nullptr;
    std::unordered_map<LineIndexT, ProcessingDMAReq*> wait_lines;

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

    MSHRArray mshrs;

    void handle_recv_msg(CacheCohenrenceMsg &msg);

    char log_buf[128];

    bool debug_log = false;

};

}}


#endif

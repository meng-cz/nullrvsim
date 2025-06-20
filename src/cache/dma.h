
#ifndef RVSIM_CACHE_DMA_H
#define RVSIM_CACHE_DMA_H

#include "common.h"

#include "dmainterface.h"
#include "cacheinterface.h"

namespace simcache {

class DMAAsCore : public SimDMADevice, public SimObject {

public:

    DMAAsCore(CacheInterface *l1cache);

    virtual void set_handler(DMACallBackHandler *handler);
    virtual void push_dma_requests(std::list<DMARequestUnit> &req);

    virtual void on_current_tick();
    virtual void apply_next_tick();

protected:

    CacheInterface * port = nullptr;
    DMACallBackHandler * handler = nullptr;

    SpinLock push_lock;
    std::list<DMARequestUnit> arrival_reqs;
    std::list<DMARequestUnit> dma_req_queue;

    bool dma_working = false;
    DMARequestUnit processing;
    uint64_t cur_pos = 0;
    uint8_t line_buf[CACHE_LINE_LEN_BYTE];


};



}

#endif

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

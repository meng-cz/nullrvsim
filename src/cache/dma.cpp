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


#include "dma.h"

#include "simroot.h"

namespace simcache {

DMAAsCore::DMAAsCore(CacheInterface *l1cache) : port(l1cache) {

}

void DMAAsCore::set_handler(DMACallBackHandler *handler) {
    this->handler = handler;
}

void DMAAsCore::push_dma_requests(std::list<DMARequestUnit> &req) {
    push_lock.lock();
    arrival_reqs.splice(arrival_reqs.end(), req);
    push_lock.unlock();
}

void DMAAsCore::on_current_tick() {
    port->on_current_tick();

    if(!dma_working && !dma_req_queue.empty()) {
        dma_working = true;
        processing = dma_req_queue.front();
        cur_pos = 0;
        dma_req_queue.pop_front();
    }
    if(!dma_working) return;

    if(processing.flag == DMAFLG_DST_HOST) {
        uint8_t * dstp = (uint8_t*)(processing.dst) + cur_pos;
        PhysAddrT srcp = processing.src + cur_pos;
        LineIndexT cache_line = (srcp >> CACHE_LINE_ADDR_OFFSET);
        uint64_t cache_offset = (srcp & (CACHE_LINE_LEN_BYTE - 1));
        SimError ret = port->load(cache_line * CACHE_LINE_LEN_BYTE, CACHE_LINE_LEN_BYTE, line_buf, true);
        simroot_assert(ret == SimError::success || ret == SimError::busy || ret == SimError::miss || ret == SimError::coherence);
        if(ret != SimError::success) return;
        uint64_t read_size = std::min<uint64_t>(CACHE_LINE_LEN_BYTE - cache_offset, processing.size - cur_pos);
        memcpy(dstp, line_buf + cache_offset, read_size);
        cur_pos += read_size;
        if(cur_pos >= processing.size) {
            cur_pos = 0;
            dma_working = false;
            if(processing.callback) handler->dma_complete_callback(processing.callback);
        }
    } else {
        PhysAddrT dstp = processing.dst + cur_pos;
        uint8_t * srcp = (uint8_t*)(processing.src) + cur_pos;
        LineIndexT cache_line = (dstp >> CACHE_LINE_ADDR_OFFSET);
        uint64_t cache_offset = (dstp & (CACHE_LINE_LEN_BYTE - 1));
        SimError ret = port->store(cache_line * CACHE_LINE_LEN_BYTE, 0, line_buf, true);
        simroot_assert(ret == SimError::success || ret == SimError::busy || ret == SimError::miss || ret == SimError::coherence);
        if(ret != SimError::success) return;
        simroot_assert(SimError::success == port->load(cache_line * CACHE_LINE_LEN_BYTE, CACHE_LINE_LEN_BYTE, line_buf, true));
        uint64_t read_size = std::min<uint64_t>(CACHE_LINE_LEN_BYTE - cache_offset, processing.size - cur_pos);
        memcpy(line_buf + cache_offset,  srcp, read_size);
        simroot_assert(SimError::success == port->store(cache_line * CACHE_LINE_LEN_BYTE, CACHE_LINE_LEN_BYTE, line_buf, true));
        cur_pos += read_size;
        if(cur_pos >= processing.size) {
            cur_pos = 0;
            dma_working = false;
            if(processing.callback) handler->dma_complete_callback(processing.callback);
        }
    }
}

void DMAAsCore::apply_next_tick() {
    port->apply_next_tick();
    dma_req_queue.splice(dma_req_queue.end(), arrival_reqs);
    arrival_reqs.clear();
}



}


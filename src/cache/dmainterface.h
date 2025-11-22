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

#ifndef RVSIM_CACHE_DMA_INTERFACE_H
#define RVSIM_CACHE_DMA_INTERFACE_H

#include "common.h"
#include "simerror.h"

namespace simcache {

// enum class DMAReqType {
//     host_memory_read,
//     host_memory_write,
// };

// class DMARequest {
// public:
//     DMARequest(DMAReqType type, VirtAddrT vaddr, uint8_t *hostptr, uint64_t size, uint64_t callbackid) {
//         this->type = type;
//         this->vaddr = vaddr;
//         this->hostptr = hostptr;
//         this->size = size;
//         this->callbackid = callbackid;
//         this->vp2pp = std::make_shared<std::unordered_map<VPageIndexT, PageIndexT>>();
//         this->pp2vp = std::make_shared<std::unordered_map<PageIndexT, VPageIndexT>>();
//     }

//     DMAReqType  type;
//     VirtAddrT   vaddr = 0;
//     uint8_t *   hostptr;
//     uint64_t    size = 0;
//     uint64_t    callbackid = 0; // 0 for no call-back
//     std::shared_ptr<std::unordered_map<VPageIndexT, PageIndexT>> vp2pp;
//     std::shared_ptr<std::unordered_map<PageIndexT, VPageIndexT>> pp2vp;

//     inline PhysAddrT dma_va_to_pa(VirtAddrT vaddr) {
//         return (vaddr & (PAGE_LEN_BYTE - 1)) + (vp2pp->at(vaddr >> PAGE_ADDR_OFFSET) << PAGE_ADDR_OFFSET);
//     }
//     inline VirtAddrT dma_pa_to_va(PhysAddrT paddr) {
//         return (paddr & (PAGE_LEN_BYTE - 1)) + (pp2vp->at(paddr >> PAGE_ADDR_OFFSET) << PAGE_ADDR_OFFSET);
//     }
// };

const uint32_t DMAFLG_SRC_HOST = (1 << 0);
const uint32_t DMAFLG_DST_HOST = (1 << 1);

typedef struct {
    PhysAddrT   src = 0;
    PhysAddrT   dst = 0;
    uint32_t    size = 0;
    uint32_t    flag = 0;
    uint64_t    callback = 0;
} DMARequestUnit;

class DMACallBackHandler {
public:
    virtual void dma_complete_callback(uint64_t callbackid) = 0;
};

class SimDMADevice {
public:
    virtual void set_handler(DMACallBackHandler *handler) = 0;
    void push_dma_request(DMARequestUnit &req) {
        std::list<DMARequestUnit> reqs;
        reqs.emplace_back(req);
        this->push_dma_requests(reqs);
    }
    virtual void push_dma_requests(std::list<DMARequestUnit> &req) = 0;

};

}

#endif

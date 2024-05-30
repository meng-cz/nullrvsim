#ifndef RVSIM_CACHE_DMA_INTERFACE_H
#define RVSIM_CACHE_DMA_INTERFACE_H

#include "common.h"
#include "simerror.h"

namespace simcache {

enum class DMAReqType {
    host_memory_read,
    host_memory_write,
};

class DMARequest {
public:
    DMARequest(DMAReqType type, VirtAddrT vaddr, uint8_t *hostptr, uint64_t size, uint64_t callbackid) {
        this->type = type;
        this->vaddr = vaddr;
        this->hostptr = hostptr;
        this->size = size;
        this->callbackid = callbackid;
        this->vp2pp = std::make_shared<std::unordered_map<VPageIndexT, PageIndexT>>();
        this->pp2vp = std::make_shared<std::unordered_map<PageIndexT, VPageIndexT>>();
    }

    DMAReqType  type;
    VirtAddrT   vaddr = 0;
    uint8_t *   hostptr;
    uint64_t    size = 0;
    uint64_t    callbackid = 0; // 0 for no call-back
    std::shared_ptr<std::unordered_map<VPageIndexT, PageIndexT>> vp2pp;
    std::shared_ptr<std::unordered_map<PageIndexT, VPageIndexT>> pp2vp;

    inline PhysAddrT dma_va_to_pa(VirtAddrT vaddr) {
        return (vaddr & (PAGE_LEN_BYTE - 1)) + (vp2pp->at(vaddr >> PAGE_ADDR_OFFSET) << PAGE_ADDR_OFFSET);
    }
    inline VirtAddrT dma_pa_to_va(PhysAddrT paddr) {
        return (paddr & (PAGE_LEN_BYTE - 1)) + (pp2vp->at(paddr >> PAGE_ADDR_OFFSET) << PAGE_ADDR_OFFSET);
    }
};

class ProcessingDMAReq {
public:
    ProcessingDMAReq(DMARequest &req) : req(req) {};
    
    DMARequest              req;
    std::list<LineIndexT>   line_todo;
    std::set<LineIndexT>    line_wait_finish;
};

class DMACallBackHandler {
public:
    virtual void dma_complete_callback(uint64_t callbackid) = 0;
};

class SimDMADevice {
public:
    virtual void set_handler(DMACallBackHandler *handler) = 0;
    void push_dma_request(DMARequest &req) {
        std::list<DMARequest> reqs;
        reqs.emplace_back(req);
        this->push_dma_requests(reqs);
    }
    virtual void push_dma_requests(std::list<DMARequest> &req) = 0;


};

}

#endif

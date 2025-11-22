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

#ifndef RVSIM_CACHE_SCC_LLC_H
#define RVSIM_CACHE_SCC_LLC_H

#include "common.h"

#include "cache/cachecommon.h"
#include "cache/cacheinterface.h"

namespace simcache {

enum class SCCReq {
    none,
    read,
    readu,
    put
};

enum class SCCSnoop {
    none,
    share,
    uniq,
    inval
};

typedef struct alignas(64) {
    // Master
    SCCReq      req_next;
    SCCReq      req_previous;
    LineIndexT  req_line_next;
    uint8_t     pad0[64 - sizeof(req_next) - sizeof(req_previous) - sizeof(req_line_next)];
    
    int32_t     req_resp;
    SCCSnoop    snoop;
    LineIndexT  snoop_line;
    uint8_t     pad1[64 - sizeof(req_resp) - sizeof(snoop) - sizeof(snoop_line)];

    // Slave
    SCCReq      req;
    LineIndexT  req_line;
    uint8_t     pad2[64 - sizeof(req) - sizeof(req_line)];

    int32_t     req_resp_vld_next;
    int32_t     req_resp_vld_previous;
    SCCSnoop    snoop_next;
    SCCSnoop    snoop_previous;
    LineIndexT  snoop_line_next;
    uint8_t     pad3[64 - sizeof(req_resp_vld_next) - sizeof(req_resp_vld_previous) - sizeof(snoop_next) - sizeof(snoop_previous) - sizeof(snoop_line_next)];

    // Share
    uint64_t    req_line_buf[CACHE_LINE_LEN_I64];
    uint64_t    snoop_line_buf[CACHE_LINE_LEN_I64];

    int32_t     index;
    uint8_t     pad4[64 - sizeof(index)];
} SCCBundle;

typedef struct {
    SCCReq      req;
    SCCReq      req_next;
    LineIndexT  req_line;
    int32_t     req_resp_vld;
    int32_t     req_resp_vld_next;
    uint64_t    req_line_buf[CACHE_LINE_LEN_I64];
} SCCNCBundle;

class SimpleCoherentLLCWithMem : public SimObject {

public:
    SimpleCoherentLLCWithMem(
        CacheParam &param,
        uint32_t port_num, uint32_t bus_width,
        uint64_t mem_size, PhysAddrT mem_base_addr, uint32_t memory_latency
    );

    SCCBundle * get_port(uint32_t idx) { return &(ports[idx]); };

    virtual void on_current_tick();
    virtual void apply_next_tick();
    
    virtual void print_statistic(std::ofstream &ofile) {};
    virtual void print_setup_info(std::ofstream &ofile) {};

    virtual void dump_core(std::ofstream &ofile) {};

    uint8_t * get_pmem() { return mem.data(); }

    bool debug = false;
protected:

    CacheParam llc_param;
    uint32_t bus_latency;
    PhysAddrT mem_base_addr;
    uint32_t memory_latency;

    typedef struct {
        uint32_t                    owner;
        unordered_set<uint32_t>     shared;
    } SCCHigherState;

    unordered_map<LineIndexT, SCCHigherState> state;

    unique_ptr<GenericLRUCacheBlock<CacheLineT>> llc_cb;

    uint32_t port_num;
    uint32_t current = 0;
    uint32_t busy = 0;
    vector<SCCBundle> ports;

    uint32_t set_snoop = 0;
    bool need_snoop_data = false;
    uint32_t who_need_snoop_data = 0;
    uint32_t wait_snoop_data = 0;

    vector<uint8_t> mem;

    uint32_t handle_read(uint32_t idx);
    uint32_t handle_readu(uint32_t idx);
    uint32_t handle_put(uint32_t idx);
    
    char log_buf[256];
};

}

#endif

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

#ifndef RVSIM_CACHE_SCC_L1_H
#define RVSIM_CACHE_SCC_L1_H

#include "common.h"

#include "cache/cachecommon.h"
#include "cache/cacheinterface.h"

#include "sccllc.h"

namespace simcache {

class SimpleCoherentL1 : public CacheInterface {

public:

    SimpleCoherentL1(CacheParam &param, SCCBundle *port);

    virtual void on_current_tick();
    virtual void apply_next_tick();
    
    virtual void print_statistic(std::ofstream &ofile) {};
    virtual void print_setup_info(std::ofstream &ofile) {};

    virtual void dump_core(std::ofstream &ofile) {};

    virtual SimError load(PhysAddrT paddr, uint32_t len, void *buf, bool noblock);
    virtual SimError store(PhysAddrT paddr, uint32_t len, void *buf, bool noblock);
    virtual SimError load_reserved(PhysAddrT paddr, uint32_t len, void *buf);
    virtual SimError store_conditional(PhysAddrT paddr, uint32_t len, void *buf);
    virtual uint32_t arrival_line(vector<ArrivalLine> *out);

    bool debug = false;
protected:

    CacheParam param;
    SCCBundle *port;

    unique_ptr<GenericLRUCacheBlock<CacheLineT>> cb;
    unordered_set<LineIndexT> writeable;

    SCCReq waiting = SCCReq::none;
    LineIndexT wait_line = 0;
    CacheLineT put_line_data;

    vector<ArrivalLine> arrival;

    bool reserved_address_valid = false;
    PhysAddrT reserved_address = 0;

    char log_buf[256];
};

}

#endif

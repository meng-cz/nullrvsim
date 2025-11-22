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

#ifndef RVSIM_CACHE_MOESI_MEM_NODE_H
#define RVSIM_CACHE_MOESI_MEM_NODE_H

#include "common.h"

#include "protocal.h"

#include "cache/meminterface.h"
#include "cache/trace.h"

#include "bus/businterface.h"

namespace simcache {
namespace moesi {

using simbus::BusInterfaceV2;
using simbus::BusPortT;

class MemoryNode : public SimObject {

public:

    MemoryNode(
        uint8_t *memblk,
        MemCtrlLineAddrMap *addr_map,
        BusInterfaceV2 *bus,
        BusPortT my_port,
        uint32_t dwidth,
        CacheEventTrace *trace
    );

    virtual void print_statistic(std::ofstream &ofile);
    virtual void print_setup_info(std::ofstream &ofile);

    virtual void on_current_tick();

protected:

    uint8_t *memblk = nullptr;
    MemCtrlLineAddrMap *addr_map = nullptr;
    BusInterfaceV2 *bus = nullptr;
    BusPortT my_port = 0;
    uint32_t dwidth = 0;

    typedef struct {
        LineIndexT  lindex = 0;
        uint64_t    hostoff = 0;
        uint32_t    transid = 0;
        uint16_t    op = 0; // 0:Read 1:Write
        BusPortT    src_port;
        uint32_t    processed = 0;
        uint8_t     linebuf[CACHE_LINE_LEN_BYTE];
    } MemoryAccessBuf;

    uint32_t memory_access_buf_size = 4;
    std::list<MemoryAccessBuf> membufs;

    struct {
        uint64_t request_precossed = 0;
        uint64_t busy_cycles = 0;
    } statistic;

    CacheEventTrace *trace = nullptr;
    char log_buf[256];
};

}}


#endif

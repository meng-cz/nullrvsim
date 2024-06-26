#ifndef RVSIM_CACHE_MOESI_MEM_NODE_H
#define RVSIM_CACHE_MOESI_MEM_NODE_H

#include "common.h"

#include "protocal.h"

#include "cache/meminterface.h"
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
        uint32_t dwidth
    );

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
        uint16_t    op = 0; // 0:Read 1:Write
        BusPortT    src_port;
        uint32_t    processed = 0;
        uint8_t     linebuf[CACHE_LINE_LEN_BYTE];
    } MemoryAccessBuf;

    uint32_t memory_access_buf_size = 4;
    std::list<MemoryAccessBuf> membufs;
};

}}


#endif

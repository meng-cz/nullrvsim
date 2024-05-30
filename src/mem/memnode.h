#ifndef RVSIM_MEMORY_NODE_H
#define RVSIM_MEMORY_NODE_H

#include "common.h"

#include "bus/businterface.h"

#include "cache/coherence.h"

namespace simmem {

using simbus::BusInterface;
using simbus::BusPortT;

using simcache::CacheCohenrenceMsg;
using simcache::CCMsgType;

class MemCtrlLineAddrMap {
public:
    virtual uint64_t get_local_mem_offset(LineIndexT lindex) = 0;
    virtual bool is_responsible(LineIndexT lindex) = 0;
};

class MemoryNode : public SimObject {

public:

    MemoryNode(
        uint8_t *memblk,
        MemCtrlLineAddrMap *addr_map,
        BusInterface *bus,
        BusPortT my_port,
        uint32_t dwidth
    );

    virtual void on_current_tick();

protected:

    uint8_t *memblk = nullptr;
    MemCtrlLineAddrMap *addr_map = nullptr;
    BusInterface *bus = nullptr;
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

}


#endif

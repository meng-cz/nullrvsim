#ifndef RVSIM_MEM_INTERFACE_H
#define RVSIM_MEM_INTERFACE_H

#include "common.h"

namespace simcache {

class MemCtrlLineAddrMap {
public:
    /**
     * 计算某CacheLine的首地址在本节点的HostMemory中的偏移量
     */
    virtual uint64_t get_local_mem_offset(LineIndexT lindex) = 0;

    /**
     * 该CacheLine所在的模拟内存段是否由本节点负责
     */
    virtual bool is_responsible(LineIndexT lindex) = 0;
};


}

#endif

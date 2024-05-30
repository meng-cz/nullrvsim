#ifndef RVSIM_CACHE_INTERFACE_H
#define RVSIM_CACHE_INTERFACE_H

#include "common.h"
#include "simerror.h"

#include "coherence.h"

namespace simcache {

typedef struct {
    LineIndexT  lindex;
    bool        readonly;
} ArrivalLine;

class CacheInterface : public SimObject {
public:

    /// @brief 尝试索引CacheBlock并读取指定长度的数据
    /// @param paddr 物理地址
    /// @param len 读取长度
    /// @param buf 数据缓冲区，可用长度至少为参数len
    /// @param noblock Cache在本周期内是否还能响应后续请求
    /// @return success：成功索引。invalidaddr：无效的物理地址。unaligned：未对齐的内存访问。miss：未命中但已经在处理。busy：流水线满或MSHR满导致暂时无法响应。coherence：该行在MSHR中处理一致性暂时无法响应。
    virtual SimError load(PhysAddrT paddr, uint32_t len, void *buf, bool noblock = false) = 0;

    /// @brief 尝试索引CacheBlock并保存指定长度的数据
    /// @param paddr 物理地址
    /// @param len 保存长度
    /// @param buf 数据缓冲区，可用长度至少为参数len
    /// @param noblock Cache在本周期内是否还能响应后续请求
    /// @return success：成功索引。invalidaddr：无效的物理地址。unaligned：未对齐的内存访问。miss：未命中但已经在处理。busy：流水线满或MSHR满导致暂时无法响应。coherence：该行在MSHR中处理一致性暂时无法响应。
    virtual SimError store(PhysAddrT paddr, uint32_t len, void *buf, bool noblock = false) = 0;

    /// @brief Try to perform a L.R. operation on a value in current cycle
    /// @param paddr simulated-physical-address
    /// @param len data length in byte
    /// @param buf pointer to data value
    /// @return 同load函数
    virtual SimError load_reserved(PhysAddrT paddr, uint32_t len, void *buf) = 0;

    /// @brief Try to perform a S.C. operation on a value in current cycle
    /// @param paddr simulated-physical-address
    /// @param len data length in byte
    /// @param buf pointer to data value
    /// @return unconditional：获取标志位失败。此外同store函数。
    virtual SimError store_conditional(PhysAddrT paddr, uint32_t len, void *buf) = 0;

    /// @brief Get the index of the lines arrived in current cycle
    /// @return Count
    virtual uint32_t arrival_line(vector<ArrivalLine> *out) = 0;
};

}

#endif


#ifndef RVSIM_CSR_H
#define RVSIM_CSR_H

#include "common.h"

namespace isa {

#define RV64_CSR_NUM_MASK (0xfff)

enum class CSRNumber {
    fflags      = 0x001,
    frm         = 0x002,
    fcsr        = 0x003,

    cycle       = 0xc00,
    time        = 0xc01,
    instret     = 0xc02,
    cycleh      = 0xc80,
    timeh       = 0xc81,
    instreth    = 0xc82,
};

template <int BitPos = 0, int BitLen = 64>
inline RawDataT rv64_csrrw(RawDataT *p_csr, RawDataT src) {
    static_assert(BitPos >= 0);
    static_assert(BitPos < 64);
    static_assert(BitLen >= 0);
    static_assert(BitPos + BitLen <= 64);
    assert(p_csr);
    if(src == 0) return *p_csr;
    const RawDataT mask = (((1UL << BitLen) - 1UL) << BitPos);
    RawDataT ret = ((*p_csr & mask) >> BitPos);
    RawDataT save = (*p_csr & (~mask));
    save |= ((src & ((1UL << BitLen) - 1UL)) << BitPos);
    *p_csr = save;
    return ret;
}

template <int BitPos = 0, int BitLen = 64>
inline RawDataT rv64_csrrs(RawDataT *p_csr, RawDataT src) {
    static_assert(BitPos >= 0);
    static_assert(BitPos < 64);
    static_assert(BitLen >= 0);
    static_assert(BitPos + BitLen <= 64);
    assert(p_csr);
    if(src == 0) return *p_csr;
    RawDataT ret = *p_csr;
    *p_csr |= ((src & ((1UL << BitLen) - 1UL)) << BitPos);
    return ret;
}

template <int BitPos = 0, int BitLen = 64>
inline RawDataT rv64_csrrc(RawDataT *p_csr, RawDataT src) {
    static_assert(BitPos >= 0);
    static_assert(BitPos < 64);
    static_assert(BitLen >= 0);
    static_assert(BitPos + BitLen <= 64);
    assert(p_csr);
    if(src == 0) return *p_csr;
    RawDataT ret = *p_csr;
    *p_csr &= (~((src & ((1UL << BitLen) - 1UL)) << BitPos));
    return ret;
}



}

#endif

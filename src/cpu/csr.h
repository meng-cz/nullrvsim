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
    const RawDataT mask = (((1UL << BitLen) - 1UL) << BitPos);
    RawDataT ret = ((*p_csr & mask) >> BitPos);
    RawDataT validsrc = ((src & ((1UL << BitLen) - 1UL)) << BitPos);
    *p_csr = ((*p_csr & (~mask)) | validsrc);
    return ret;
}

template <int BitPos = 0, int BitLen = 64>
inline RawDataT rv64_csrrs(RawDataT *p_csr, RawDataT src) {
    static_assert(BitPos >= 0);
    static_assert(BitPos < 64);
    static_assert(BitLen >= 0);
    static_assert(BitPos + BitLen <= 64);
    assert(p_csr);
    const RawDataT mask = (((1UL << BitLen) - 1UL) << BitPos);
    RawDataT ret = ((*p_csr & mask) >> BitPos);
    RawDataT validsrc = ((src & ((1UL << BitLen) - 1UL)) << BitPos);
    *p_csr |= validsrc;
    return ret;
}

template <int BitPos = 0, int BitLen = 64>
inline RawDataT rv64_csrrc(RawDataT *p_csr, RawDataT src) {
    static_assert(BitPos >= 0);
    static_assert(BitPos < 64);
    static_assert(BitLen >= 0);
    static_assert(BitPos + BitLen <= 64);
    assert(p_csr);
    const RawDataT mask = (((1UL << BitLen) - 1UL) << BitPos);
    RawDataT ret = ((*p_csr & mask) >> BitPos);
    RawDataT validsrc = ((src & ((1UL << BitLen) - 1UL)) << BitPos);
    *p_csr &= (~validsrc);
    return ret;
}



}

#endif

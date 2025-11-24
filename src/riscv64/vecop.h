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

#pragma once

#include "common.h"
#include "riscv64.h"

namespace riscv64 {

enum class VLSWidth : uint8_t {
    Vec8B = 0b0000,
    Vec16B = 0b0101,
    Vec32B = 0b0110,
    Vec64B = 0b0111
};

enum class VLSMop : uint8_t {
    UnitStride = 0,
    IndexedUnordered = 1,
    Strided = 2,
    IndexedOrdered = 3
};

enum class VLumop : uint8_t {
    Default = 0b00000,
    WholeRegister = 0b01000,
    MaskLoad = 0b01011,
    FaultOnlyFirst = 0b10000
};

enum class VSumop : uint8_t {
    Default = 0b00000,
    WholeRegister = 0b01000,
    MaskLoad = 0b01011,
};


inline ExeOPType vec_ls_generate_optype(InstT rawinst) {
    return ((rawinst >> 12) & 0x7U) | (((rawinst >> 25) & 0x7fU) << 3);
}
inline VLSWidth vec_ls_extract_width(ExeOPType optype) {
    return static_cast<VLSWidth>((optype & 0x7U) | ((optype >> 3) & 0x8U));
}
inline bool vec_ls_extract_masked(ExeOPType optype) {
    return ((optype >> 3) & 0x1U) != 0;
}
inline VLSMop vec_ls_extract_mop(ExeOPType optype) {
    return static_cast<VLSMop>((optype >> 4) & 0x3U);
}
inline uint8_t vec_ls_extract_nf(ExeOPType optype) {
    return static_cast<uint8_t>((optype >> 7) & 0x7U);
}
inline VLumop vec_ld_extract_umop(ExeOPType optype) {
    return static_cast<VLumop>((optype >> 10) & 0x1fU);
}
inline VSumop vec_st_extract_umop(ExeOPType optype) {
    return static_cast<VSumop>((optype >> 10) & 0x1fU);
}


} // namespace riscv64

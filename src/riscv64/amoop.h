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

namespace riscv64 {

enum class AMOOPType : uint32_t {
    ADD_W   = 0x00 | 0x40,
    SWAP_W  = 0x01 | 0x40,
    LR_W    = 0x02 | 0x40,
    SC_W    = 0x03 | 0x40,
    XOR_W   = 0x04 | 0x40,
    AND_W   = 0x0c | 0x40,
    OR_W    = 0x08 | 0x40,
    MIN_W   = 0x10 | 0x40,
    MAX_W   = 0x14 | 0x40,
    MINU_W  = 0x18 | 0x40,
    MAXU_W  = 0x1c | 0x40,
    ADD_D   = 0x00 | 0x60,
    SWAP_D  = 0x01 | 0x60,
    LR_D    = 0x02 | 0x60,
    SC_D    = 0x03 | 0x60,
    XOR_D   = 0x04 | 0x60,
    AND_D   = 0x0c | 0x60,
    OR_D    = 0x08 | 0x60,
    MIN_D   = 0x10 | 0x60,
    MAX_D   = 0x14 | 0x60,
    MINU_D  = 0x18 | 0x60,
    MAXU_D  = 0x1c | 0x60,
};

uint64_t perform_amo_op(AMOOPType optype, uint64_t current, uint64_t oprand);



}

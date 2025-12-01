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

template<uint32_t AddrWidth, uint32_t WayCnt>
class ReplacerRandom {
public:
    static_assert(WayCnt >= 1);
    static_assert(AddrWidth >= 1);
    inline void touch(uint64_t addr, uint32_t idx) {}
    inline uint32_t victim(uint64_t addr) {
        return (pcg32.rand()%WayCnt);
    }

    inline void apply_next_tick() {}
protected:
    PCG32Random pcg32;
};

template<uint32_t AddrWidth, uint32_t WayCnt>
class ReplacerPLRU {
public:
    static_assert(!(WayCnt & (WayCnt-1)));
    static_assert(WayCnt >= 2);
    const uint32_t level = std::__countr_zero<uint32_t>(WayCnt);
    inline void touch(uint64_t addr, uint32_t idx) {
        to_be_touched.push_back({addr, idx & (WayCnt-1)});
    }
    inline uint32_t victim(uint64_t addr) {
        uint32_t i = 1;
        auto &bitree_left_hot = bitree_left_hot_table[addr & ((1UL << AddrWidth) - 1)];
        for(uint32_t n = 0; n < level; n++) {
            i = (i << 1) | (bitree_left_hot[i]?1:0);
        }
        return i - WayCnt;
    }
    inline void apply_next_tick() {
        for (auto &entry : to_be_touched) {
            uint32_t idx = entry.second;
            auto &bitree_left_hot = bitree_left_hot_table[entry.first & ((1UL << AddrWidth) - 1)];
            idx += WayCnt;
            for(uint32_t n = 0; n < level; n++) {
                bitree_left_hot[idx / 2] = !(idx & 1);
                idx = idx >> 1;
            }
        }
    }
protected:
    std::array<std::array<bool, WayCnt>, 1UL << AddrWidth> bitree_left_hot_table;
    std::vector<std::pair<uint64_t, uint32_t>> to_be_touched;
};

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

template<typename DataT, uint32_t AddrWidth, uint32_t RDWidth, uint32_t WRWidth>
class BlockRamRDFirst {
public:
    inline void readReq(uint32_t portidx, uint64_t addr) {
        toBeReadAddrs[portidx] = addr & ((1UL << AddrWidth) - 1UL);
    }

    inline void readResp(uint32_t portidx, DataT *out) {
        *out = readDatas[portidx];
    }

    inline void writeReq(uint32_t portidx, uint64_t addr, DataT &data) {
        toBeWritens[portidx].addr = addr & ((1UL << AddrWidth) - 1UL);
        toBeWritens[portidx].data = data;
        if constexpr (WRWidth == 1) {
            toBeWritens[portidx].valid = true;
        }
    }

    inline void apply_next_tick() {
        for (uint32_t i = 0; i < RDWidth; i++) {
            readDatas[i] = datas[toBeReadAddrs[i]];
        }
        for (uint32_t i = 0; i < WRWidth; i++) {
            if constexpr (WRWidth == 1) {
                if (!toBeWritens[i].valid) {
                    continue;
                }
                datas[toBeWritens[i].addr] = toBeWritens[i].data;
                toBeWritens[i].valid = false;
            } else {
                datas[toBeWritens[i].addr] = toBeWritens[i].data;
                toBeWritens[i].addr = (1UL << AddrWidth);  // invalidate
            }
        }
    }

protected:
    std::array<DataT, (1UL << AddrWidth) + 1> datas;
    typedef struct {
        uint64_t addr = (1UL << AddrWidth);
        DataT data;
    } ToBeWriten;
    typedef struct {
        uint64_t addr = 0;
        DataT data;
        bool valid = false;
    } ToBeWritenV;
    using WRType = std::conditional_t<WRWidth == 1, ToBeWritenV, ToBeWriten>;
    std::array<WRType, WRWidth> toBeWritens;
    std::array<uint64_t, RDWidth> toBeReadAddrs;
    std::array<DataT, RDWidth> readDatas;
};

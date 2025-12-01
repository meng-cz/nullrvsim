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
#include "utils/pipe.hpp"
#include "utils/blockram.hpp"
#include "utils/replacer.hpp"

#include "xsv3sys/bundles/bpu.h"

namespace xsv3sys {
namespace bpu {

constexpr uint32_t MbtbNumSet = (MbtbNumEntries / MbtbNumWay);
constexpr uint32_t MbtbSetIdxLen = std::__countr_zero<uint32_t>(MbtbNumSet);

typedef struct {
    VirtAddrT           target;
    BranchAttribute     attribute;
    CfiPosT             cfiPosition;
    bool                hit;
} BtbInfo;

typedef struct {
    BtbInfo         result[NumBtbResultEntries];
    MainBtbMeta     meta;
    bool            valid;
} MainBTBPrediction;

class MainBTB {
public:

typedef struct {
    void * __top;
    string __instance_name;
} ConstructorParams;

    MainBTB(ConstructorParams &params) : params(params) {}

    inline void on_current_tick() {
        _tick_s1ToS2();
    }
    inline void apply_next_tick() {
        btbBlock.apply_next_tick();
        replacer.apply_next_tick();
        s1_reg.apply_next_tick();
        s2_reg.apply_next_tick();
    }

    inline void s0SetVaddr(VirtAddrT vaddr) {
        auto &s1Reg = s1_reg.get_input_buffer();
        s1Reg.valid = true;
        s1Reg.data = vaddr;
        btbBlock.readReq(0, getSetIdx(vaddr));
        for (uint32_t i = 1; i < MbtbNumAlignBanks; i++) {
            VirtAddrT bankVaddr = vaddr + (i << FetchBlockAlignWidth);
            if ((vaddr >> PAGE_ADDR_OFFSET) == (bankVaddr >> PAGE_ADDR_OFFSET)) {
                btbBlock.readReq(i, getSetIdx(bankVaddr));
            }
        }
    }
    inline void t0Train(BPUTrain &train) {

    }

    inline void s2GetPredict(MainBTBPrediction *info) {
        auto &s2reg = s2_reg.get_output_buffer();
        auto &s2regData = s2reg.data;
        if (!s2reg.valid) {
            info->valid = false;
            return;
        }
        VirtAddrT vaddr = s2regData.vaddr;
        info->valid = true;
        for (uint32_t i = 0; i < MbtbNumAlignBanks; i++) {
            VirtAddrT bankVaddr = vaddr + (i << FetchBlockAlignWidth);
            bool isCrossPage = ((vaddr >> PAGE_ADDR_OFFSET) != (bankVaddr >> PAGE_ADDR_OFFSET));
            for (uint32_t j = 0; j < MbtbNumWay; j++) {
                auto &entry = s2regData.entries[i][j];
                if (isCrossPage || !entry.valid) {
                    info->result[i].hit = false;
                    info->meta.entries[i].rawHit = false;
                    continue;
                }
                uint32_t entryTag = getTag(bankVaddr);
                if (entry.tag == entryTag) {
                    info->result[i].hit = true;
                    info->result[i].target = entry.target;
                    info->result[i].attribute = entry.attribute;
                    info->result[i].cfiPosition = entry.position;
                    info->meta.entries[i].rawHit = true;
                    info->meta.entries[i].position = entry.position;
                    info->meta.entries[i].attribute = entry.attribute;
                }
                else {
                    info->result[i].hit = false;
                    info->meta.entries[i].rawHit = false;
                }
            }
        }
    }



private:
    ConstructorParams params;

    inline uint64_t getSetIdx(VirtAddrT vaddr) {
        return extract_bits<VirtAddrT, FetchBlockAlignWidth, MbtbSetIdxLen>(vaddr);
    }
    inline uint32_t getTag(VirtAddrT vaddr) {
        return extract_bits<VirtAddrT, (FetchBlockAlignWidth + MbtbSetIdxLen), MbtbTagWidth>(vaddr);
    }

    typedef struct {
        VirtAddrT       target;
        uint32_t        tag;
        BranchAttribute attribute;
        CfiPosT         position;
        bool            valid;
    } MbtbEntry;

    BlockRamRDFirst<array<MbtbEntry, MbtbNumWay>, MbtbSetIdxLen, 2, 1> btbBlock;
    ReplacerRandom<MbtbSetIdxLen, MbtbNumWay> replacer;
    
    SimpleStageValidPipe<VirtAddrT> s1_reg;

    typedef struct {
        array<MbtbEntry, MbtbNumWay>    entries[MbtbNumAlignBanks];
        VirtAddrT       vaddr;
    } S2Reg;

    SimpleStageValidPipe<S2Reg> s2_reg;

    inline void _tick_s1ToS2() {
        auto &s1Reg = s1_reg.get_output_buffer();
        if (!s1Reg.valid) {
            return;
        }
        VirtAddrT vaddr = s1Reg.data;
        auto &s2Reg = s2_reg.get_input_buffer();
        auto &s2RegData = s2Reg.data;
        s2Reg.valid = true;
        s2RegData.vaddr = vaddr;
        btbBlock.readResp(0, &s2RegData.entries[0]);
        for (uint32_t i = 1; i < MbtbNumAlignBanks; i++) {
            VirtAddrT bankVaddr = vaddr + (i << FetchBlockAlignWidth);
            if ((vaddr >> PAGE_ADDR_OFFSET) == (bankVaddr >> PAGE_ADDR_OFFSET)) {
                btbBlock.readResp(i, &s2RegData.entries[i]);
            }
        }
    }

    typedef struct {
        MainBtbMeta             meta;
        ValidData<BranchInfo>   branchs[ResolveEntryBranchNumber];
        VirtAddrT               startVaddr;
    } T1Reg;

};


} // namespace bpu
} // namespace xsv3sys

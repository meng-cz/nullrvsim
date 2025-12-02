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
#include "xsv3sys/configs/configs.h"

namespace xsv3sys {
namespace bpu {

constexpr uint32_t MbtbNumSet = (MbtbNumEntries / MbtbNumWay);
constexpr uint32_t MbtbSetIdxLen = std::__countr_zero<uint32_t>(MbtbNumSet);

typedef struct {
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
        _tick_t1TrainToBtb();
    }
    inline void apply_next_tick() {
        btbBlock.apply_next_tick();
        replacer.apply_next_tick();
        s1_reg.apply_next_tick();
        s2_reg.apply_next_tick();
        t1_reg.apply_next_tick();
    }

    inline void s0SetVaddr(VirtAddrT vaddr) {
        auto &s1Reg = s1_reg.get_input_buffer();
        s1Reg.valid = true;
        s1Reg.data = vaddr;
        btbBlock.readReq(0, getSetIdx(vaddr));
    }
    inline void t0Train(BPUTrain &train) {
        auto &t1Reg = t1_reg.get_input_buffer();
        t1Reg.valid = true;
        t1Reg.data.startVaddr = train.startVaddr;
        for (uint32_t i = 0; i < ResolveEntryBranchNumber; i++) {
            t1Reg.data.branchs[i] = train.branchs[i];
        }
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
        for (uint32_t i = 0; i < ResolveEntryBranchNumber; i++) {
            info->meta.entries[i] = s2regData.entry.entries[i];
        }
    }



private:
    ConstructorParams params;

    inline uint64_t getSetIdx(VirtAddrT vaddr) {
        return extract_bits<VirtAddrT, 1, MbtbSetIdxLen>(vaddr);
    }
    inline uint32_t getTag(VirtAddrT vaddr) {
        return extract_bits<VirtAddrT, (1 + MbtbSetIdxLen), MbtbTagWidth>(vaddr);
    }

    typedef struct {
        MainBtbMetaEntry    entries[ResolveEntryBranchNumber];
        uint32_t            tag;
        bool                valid;
    } MbtbEntry;

    BlockRamRDFirst<array<MbtbEntry, MbtbNumWay>, MbtbSetIdxLen, 2, 1> btbBlock;
    ReplacerRandom<MbtbSetIdxLen, MbtbNumWay> replacer;
    
    SimpleStageValidPipe<VirtAddrT> s1_reg;

    typedef struct {
        MbtbEntry       entry;
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

        array<MbtbEntry, MbtbNumWay> line;
        btbBlock.readResp(0, &line);
        for (uint32_t i = 0; i < MbtbNumWay; i++) {
            if (line[i].valid && (line[i].tag == getTag(vaddr))) {
                s2RegData.entry = line[i];
                return;
            }
        }
    }

    typedef struct {
        ValidData<BranchInfo>   branchs[ResolveEntryBranchNumber];
        VirtAddrT               startVaddr;
    } T1Reg;

    SimpleStageValidPipe<T1Reg> t1_reg;

    inline void _tick_t1TrainToBtb() {
        auto &t1Reg = t1_reg.get_output_buffer();
        if (!t1Reg.valid) {
            return;
        }
        VirtAddrT vaddr = t1Reg.data.startVaddr;
        uint64_t setIdx = getSetIdx(vaddr);
        array<MbtbEntry, MbtbNumWay> line;
        btbBlock.readResp(1, &line);

        // find victim
        uint32_t victimWay = MbtbNumWay;
        for (uint32_t i = 0; i < MbtbNumWay; i++) {
            if (!line[i].valid) {
                victimWay = i;
                break;
            }
        }
        if (victimWay == MbtbNumWay) {
            victimWay = replacer.victim(setIdx);
        }

        // prepare new entry
        MbtbEntry &newEntry = line[victimWay];
        newEntry.tag = getTag(vaddr);
        newEntry.valid = false;
        for (uint32_t i = 0; i < ResolveEntryBranchNumber; i++) {
            if (t1Reg.data.branchs[i].valid) {
                newEntry.entries[i].target = t1Reg.data.branchs[i].data.target;
                newEntry.entries[i].attribute = t1Reg.data.branchs[i].data.attribute;
                newEntry.entries[i].position = t1Reg.data.branchs[i].data.cfiPosition;
                newEntry.valid = true;
            } else {
                newEntry.entries[i].target = 0;
                newEntry.entries[i].attribute = BranchAttribute::None;
                newEntry.entries[i].position = 0;
            }
        }

        // write back
        btbBlock.writeReq(0, setIdx, line);
        replacer.touch(setIdx, victimWay);
    }

};


} // namespace bpu
} // namespace xsv3sys

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
#include "utils/replacer.hpp"
#include "utils/storage.hpp"

#include "xsv3sys/bundles/bpu.h"

#ifndef MicroBTBNumEntries
#define MicroBTBNumEntries (32)
#endif

#ifndef MicroBTBTagWidth
#define MicroBTBTagWidth (22)
#endif

namespace xsv3sys {
namespace bpu {

class MicroBTB {
public:

typedef struct {
    void * __top;
    string __instance_name;
} ConstructorParams;

    MicroBTB(ConstructorParams &params) : params(params) {}

    inline void on_current_tick() {
        _tick_t1Train();
    }
    inline void apply_next_tick() {
        replacer.apply_next_tick();
        entries.apply_next_tick();
        s1_startVaddr.apply_next_tick();
        t1_reg.apply_next_tick();
    }

    inline void s0SetVaddr(VirtAddrT vaddr) {
        s1_startVaddr.push(vaddr);
    }
    inline void t0Train(BPUTrain &train) {
        auto reg = t1_reg.get_input_buffer();
        auto data = reg.data;
        BranchInfo *binfo = nullptr;
        for (auto &b : train.branchs) {
            if (b.valid && b.data.mispredict) {
                binfo = &b.data;
                break;
            }
        }
        if (binfo) {
            reg.valid = true;
            data.actualTaken = binfo->taken;
            data.position = binfo->cfiPosition;
            data.target = binfo->target;
            data.attribute = binfo->attribute;
            VirtAddrT startVaddr = train.startVaddr;
            uint32_t tag = ((startVaddr >> 1) & ((1UL << MicroBTBTagWidth) - 1UL));
            data.tag = tag;
            uint32_t idx = 0;
            if (entries.match(tag, &(data.hitEntry), &idx)) {
                data.hit = true;
                data.hitIdx = idx;
                data.hitNotUseful = data.hitEntry.usefulCnt.is_negative();
                data.hitPositionSame = (data.hitEntry.slot1.position == binfo->cfiPosition);
                data.hitAttributeSame = (data.hitEntry.slot1.attribute == binfo->attribute);
                data.hitTargetSame = (data.hitEntry.slot1.target == binfo->target);
            } else {
                data.hit = false;
                data.hitNotUseful = false;
                data.hitPositionSame = false;
                data.hitAttributeSame = false;
                data.hitTargetSame = false;
                data.hitIdx = 0;
            }
        }
    }

    inline void s1GetPredict(BranchInfo *info) {
        VirtAddrT vaddr = 0;
        if(!s1_startVaddr.top(&vaddr)) {
            info->taken = false;
            return;
        }
        uint32_t tag = ((vaddr >> 1) & ((1UL << MicroBTBTagWidth) - 1UL));
        Entry entry; uint32_t idx = 0;
        if (entries.match(tag, &entry, &idx)) {
            info->taken = true;
            info->cfiPosition = entry.slot1.position;
            info->target = entry.slot1.target;
            info->attribute = entry.slot1.attribute;
            replacer.touch(0, idx);
        } else {
            info->taken = false;
            return;
        }
    }


private:
    ConstructorParams params;

    typedef struct {
        bool            isStaticTarget; // whether branch in slot 1 has a static target
        CfiPosT         position;       // branch position: at fetchBlockVAddr + position
        BranchAttribute attribute;      // branch attribute
        VirtAddrT       target;
    } EntrySlot1;

    typedef struct {
        bool            valid;          // whether branch in slot 2 is valid
        bool            taken;          // whether branch in slot 2 is predicted as taken
        CfiPosT         position;
        BranchAttribute attribute;
        VirtAddrT       target;
    } EntrySlot2;

    typedef struct {
        SaturateCounter8<4>     usefulCnt;
        EntrySlot1              slot1;
        EntrySlot2              slot2;
    } Entry;

    TaggedArrayStorageNext<uint32_t, Entry, MicroBTBNumEntries> entries;

    ReplacerPLRU<0, MicroBTBNumEntries> replacer;

    SimpleStageValidPipe<VirtAddrT> s1_startVaddr;

    typedef struct {
        uint32_t            tag;
        bool                actualTaken;
        CfiPosT             position;
        uint32_t            target;
        bool                hit;
        bool                hitNotUseful;
        bool                hitPositionSame;
        bool                hitAttributeSame;
        bool                hitTargetSame;
        uint8_t             hitIdx;
        BranchAttribute     attribute;
        Entry               hitEntry;
    } T1TrainReg;

    SimpleStageValidPipe<T1TrainReg> t1_reg;

    inline void _tick_t1Train() {
        auto &reg = t1_reg.get_output_buffer();
        auto &data = reg.data;
        if (!reg.valid) {
            return;
        }
        Entry &entry = data.hitEntry;
        if (!data.hit || (data.hitNotUseful && (!data.hitAttributeSame || !data.hitPositionSame || !data.hitTargetSame))) {
            entry.usefulCnt = entry.usefulCnt.get_max();
            entry.slot1.position = data.position;
            entry.slot1.attribute = data.attribute;
            entry.slot1.target = data.target;
            entry.slot1.isStaticTarget = !(data.hit && !data.hitTargetSame);
            entry.slot2.valid = false;
        } else {
            entry.usefulCnt = entry.usefulCnt.get_increment();
        }
        uint32_t updateIdx = (data.hit) ? data.hitIdx : replacer.victim(0);
        entries.setnext(updateIdx, data.tag, entry);
        replacer.touch(0, updateIdx);
    }

};




} // namespace bpu
} // namespace xsv3sys

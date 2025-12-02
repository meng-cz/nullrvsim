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

typedef struct {
    VirtAddrT   startVaddr;
    HistoryT    phr;
} TageReq;

typedef struct {
    TageMeta    meta;
    bool        taken[ResolveEntryBranchNumber];
    bool        valid;
} TageResp;

constexpr uint32_t TageSetIdxWidth = std::__countr_zero<uint32_t>(TageNumSets);



template <size_t... Is>
FORCE_INLINE void __tage_internal_loop_fold_tag_hist(const HistoryT& hist, uint32_t * histTag, uint32_t * histTag2, std::index_sequence<Is...>) {
    constexpr uint32_t HistLen = TageHistoryLengths[Is];
    HistoryT localHist = hist.extract<HistLen-1,0>();
    histTag[Is] = foldHistory<TageTagWidth, HistLen>(localHist);
    histTag2[Is] = foldHistory<TageTagWidth-1, HistLen>(localHist);
}

FORCE_INLINE void tage_fold_tag_hist(const HistoryT& hist, uint32_t * histTag, uint32_t * histTag2) {
    __tage_internal_loop_fold_tag_hist(hist, histTag, histTag2, std::make_index_sequence<TageNumTables>{});
}

template <size_t... Is>
FORCE_INLINE void __tage_internal_loop_fold_idx_hist(const HistoryT& hist, uint32_t * histIdx, std::index_sequence<Is...>) {
    constexpr uint32_t HistLen = TageHistoryLengths[Is];
    histIdx[Is] = foldHistory<TageSetIdxWidth, HistLen>(hist.extract<HistLen-1,0>());
}

FORCE_INLINE void tage_fold_idx_hist(const HistoryT& hist, uint32_t * histIdx) {
    __tage_internal_loop_fold_idx_hist(hist, histIdx, std::make_index_sequence<TageNumTables>{});
}


class TAGE {

public:

    typedef struct {
        void * __top;
        string __instance_name;
    } ConstructorParams;


    inline void on_current_tick() {
        _tick_s1ToS2();
    }
    inline void apply_next_tick() {
        s1_reg.apply_next_tick();
        s2_reg.apply_next_tick();
        for (auto & tab : tables) {
            tab.block.apply_next_tick();
        }
    }

    inline void s0Req(TageReq &req) {
        auto &s1 = s1_reg.get_input_buffer();
        auto &s1Data = s1.data;
        s1.valid = true;
        s1Data.vaddr = req.startVaddr;

        tage_fold_tag_hist(req.phr, s1Data.foldedHistForTag.data(), s1Data.anotherHistForTag.data());

        uint32_t setIdx = getSetIdx(req.startVaddr);
        for (uint32_t i = 0; i < TageNumTables; i++) {
            tables[i].block.readReq(0, setIdx);
        }
    }

    inline void s2Resp(MainBtbMeta & mbtbResult, TageResp * tageResp) {

    }



private:
    ConstructorParams params;

    inline uint64_t getSetIdx(uint64_t vaddr) {
        return extract_bits<uint64_t, FetchBlockAlignSize, TageSetIdxWidth>(vaddr);
    }
    inline uint64_t getTag(uint64_t vaddr) {
        return extract_bits<uint64_t, TageSetIdxWidth + FetchBlockAlignSize, TageTagWidth>(vaddr);
    }

    typedef struct {
        uint32_t                                    tag;
        SaturateCounter8<1U<<TageTakenCtrWidth>     takenCtr;
        SaturateCounter8<1U<<TageUsefulCtrWidth>    usefulCtr;
        bool                                        valid;
    } TageEntry;

    typedef struct {
        BlockRamRDFirst<array<TageEntry, TageNumWays>, TageSetIdxWidth, 2, 1> block;
    } TageTable;

    array<TageTable, TageNumTables> tables;

    typedef struct {
        array<uint32_t, TageNumTables>  foldedHistForTag;
        array<uint32_t, TageNumTables>  anotherHistForTag;
        VirtAddrT                       vaddr;
    } S1Reg;

    SimpleStageValidPipe<S1Reg> s1_reg;

    typedef struct {
        array<array<TageEntry, TageNumWays>, TageNumTables> entries;
        array<uint32_t, TageNumTables>  foldedHistForTag;
        array<uint32_t, TageNumTables>  anotherHistForTag;
        VirtAddrT                       vaddr;
    } S2Reg;

    SimpleStageValidPipe<S2Reg> s2_reg;

    inline void _tick_s1ToS2() {
        auto &s1Reg = s1_reg.get_output_buffer();
        if (!s1Reg.valid) {
            return;
        }
        VirtAddrT vaddr = s1Reg.data.vaddr;
        auto &s2Reg = s2_reg.get_input_buffer();
        auto &s2RegData = s2Reg.data;
        s2Reg.valid = true;
        s2RegData.vaddr = vaddr;
        s2RegData.foldedHistForTag = s1Reg.data.foldedHistForTag;
        s2RegData.anotherHistForTag = s1Reg.data.anotherHistForTag;

        for (uint32_t i = 0; i < TageNumTables; i++) {
            tables[i].block.readResp(0, &s2RegData.entries[i]);
        }
    }

};

} // namespace bpu
} // namespace xsv3sys

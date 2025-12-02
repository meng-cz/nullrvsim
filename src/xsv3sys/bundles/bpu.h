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
#include "utils/saturate.hpp"
#include "utils/bitvec.hpp"
#include "utils/uint.hpp"

#include "xsv3sys/configs/configs.h"

namespace xsv3sys {

typedef uint8_t CfiPosT;
typedef uint8_t PhrPtrT;

/**
 * Fetch Pack结尾的跳转类型
 */
enum class BranchType : uint8_t {
    None = 0,
    Conditional = 1,
    Direct = 2,
    Indirect = 3
};

/**
 * RAS操作类型
 */
enum class RasAction : uint8_t {
    None = 0,
    Push = 1,
    Pop = 2,
    PopAndPush = 3
};

/**
 * Fetch Pack结尾的跳转属性
 */
enum class BranchAttribute : uint8_t {
    None = 0,
    Conditional = 1,
    DirectCall = 2,
    IndirectCall = 3,
    Return = 4,
    ReturnAndCall = 5,
    OtherDirect = 6,
    OtherIndirect = 7
};

typedef UInt<BranchHistoryWidth> HistoryT;

// ---------------------------- Main BTB Meta ---------------------------- //

typedef struct {
    VirtAddrT       target;
    BranchAttribute attribute;
    CfiPosT         position;
} MainBtbMetaEntry;

typedef struct {
    MainBtbMetaEntry entries[ResolveEntryBranchNumber];
} MainBtbMeta;

// ---------------------------- TAGE Meta ---------------------------- //

typedef struct {
    SaturateCounter8<1U << TageBaseTableTakenCtrWidth> takenCtrs[FetchBlockInstNum];
} TageMeta;

// ---------------------------- RAS Meta ---------------------------- //

typedef struct {
    uint16_t    ssp;
    uint16_t    tosw;
} RasMeta;

typedef struct {
    uint16_t ssp;
    uint16_t sctr;
    uint16_t tosw;
    uint16_t tosr;
    uint16_t nos;
} RasInternalMeta;

// ---------------------------- SC Meta ---------------------------- //

typedef struct {
    SaturateCounter8<1U << ScCtrWidth> ctr;
} ScEntry;

static_assert(ScNumWays <= 16, "ScNumWays <= 16");

typedef struct {
    ScEntry         scPathResp[PathTableSize][ScNumWays];
    ScEntry         scGlobalResp[GlobalTableSize][ScNumWays];
    HistoryT        scGhr;
    BitVec16        scPred;
    BitVec16        useScPred;
} ScMeta;

// ---------------------------- ITTAGE Meta ---------------------------- //

static_assert(IttageNumTables <= 128, "IttageNumTables <= 128");
static_assert(IttageConfidenceCntWidth <= 8, "IttageConfidenceCntWidth <= 8");
static_assert(IttageUsefulCntWidth <= 8, "IttageUsefulCntWidth <= 8");

typedef struct {
    VirtAddrT       providerTarget;
    VirtAddrT       altProviderTarget;
    uint8_t         allocate;
    uint8_t         provider;
    uint8_t         altProvider;
    bool            altDiffers;
    bool            valid;
    SaturateCounter8<1U << IttageUsefulCntWidth>        providerUsefulCnt;
    SaturateCounter8<1U << IttageConfidenceCntWidth>    providerCnt;
    SaturateCounter8<1U << IttageConfidenceCntWidth>    altProviderCnt;
} IttageMeta;

// ---------------------------- GHR Meta ---------------------------- //

typedef struct {
    HistoryT        ghr;
    BitVec16        hitMask;
    CfiPosT         position[ResolveEntryBranchNumber];
} GhrMeta;

// ---------------------------- BPU ---------------------------- //



/**
 * BPU -> Ftq
 * FetchPack PC信息和BPU预测结果
 */
typedef struct {
    VirtAddrT           startVAddr;     // 起始虚拟地址
    VirtAddrT           target;         // 目标地址
    ValidData<CfiPosT>  takenCfiOffset; // 如果分支被预测为taken，则该字段有效，表示被预测为taken时的Cfi指令在Fetch Pack中的偏移
} BPUPrediction;

typedef struct {
    VirtAddrT       target;
    bool            taken;
    bool            mispredict;
    CfiPosT         cfiPosition;
    BranchAttribute attribute;
} BranchInfo;

/**
 * metadata for training (e.g. aheadBtb, mainBtb-specific)
 */
typedef struct {
    MainBtbMeta     mbtb;
    TageMeta        tage;
    RasMeta         ras;
    PhrPtrT         phr;
    ScMeta          sc;
    IttageMeta      ittage;
} BPUMeta;

/**
 * metadata for redirect (e.g. speculative state recovery) & training (e.g. rasPtr, phr)
 */
typedef struct {
    PhrPtrT             phrHistPtr;
    GhrMeta             ghrMeta;
    RasInternalMeta     rasMeta;
    VirtAddrT           topRetAddr;
} BpuSpeculationMeta;

typedef struct {
    BPUPrediction       prediction;
    BPUMeta             meta;
    BpuSpeculationMeta  speculationMeta;
} BPUToFTQResult;


/**
 * Backend & Ftq -> Bpu
 */
typedef struct {
    VirtAddrT           startVaddr;
    VirtAddrT           target;
    bool                taken;
    bool                isRvc;
    BranchAttribute     attribute;
    BpuSpeculationMeta  speculationMeta;
} BPURedirect;

typedef struct {
    VirtAddrT               startVaddr;
    BPUMeta                 meta;
    ValidData<BranchInfo>   branchs[ResolveEntryBranchNumber];
} BPUTrain;

typedef struct {
    VirtAddrT           pushAddr;
    BranchAttribute     attribute;
    RasMeta             rasMeta;
} BPUCommit;


template <uint32_t foldedLen, uint32_t histLen, uint32_t I, uint32_t N>
FORCE_INLINE uint32_t __constexpr_loop_foldHistory(const UInt<histLen> &hist) {
    if constexpr (I < N - 1) {
        uint32_t v = hist.extract<(I + 1) * foldedLen - 1, I * foldedLen>().value;
        return v ^ __constexpr_loop_foldHistory<foldedLen, histLen, I + 1, N>(hist);
    } else if constexpr (I == N - 1) {
        static_assert(histLen > I * foldedLen);
        return hist.extract<histLen - 1, I * foldedLen>().value;
    } else {
        return 0;
    }
}

template <uint32_t foldedLen, uint32_t histLen>
FORCE_INLINE uint32_t foldHistory(const UInt<histLen> &hist) {
    static_assert(foldedLen <= 32);
    static_assert(foldedLen > 0);
    static_assert(histLen <= 512);
    static_assert(histLen > 0);
    if constexpr (foldedLen >= histLen) {
        return hist.value;
    } else {
        constexpr uint32_t ROUND = CEIL_DIV(histLen, foldedLen);
        return __constexpr_loop_foldHistory<foldedLen, histLen, 0, ROUND>(hist);
    }
}

} // namespace xsv3sys


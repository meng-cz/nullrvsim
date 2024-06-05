#ifndef RVSIM_CPU_XS_TYPES_H
#define RVSIM_CPU_XS_TYPES_H

#include "common.h"
#include "simerror.h"
#include "tickqueue.h"

#include "cpu/isa.h"

namespace simcpu {

namespace xs {

using isa::RV64OPCode;
using isa::RV64InstParam;
using isa::RV64InstDecoded;
using isa::RVInstFlagT;
using isa::RVRegType;

#define XSIFU_BRANCH_CNT    (2)
#define XSIFU_HIST_LEN      (128)

class BrHist {
public:
    BrHist() : bitlen(XSIFU_HIST_LEN), ptr(0) {
        assert(bitlen);
        bitlen = ALIGN(bitlen,64);
        data.assign(bitlen/64+1, 0);
    }
    inline void clear() {
        data.assign(bitlen/64+1, 0);
        ptr = 0;
    }
    inline void push(bool taken) { 
        assert(ptr < bitlen);
        if(ptr == 0) {
            ptr = bitlen - 1;
        }
        else {
            ptr--;
        }
        if(taken) {
            data[ptr/64] |= (1UL<<(ptr%64));
        }
        else {
            data[ptr/64] &= (~(1UL<<(ptr%64)));
        }
    };
    inline uint64_t get(uint32_t histlen, uint32_t taglen) {
        assert(histlen <= bitlen && taglen <= 64 && ptr < bitlen);
        data[bitlen/64] = data[0];
        uint64_t res = 0;
        for(uint32_t i = 0; i < histlen; i+= taglen) {
            if(taglen > (histlen - i)) taglen = histlen - i;
            uint32_t pos = ((ptr + i) % bitlen), len = taglen, off = (pos % 64);
            if(off + len <= 64) {
                res ^= (((1UL << len) - 1) & (data[pos/64] >> off));
            }
            else {
                uint32_t low_len = 64 - off, high_len = len - low_len;
                res ^= (((data[pos/64+1] & ((1UL << high_len) - 1UL)) << low_len) | (data[pos/64] >> off));
            }
        }
        return res;
    }
protected:
    std::vector<uint64_t> data;
    uint32_t bitlen = XSIFU_HIST_LEN;
    uint32_t ptr = 0;
};

typedef struct {
    uint32_t    bos;
    uint32_t    tosr;
    uint32_t    tosw;
    uint32_t    ssp;
} RASSnapshot;

enum class FetchPackJmp {
    normal = 0,
    jal,
    jalr,
    call,
    ret
};

typedef struct {
    uint16_t    inst_offset = 0;
    int16_t     pred_taken = 0;
    int32_t     jmp_offset = 0;
} FTQBranch;

typedef struct {
    vector<RVInstT>     insts;      // 指令列表
    VirtAddrT           startpc;    // Fetch块第一条指令的pc
    VirtAddrT           endpc;      // Fetch块最后一条指令的下一条指令的pc
    vector<FTQBranch>   branchs;    // Fetch块内的分支指令信息
    FetchPackJmp        jmpinfo;    // Fetch块最后一条指令是不是jmp
    VirtAddrT           jmptarget;  // Fetch块最后一条指令的跳转地址
    BrHist              bhr;        // 进行该Fetch块的分支预测前的全局分支历史
    RASSnapshot         ras;        // 进行函数跳转预测前的RAS信息
    RASSnapshot         ras2;       // 进行函数跳转预测后的RAS信息
    uint16_t            commit_cnt;
    bool                commit_ras; // 是否进行了RAS预测，如果进行了就需要在提交时更新RAS
} FTQEntry;

inline VirtAddrT nextpc(FTQEntry *fetch) {
    VirtAddrT ret = ((fetch->jmpinfo == FetchPackJmp::normal)?(fetch->endpc):(fetch->jmptarget));
    for(auto &br : fetch->branchs) {
        if(br.pred_taken >= 0) {
            ret = fetch->startpc + (int64_t)(br.inst_offset) + (int64_t)(br.jmp_offset);
            break;
        }
    }
    return ret;
}

typedef struct {

} BPUPackage;

typedef RVRegIndexT PhysReg;
typedef RVRegIndexT VirtReg;

typedef uint64_t XSInstID;
inline bool inst_later_than(XSInstID a, XSInstID b) {
    return (((a>>62) == 0UL && (b>>62) == 3UL) || a > b);
}

typedef struct {
    XSInstID        id;
    FTQEntry        *ftq;
    VirtAddrT       pc;
    RVInstT         inst;
    RV64OPCode      opcode;
    RV64InstParam   param;
    RVInstFlagT     flag;
    RawDataT        imm;
    VirtReg         vrd;
    VirtReg         vrs[3];
    PhysReg         prd;
    PhysReg         prs[3];
    PhysReg         prstale;
    RawDataT        arg0; // rdvalue, stdata
    RawDataT        arg1; // jmppc, l/saddr
    RawDataT        arg2; // s3, l/spaddr
    uint32_t        exlatency;
    bool            rsready[3];
    SimError        err;
    bool            finished;
    string          dbgname;
} XSInst;

inline bool inst_ready(XSInst *inst) {
    bool nr1 = ((inst->flag & (RVINSTFLAG_S1FP | RVINSTFLAG_S1INT)) && (!inst->rsready[0]));
    bool nr2 = ((inst->flag & (RVINSTFLAG_S2FP | RVINSTFLAG_S2INT)) && (!inst->rsready[1]));
    bool nr3 = ((inst->flag & (RVINSTFLAG_S3FP | RVINSTFLAG_S3INT)) && (!inst->rsready[2]));
    return (!(nr1 || nr2 || nr3));
}

enum class DispType {
    alu = 0,
    mem,
    fp,
};

enum class RSType {
    alu = 0,
    mdu,
    misc,
    ld,
    sta,
    std,
    fmac,
    fmisc
};

inline DispType disp_type(XSInst *inst) {
    switch(inst->opcode) {
        case RV64OPCode::load :
        case RV64OPCode::loadfp :
        case RV64OPCode::store :
        case RV64OPCode::storefp :
        case RV64OPCode::amo :
        case RV64OPCode::miscmem : // Fence 指令也需要交给lsu处理
            return DispType::mem;
        case RV64OPCode::madd :
        case RV64OPCode::msub :
        case RV64OPCode::nmsub :
        case RV64OPCode::nmadd :
            return DispType::fp;
        case RV64OPCode::opfp :
            return (isa::rv64_fpop_is_i_s1(inst->param.fp.op)?(DispType::alu):(DispType::fp));
    }
    return DispType::alu;
}

typedef struct {
    uint32_t ftb_ways = 4;
    uint32_t ubtb_ways = 32;
    uint32_t ftb_set_bits = 9; // 512

    uint32_t fetch_width_bytes = 32;
    uint32_t ftq_size = 64;

    uint32_t inst_buffer_size = 48;

    uint32_t decode_width = 6;
    uint32_t rob_size = 64;
    uint32_t branch_inst_cnt = 16;

    uint32_t phys_ireg_cnt = 192;
    uint32_t phys_freg_cnt = 96;

    uint32_t int_rs_size = 16;
    uint32_t fp_rs_size = 16;
    uint32_t mem_rs_size = 16;

    uint32_t load_queue_size = 80;
    uint32_t store_queue_size = 64;
    uint32_t commited_store_buffer_size = 16;
} XiangShanParam;

}}

#endif

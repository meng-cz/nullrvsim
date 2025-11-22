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

#ifndef RVSIM_ISA_H
#define RVSIM_ISA_H

#include "common.h"

#include "amo.h"

typedef uint32_t RVInstT;
typedef uint16_t RVCInstT;

typedef uint16_t RVRegIndexT;

namespace isa {

#define RV_REG_CNT_INT  (32)
#define RV_REG_CNT_FP  (32)

#define RV_REG_x0   (0)
#define RV_REG_ra   (1)
#define RV_REG_sp   (2)
#define RV_REG_gp   (3)
#define RV_REG_tp   (4)
#define RV_REG_t0   (5)
#define RV_REG_t1   (6)
#define RV_REG_t2   (7)
#define RV_REG_s0   (8)
#define RV_REG_s1   (9)
#define RV_REG_a0   (10)
#define RV_REG_a1   (11)
#define RV_REG_a2   (12)
#define RV_REG_a3   (13)
#define RV_REG_a4   (14)
#define RV_REG_a5   (15)
#define RV_REG_a6   (16)
#define RV_REG_a7   (17)
#define RV_REG_s2   (18)
#define RV_REG_s3   (19)
#define RV_REG_s4   (20)
#define RV_REG_s5   (21)
#define RV_REG_s6   (22)
#define RV_REG_s7   (23)
#define RV_REG_s8   (24)
#define RV_REG_s9   (25)
#define RV_REG_s10  (26)
#define RV_REG_s11  (27)
#define RV_REG_t3   (28)
#define RV_REG_t4   (29)
#define RV_REG_t5   (30)
#define RV_REG_t6   (31)

#define RV_REG_ft0  (0)
#define RV_REG_ft1  (1)
#define RV_REG_ft2  (2)
#define RV_REG_ft3  (3)
#define RV_REG_ft4  (4)
#define RV_REG_ft5  (5)
#define RV_REG_ft6  (6)
#define RV_REG_ft7  (7)
#define RV_REG_fs0  (8)
#define RV_REG_fs1  (9)
#define RV_REG_fa0  (10)
#define RV_REG_fa1  (11)
#define RV_REG_fa2  (12)
#define RV_REG_fa3  (13)
#define RV_REG_fa4  (14)
#define RV_REG_fa5  (15)
#define RV_REG_fa6  (16)
#define RV_REG_fa7  (17)
#define RV_REG_fs2  (18)
#define RV_REG_fs3  (19)
#define RV_REG_fs4  (20)
#define RV_REG_fs5  (21)
#define RV_REG_fs6  (22)
#define RV_REG_fs7  (23)
#define RV_REG_fs8  (24)
#define RV_REG_fs9  (25)
#define RV_REG_fs10 (26)
#define RV_REG_fs11 (27)
#define RV_REG_ft8  (28)
#define RV_REG_ft9  (29)
#define RV_REG_ft10 (30)
#define RV_REG_ft11 (31)

char** ireg_names();

inline char* ireg_name(RVRegIndexT index) {
    return ireg_names()[index];
};

RVRegIndexT ireg_index_of(const char* name);

char** freg_names();

inline char* freg_name(RVRegIndexT index) {
    return freg_names()[index];
};

RVRegIndexT freg_index_of(const char* name);

typedef std::array<uint64_t, RV_REG_CNT_INT + RV_REG_CNT_FP> RVRegArray;
inline void zero_regs(RVRegArray &regs) {
    memset(regs.data(), 0, regs.size() * sizeof(uint64_t));
}
inline uint64_t& ireg_value_of(RVRegArray &regs, const char *name) {
    return regs[ireg_index_of(name)];
}
inline uint64_t& freg_value_of(RVRegArray &regs, const char *name) {
    return regs[RV_REG_CNT_INT + freg_index_of(name)];
}

enum class RVRegType{
    i = 0,
    f = 1,
};

#define OP_LOAD         (0x03) // 000 0011
#define OP_LOADFP       (0x07) // 000 0111
#define OP_MISCMEM      (0x0F) // 000 1111
#define OP_OPIMM        (0x13) // 001 0011
#define OP_AUIPC        (0x17) // 001 0111
#define OP_OPIMM32      (0x1B) // 001 1011

#define OP_STORE        (0x23) // 010 0011
#define OP_STOREFP      (0x27) // 010 0111
#define OP_AMO          (0x2F) // 010 1111
#define OP_OP           (0x33) // 011 0011
#define OP_LUI          (0x37) // 011 0111
#define OP_OP32         (0x3B) // 011 1011

#define OP_MADD         (0x43) // 100 0011
#define OP_MSUB         (0x47) // 100 0111
#define OP_NMSUB        (0x4B) // 100 1011
#define OP_NMADD        (0x4F) // 100 1111
#define OP_OPFP         (0x53) // 101 0011
#define OP_OPV          (0x57) // 101 0111

#define OP_BRANCH       (0x63) // 110 0011
#define OP_JALR         (0x67) // 110 0111
#define OP_JAL          (0x6F) // 110 1111
#define OP_SYSTEM       (0x73) // 111 0011

enum class RV64OPCode {
    load    = 0x03,
    loadfp  = 0x07,
    miscmem = 0x0f,
    opimm   = 0x13,
    auipc   = 0x17,
    opimm32 = 0x1b,
    store   = 0x23,
    storefp = 0x27,
    amo     = 0x2f,
    op      = 0x33,
    lui     = 0x37,
    op32    = 0x3b,
    madd    = 0x43,
    msub    = 0x47,
    nmsub   = 0x4b,
    nmadd   = 0x4f,
    opfp    = 0x53,
    opv     = 0x57,
    branch  = 0x63,
    jalr    = 0x67,
    jal     = 0x6f,
    system  = 0x73,
    nop     = 0
};

inline uint32_t rv64_ls_width_to_length(RV64LSWidth wid) {
    switch (wid)
    {
    case RV64LSWidth::byte: case RV64LSWidth::ubyte: return 1;
    case RV64LSWidth::harf: case RV64LSWidth::uharf: return 2;
    case RV64LSWidth::word: case RV64LSWidth::uword: return 4;
    case RV64LSWidth::dword: return 8;
    case RV64LSWidth::qword: return 16;
    }
    return 0;
}

enum class RV64BranchOP3 {
    BEQ     = 0,
    BNE     = 1,
    BLT     = 4,
    BGE     = 5,
    BLTU    = 6,
    BGEU    = 7
};

enum class RV64IntOP73 {
    ADD     = 0x000,
    SUB     = 0x100,
    SLL     = 0x001,
    SLT     = 0x002,
    SLTU    = 0x003,
    XOR     = 0x004,
    SRL     = 0x005,
    SRA     = 0x105,
    OR      = 0x006,
    AND     = 0x007,
    MUL     = 0x008,
    MULH    = 0x009,
    MULHSU  = 0x00a,
    MULHU   = 0x00b,
    DIV     = 0x00c,
    DIVU    = 0x00d,
    REM     = 0x00e,
    REMU    = 0x00f,
};

inline bool rv64_intop_is_mul(RV64IntOP73 op) {
    return ((op == RV64IntOP73::MUL) || (op == RV64IntOP73::MULH) || (op == RV64IntOP73::MULHSU) ||
    (op == RV64IntOP73::MULHU) || (op == RV64IntOP73::DIV) || (op == RV64IntOP73::DIVU) ||
    (op == RV64IntOP73::REM) || (op == RV64IntOP73::REMU)
    );
}

enum class RV64FPOP5 {
    ADD     = 0x00,
    SUB     = 0x01,
    MUL     = 0x02,
    DIV     = 0x03,
    SQRT    = 0x0b,
    SGNJ    = 0x04,
    MIN     = 0x05,
    MVF2F   = 0x08,
    CMP     = 0x14,
    CVTF2I  = 0x18,
    CVTI2F  = 0x1a,
    MVF2I   = 0x1c,
    MVI2F   = 0x1e,
};

inline bool rv64_fpop_is_i_rd(RV64FPOP5 op) {
    return ((op == RV64FPOP5::CMP) || (op == RV64FPOP5::CVTF2I) || (op == RV64FPOP5::MVF2I));
}
inline bool rv64_fpop_is_i_s1(RV64FPOP5 op) {
    return ((op == RV64FPOP5::CVTI2F) || (op == RV64FPOP5::MVI2F));
}
inline bool rv64_fpop_has_s2(RV64FPOP5 op) {
    return ((op == RV64FPOP5::ADD) || (op == RV64FPOP5::SUB) ||
    (op == RV64FPOP5::MUL) || (op == RV64FPOP5::DIV) ||
    (op == RV64FPOP5::SGNJ) || (op == RV64FPOP5::MIN) ||
    (op == RV64FPOP5::CMP)
    );
}

enum class RV64FPWidth2 {
    fword   = 0,
    fdword  = 1,
    fharf   = 2,
    fqword  = 3
};

enum class RV64FPCVTWidth5 {
    word    = 0,
    uword   = 1,
    dword   = 2,
    udword  = 3
};

enum class RV64FPRM3 {
    RNE     = 0,
    RTZ     = 1,
    RDN     = 2,
    RUP     = 3,
    RMM     = 4,
    DYN     = 7
};

enum class RV64FPMIN3 {
    MIN     = 0,
    MAX     = 1
};

enum class RV64FPSGNJ3 {
    SGNJ    = 0,
    SGNJN   = 1,
    SGNJX   = 2
};

enum class RV64FPMVI3 {
    RAW     = 0,
    FCLASS  = 1
};

enum class RV64FPCMP3 {
    FEQ     = 2,
    FLT     = 1,
    FLE     = 0
};

typedef struct {
    RV64FPOP5       op;
    RV64FPWidth2    fwid;
    RV64FPCVTWidth5 iwid;
    union {
        RV64FPRM3   rm;
        RV64FPMIN3  minmax;
        RV64FPSGNJ3 sgnj;
        RV64FPMVI3  mv;
        RV64FPCMP3  cmp;
    } funct3;
} RV64FPParam;

enum class RV64CSROP3 {
    CSRRW   = 1,
    CSRRS   = 2,
    CSRRC   = 3,
    CSRRWI  = 5,
    CSRRSI  = 6,
    CSRRCI  = 7
};

typedef uint16_t RVCSRIndexT;

typedef struct {
    RV64CSROP3      op;
    RVCSRIndexT     index;
} RV64CSRParam;


typedef uint64_t RVInstFlagT;
#define RVINSTFLAG_RVC      (1 << 0)
#define RVINSTFLAG_UNIQUE   (1 << 1)
#define RVINSTFLAG_FENCE    (1 << 2)
#define RVINSTFLAG_FENCEI   (1 << 3)
#define RVINSTFLAG_FENCETSO (1 << 4)
#define RVINSTFLAG_SFENCE   (1 << 5)
#define RVINSTFLAG_PAUSE    (1 << 8)
#define RVINSTFLAG_ECALL    (1 << 9)
#define RVINSTFLAG_EBREAK   (1 << 10)

#define RVINSTFLAG_S1INT    (1 << 16)
#define RVINSTFLAG_S1FP     (1 << 17)
#define RVINSTFLAG_S2INT    (1 << 18)
#define RVINSTFLAG_S2FP     (1 << 19)
#define RVINSTFLAG_S3INT    (1 << 20)
#define RVINSTFLAG_S3FP     (1 << 21)
#define RVINSTFLAG_RDINT    (1 << 22)
#define RVINSTFLAG_RDFP     (1 << 23)

typedef union {
    RV64LSWidth     loadstore;
    RV64BranchOP3   branch;
    RV64IntOP73     intop;
    RV64AMOParam    amo;
    RV64FPParam     fp;
    RV64CSRParam    csr;
} RV64InstParam;

typedef struct {
    VirtAddrT pc = 0;
    RV64OPCode opcode = RV64OPCode::nop;

    RV64InstParam param;

    RVRegIndexT rs1;
    RVRegIndexT rs2;
    RVRegIndexT rs3;
    RVRegIndexT rd;
    IntDataT imm;

    RVInstFlagT flag;

    string debug_name_str;

    inline bool is_rvc() { return ((flag & RVINSTFLAG_RVC) > 0); }
    inline bool is_unique() { return ((flag & RVINSTFLAG_UNIQUE) > 0); }
} RV64InstDecoded;

#define RVCSR_FFLAGS    (0x001U)
#define RVCSR_FRM       (0x002U)
#define RVCSR_FCSR      (0x003U)

#define RV_FFLASG_MASK          (0x1fUL)
#define RV_FRM_MASK             (0xe0UL)

#define RV_FFLAGS_INVALID       (1UL<<4)
#define RV_FFLAGS_DIVBYZERO     (1UL<<3)
#define RV_FFLAGS_OVERFLOW      (1UL<<2)
#define RV_FFLAGS_UNDERFLOW     (1UL<<1)
#define RV_FFLAGS_INEXACT       (1UL<<0)

bool decode_rv64(RVInstT raw, RV64InstDecoded *dec);

void init_rv64_inst_name_str(RV64InstDecoded *inst);

inline bool isRVC(RVInstT inst) {
    return ((inst & 3) != 3);
}

inline bool pdec_isifence(RVInstT inst) {
    return (inst & ((1<<7)-1)) == 15 && ((inst>>12) & ((1<<3)-1)) == 1;
}

inline bool pdec_isJ(RVInstT inst) {
    if(isRVC(inst)) {
        if((inst & 3) == 1 && ((inst>>13) & 7) == 5) return true;
        if((inst & 0x7f) == 2 && ((inst>>13) & 7) == 4) return true;
    }
    else {
        if((inst & 0x7F) == OP_JAL || (inst & 0x7F) == OP_JALR) return true;
    }
    return false;
}

inline bool pdec_isJR(RVInstT inst) {
    if(isRVC(inst)) {
        if((inst & 0x7f) == 2 && ((inst>>13) & 7) == 4 && ((inst >> 7) & 31)) return true;
    }
    else {
        if((inst & 0x7F) == OP_JALR) return true;
    }
    return false;
}

inline bool pdec_isJI(RVInstT inst) {
    if(isRVC(inst)) {
        if((inst & 3) == 1 && ((inst>>13) & 7) == 5) return true;
    }
    else {
        if((inst & 0x7F) == OP_JAL) return true;
    }
    return false;
}

inline bool pdec_isCALLI(RVInstT inst) {
    if((inst & 0x7F) == OP_JAL && ((inst >> 7) & 31) == 1) return true;
    return false;
}

inline bool pdec_isCALLR(RVInstT inst) {
    if(isRVC(inst)) {
        if((inst & 0x7f) == 2 && ((inst>>12) & 15) == 9 && ((inst >> 7) & 31) != 0) return true;
    }
    else {
        if((inst & 0x7F) == OP_JALR && ((inst >> 7) & 31) == 1) return true;
    }
    return false;
}

inline bool pdec_isRET(RVInstT inst) {
    if(isRVC(inst)) {
        if((inst & 0x7f) == 2 && ((inst>>12) & 15) == 8 && ((inst >> 7) & 31) == 1) return true;
    }
    else {
        if((inst & 0x7F) == OP_JALR && ((inst >> 7) & 31) != 1 && ((inst >> 15) & 31) == 1) return true;
    }
    return false;
}

bool pdec_get_J_target(RVInstT inst, int32_t *target);

inline bool pdec_isB(RVInstT inst) {
    if(isRVC(inst)) {
        if((inst & 3) == 1 && ((inst>>13) & 7) == 6) return true;
        if((inst & 3) == 1 && ((inst>>13) & 7) == 7) return true;
    }
    else {
        if((inst & 0x7F) == OP_BRANCH) return true;
    }
    return false;
}

bool pdec_get_B_target(RVInstT inst, int32_t *target);

inline bool pdec_is_uniq(RVInstT inst) {
    if(isRVC(inst)) {
        if(inst = 0x9002) return true;
    }
    else {
        if((inst & 0x7F) == OP_MISCMEM) return true;
        if((inst & 0x7F) == OP_SYSTEM) return true;
    }
    return false;
}

}

namespace test {

bool test_decoder_rv64();

}

#endif

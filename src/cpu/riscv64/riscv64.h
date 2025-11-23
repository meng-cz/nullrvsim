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

typedef uint32_t InstT;
typedef uint32_t InstCT;

enum class IRegIdx {
    x0  = 0,
    ra  = 1,
    sp  = 2,
    gp  = 3,
    tp  = 4,
    t0  = 5,
    t1  = 6,
    t2  = 7,
    s0  = 8,
    s1  = 9,
    a0  = 10,
    a1  = 11,
    a2  = 12,
    a3  = 13,
    a4  = 14,
    a5  = 15,
    a6  = 16,
    a7  = 17,
    s2  = 18,
    s3  = 19,
    s4  = 20,
    s5  = 21,
    s6  = 22,
    s7  = 23,
    s8  = 24,
    s9  = 25,
    s10 = 26,
    s11 = 27,
    t3  = 28,
    t4  = 29,
    t5  = 30,
    t6  = 31,
};

enum class FRegIdx {
    ft0  = 0,
    ft1  = 1,
    ft2  = 2,
    ft3  = 3,
    ft4  = 4,
    ft5  = 5,
    ft6  = 6,
    ft7  = 7,
    fs0  = 8,
    fs1  = 9,
    fa0  = 10,
    fa1  = 11,
    fa2  = 12,
    fa3  = 13,
    fa4  = 14,
    fa5  = 15,
    fa6  = 16,
    fa7  = 17,
    fs2  = 18,
    fs3  = 19,
    fs4  = 20,
    fs5  = 21,
    fs6  = 22,
    fs7  = 23,
    fs8  = 24,
    fs9  = 25,
    fs10 = 26,
    fs11 = 27,
    ft8  = 28,
    ft9  = 29,
    ft10 = 30,
    ft11 = 31,
};

typedef uint16_t RegIdxT;

#define RV64_IREG_IDX(name) ((RegIdxT)(IRegIdx::##name))
#define RV64_FREG_IDX(name) ((RegIdxT)(FRegIdx::##name))

string get_ireg_name(RegIdxT index);
string get_freg_name(RegIdxT index);

enum class OPCode {
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

enum class SrcType : uint8_t {
    ZERO = 0,
    IMM,
    PC,
    IREG,
    FREG,
    VREG,
    V0,
};

enum class DestType : uint8_t {
    NONE = 0,
    IREG,
    FREG,
    VREG,
};

enum class ExeType : uint32_t {
    ALU,    // add, sub, bits
    MUL,    // mul, mulh
    DIV,    // div, rem
    LOAD,   // ld, lw, lh, lb, ...
    STORE,  // sd, sw, ...
    BRANCH, // bne, beq, ...
    JUMP,   // auipc, jal, jalr
    CSR,    // system: csrrw, ..., ecall, ebreak, mret, sret, ...
    FENCE,  // miscmem: fence, fencei, ...
    /* A Extension */
    AMO,    // lr, sc, amoadd, ..., _d, _w
    /* F/D/Q/Zfh Extension */
    FALU,   // fadd, fsub, fmin, fsgnj
    FCVT,   // fcvt2f
    FCMP,   // fcmp
    FMUL,   // fmul
    FDIV,   // fdiv
    FSQRT,  // fsqrt
    FMA,    // fmadd, fmsub, fnmsub, fnmadd
    I2F,    // fcvti2f, fmvi2f
    F2I,    // fcvtf2i, fmvf2i, fclass
    /* B/K Extension */
    BITS,
    CRYPTO,
    /* V Extension */
    VLOAD,
    VSTORE,
    VSETCSR,
    VIALU,
    VIMUL,
    VIDIV,
    VIMAC,
    VIPU,
    VPPU,
    VFALU,
    VFMA,
    VFCVT,
    VFMUL,
    VFDIV,
    VFSQRT,
    VFMAC,
    VMOVE,
    I2V,
    F2V,
    /* Invalid */
    INVALID
};

typedef uint32_t ExeOPType;

typedef struct {
    InstT rawinst;

    OPCode opcode;

    DestType desttype;
    SrcType srctype1;
    SrcType srctype2;
    SrcType srctype3;
    RegIdxT rd;
    RegIdxT rs1;
    RegIdxT rs2;
    RegIdxT rs3;
    int64_t imm;

    ExeType exetype;
    ExeOPType exeop;

    bool isrvc;

} InstInfo;

bool decode_inst(InstCT inst, InstInfo *instinfo);
bool decode_rvc_inst(InstCT inst, InstInfo *instinfo);


} // namespace riscv64

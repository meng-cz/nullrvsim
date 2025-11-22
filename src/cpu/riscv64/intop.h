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

enum class ALUOPType : uint32_t {
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
    ADDW    = 0x010,
    SUBW    = 0x110,
    SLLW    = 0x011,
    SLTW    = 0x012,
    SLTUW   = 0x013,
    XORW    = 0x014,
    SRLW    = 0x015,
    SRAW    = 0x115,
    ORW     = 0x016,
    ANDW    = 0x017,
};

uint64_t perform_alu_op(ALUOPType optype, uint64_t s1, uint64_t s2);

enum class MULOPType : uint32_t {
    MUL     = 0x08,
    MULH    = 0x09,
    MULHSU  = 0x0a,
    MULHU   = 0x0b,
    MULW    = 0x18,
};

uint64_t perform_mul_op(MULOPType optype, uint64_t s1, uint64_t s2);

enum class DIVOPType : uint32_t {
    DIV     = 0x0c,
    DIVU    = 0x0d,
    REM     = 0x0e,
    REMU    = 0x0f,
    DIVW    = 0x1c,
    DIVUW   = 0x1d,
    REMW    = 0x1e,
    REMUW   = 0x1f,
};

uint64_t perform_div_op(DIVOPType optype, uint64_t s1, uint64_t s2);

enum class LOADOPType : uint32_t {
    LB      = 0x0,
    LH      = 0x1,
    LW      = 0x2,
    LD      = 0x3,
    LBU     = 0x4,
    LHU     = 0x5,
    LWU     = 0x6,
};

enum class STOREOPType : uint32_t {
    SB      = 0x0,
    SH      = 0x1,
    SW      = 0x2,
    SD      = 0x3,
};

enum class BRANCHOPType : uint32_t {
    BEQ     = 0x0,
    BNE     = 0x1,
    BLT     = 0x4,
    BGE     = 0x5,
    BLTU    = 0x6,
    BGEU    = 0x7
};

enum class JUMPOPType : uint32_t {
    AUIPC   = 0x0,
    JAL     = 0x1,
    JALR    = 0x2,
};

enum class CSROPType : uint32_t {
    RW      = 0x01,
    RS      = 0x02,
    RC      = 0x03,
    RWI     = 0x05,
    RSI     = 0x06,
    RCI     = 0x07,
    EBREAK  = 0x10,
    ECALL   = 0x11,
    SRET    = 0x12,
    MRET    = 0x13,
    MNRET   = 0x14,
    DRET    = 0x15,
    WFI     = 0x16,
    /* Zawrs Extension */
    WRSNTO  = 0x20,
    WRSSTO  = 0x21,
};

enum class FENCEOPType : uint32_t {
    FENCE       = 0x00,
    FENCEI      = 0x01,
    SFENCE      = 0x02,
    /* H Extension */
    HFENCEG     = 0x10,
    HFENCEV     = 0x11,
};

} 

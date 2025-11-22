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
#include "riscv64.h"

namespace riscv64 {

enum class FFlags : uint32_t {
    NONE        = 0U,
    INEXACT     = 1U << 0,
    UNDERFLOW   = 1U << 1,
    OVERFLOW    = 1U << 2,
    DIVBYZERO   = 1U << 3,
    INVALID     = 1U << 4,
};

inline ExeOPType fp_generate_optype(InstT rawinst) {
#define GET_BITS_AT(n, offset, len) (((n) >> (offset)) & ((1<<(len)) - 1))
    return GET_BITS_AT(rawinst, 12, 3) | (GET_BITS_AT(rawinst, 20, 12) << 3);
#undef GET_BITS_AT
}

enum class FPRM : uint8_t {
    RNE     = 0,
    RTZ     = 1,
    RDN     = 2,
    RUP     = 3,
    RMM     = 4,
    DYN     = 7
};

inline FPRM fp_extract_optype_rm(ExeOPType fpu_optype) {
    return static_cast<FPRM>(fpu_optype & 7U);
}

enum class FPOP5 : uint8_t {
    ADD     = 0x00,
    SUB     = 0x01,
    MUL     = 0x02,
    DIV     = 0x03,
    SQRT    = 0x0b,
    SGNJ    = 0x04,
    MIN     = 0x05,
    CVTF2F  = 0x08,
    CMP     = 0x14,
    CVTF2I  = 0x18,
    CVTI2F  = 0x1a,
    MVF2I   = 0x1c,
    MVI2F   = 0x1e,
};

inline FPOP5 fp_extract_optype_op5(ExeOPType fpu_optype) {
    return static_cast<FPOP5>((fpu_optype >> 10) & 31U);
}

inline ExeType fp_detect_exe_type(FPOP5 fpop) {
    switch (fpop)
    {
    case FPOP5::ADD:
    case FPOP5::SUB: return ExeType::FALU;
    case FPOP5::MUL: return ExeType::FMUL;
    case FPOP5::DIV: return ExeType::FDIV;
    case FPOP5::SQRT: return ExeType::FSQRT;
    case FPOP5::SGNJ:
    case FPOP5::MIN: return ExeType::FALU;
    case FPOP5::CVTF2F: return ExeType::FCVT;
    case FPOP5::CMP: return ExeType::FCMP;
    case FPOP5::CVTF2I: return ExeType::F2I;
    case FPOP5::CVTI2F: return ExeType::I2F;
    case FPOP5::MVF2I: return ExeType::F2I;
    case FPOP5::MVI2F: return ExeType::I2F;
    default: return ExeType::FALU;
    }
}

enum class FPWidth2 : uint8_t {
    FS      = 0,
    FD      = 1,
    FH      = 2,
    FQ      = 3
};

inline FPWidth2 fp_extract_optype_width2(ExeOPType fpu_optype) {
    return static_cast<FPWidth2>((fpu_optype >> 8) & 3U);
}
inline FPWidth2 fp_extract_optype_cvtwidth2(ExeOPType fpu_optype) {
    return static_cast<FPWidth2>((fpu_optype >> 3) & 3U);
}

enum class FPIntWidth5 : uint8_t {
    IW      = 0,
    IWU     = 1,
    IL      = 2,
    ILU     = 3,
};

inline FPIntWidth5 fp_extract_optype_intwidth2(ExeOPType fpu_optype) {
    return static_cast<FPIntWidth5>((fpu_optype >> 3) & 3U);
}

enum class FPMIN3 : uint8_t {
    MIN     = 0,
    MAX     = 1
};
inline FPMIN3 fp_extract_optype_min3(ExeOPType fpu_optype) {
    return static_cast<FPMIN3>(fpu_optype  & 7U);
}

enum class FPSGNJ3 : uint8_t {
    SGNJ    = 0,
    SGNJN   = 1,
    SGNJX   = 2
};
inline FPSGNJ3 fp_extract_optype_sgnj3(ExeOPType fpu_optype) {
    return static_cast<FPSGNJ3>(fpu_optype  & 7U);
}

enum class FPMVI3 : uint8_t {
    RAW     = 0,
    FCLASS  = 1
};
inline FPMVI3 fp_extract_optype_mvi3(ExeOPType fpu_optype) {
    return static_cast<FPMVI3>(fpu_optype  & 7U);
}

enum class FPCMP3 : uint8_t {
    FEQ     = 2,
    FLT     = 1,
    FLE     = 0
};
inline FPCMP3 fp_extract_optype_cmp3(ExeOPType fpu_optype) {
    return static_cast<FPCMP3>(fpu_optype  & 7U);
}


uint64_t perform_falu_op(ExeOPType optype, uint64_t s1, uint64_t s2, uint64_t *fcsr);

uint64_t perform_fcvt_op(ExeOPType optype, uint64_t s1, uint64_t *fcsr);

uint64_t perform_fcmp_op(ExeOPType optype, uint64_t s1, uint64_t s2, uint64_t *fcsr);

uint64_t perform_fmul_op(ExeOPType optype, uint64_t s1, uint64_t s2, uint64_t *fcsr);

uint64_t perform_fdiv_op(ExeOPType optype, uint64_t s1, uint64_t s2, uint64_t *fcsr);

uint64_t perform_fsqrt_op(ExeOPType optype, uint64_t s1, uint64_t *fcsr);

uint64_t perform_i2f_op(ExeOPType optype, uint64_t s1, uint64_t *fcsr);

uint64_t perform_f2i_op(ExeOPType optype, uint64_t s1, uint64_t *fcsr);



inline ExeOPType fma_generate_optype(InstT rawinst) {
#define GET_BITS_AT(n, offset, len) (((n) >> (offset)) & ((1<<(len)) - 1))
    return GET_BITS_AT(rawinst, 0, 7) | (GET_BITS_AT(rawinst, 12, 3) << 7) | (GET_BITS_AT(rawinst, 25, 2) << 10);
#undef GET_BITS_AT
}

enum class FMAOP7 : uint8_t {
    MADD    = 0x43,
    MSUB    = 0x47,
    NMSUB   = 0x4b,
    NMADD   = 0x4f,
};
inline FMAOP7 fma_extract_optype_op7(ExeOPType fma_optype) {
    return static_cast<FMAOP7>(fma_optype & ((1U << 7) - 1U));
}

inline FPRM fma_extract_optype_rm(ExeOPType fma_optype) {
    return static_cast<FPRM>((fma_optype >> 7) & 7U);
}

inline FPWidth2 fma_extract_optype_width2(ExeOPType fma_optype) {
    return static_cast<FPWidth2>((fma_optype >> 10) & 3U);
}

uint64_t perform_fma_op(ExeOPType optype, uint64_t s1, uint64_t s2, uint64_t s3, uint64_t *fcsr);

}

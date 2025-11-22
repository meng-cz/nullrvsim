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

typedef uint16_t Float16;
typedef uint32_t Float32;
typedef uint64_t Float64;
typedef uint128_t Float128;

typedef uint32_t FexptT;
enum class FloatException : FexptT {
    NONE        = 0U,
    INEXACT     = 1U << 0,
    UNDERFLOW   = 1U << 1,
    OVERFLOW    = 1U << 2,
    DIVBYZERO   = 1U << 3,
    INVALID     = 1U << 4,
};

enum class FloatRound : uint8_t {
    RNE     = 0,
    RTZ     = 1,
    RDN     = 2,
    RUP     = 3,
    RMM     = 4
};

bool fp16_is_qnan(Float16 A);
bool fp16_is_snan(Float16 A);
bool fp16_is_nan(Float16 A);
bool fp16_is_pos_infi(Float16 A);
bool fp16_is_neg_infi(Float16 A);
bool fp16_is_normal(Float16 A);
bool fp16_is_subnormal(Float16 A);
bool fp16_is_pos_zero(Float16 A);
bool fp16_is_neg_zero(Float16 A);
bool fp16_is_zero(Float16 A);

Float16 fp16_add(Float16 A, Float16 B, FloatRound rm, FexptT *expt);
Float16 fp16_sub(Float16 A, Float16 B, FloatRound rm, FexptT *expt);
Float16 fp16_mul(Float16 A, Float16 B, FloatRound rm, FexptT *expt);
Float16 fp16_div(Float16 A, Float16 B, FloatRound rm, FexptT *expt);
Float16 fp16_sqrt(Float16 A, FloatRound rm, FexptT *expt);
bool fp16_equal(Float16 A, Float16 B);
bool fp16_less_than(Float16 A, Float16 B);
bool fp16_less_equal(Float16 A, Float16 B);

uint32_t fp16_to_uint32(Float16 A, FloatRound rm, FexptT *expt);
int32_t fp16_to_int32(Float16 A, FloatRound rm, FexptT *expt);
uint64_t fp16_to_uint64(Float16 A, FloatRound rm, FexptT *expt);
int64_t fp16_to_int64(Float16 A, FloatRound rm, FexptT *expt);

Float16 fp16_from_uint32(uint32_t A, FloatRound rm, FexptT *expt);
Float16 fp16_from_int32(int32_t A, FloatRound rm, FexptT *expt);
Float16 fp16_from_uint64(uint64_t A, FloatRound rm, FexptT *expt);
Float16 fp16_from_int64(int64_t A, FloatRound rm, FexptT *expt);

bool test_fp16();


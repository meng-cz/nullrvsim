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


#include "floatop.h"

#define RAISE_EXPT(name) do { if (expt) [[likely]] {*expt |= ((FexptT)FloatException::name);} } while(0)

/**
 * FP32:
 * 1 sign
 * 8 exp
 * 23 frac
 */

const uint32_t FP32_FRAC_BITS = 23;
const uint32_t FP32_EXP_BITS = 8;

typedef uint32_t FP32FracT;
typedef uint32_t FP32FracEx4T;
typedef int32_t FP32ExpT;
typedef bool FP32SignT;

const FP32ExpT FP32_EXP_FULL = (1 << FP32_EXP_BITS) - 1;
const FP32FracT FP32_FRAC_FULL = (1 << FP32_FRAC_BITS) - 1;
const Float32 FP32_CANONICAL_NAN = (FP32_EXP_FULL << FP32_FRAC_BITS) | (1 << (FP32_FRAC_BITS - 1));



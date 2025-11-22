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

#ifndef RVSIM_CPU_OPERATION_H
#define RVSIM_CPU_OPERATION_H

#include "isa.h"

#include "simerror.h"

namespace isa {

SimError perform_branch_op_64(RV64BranchOP3 op, IntDataT *out, IntDataT s1, IntDataT s2);

SimError perform_int_op_64(RV64IntOP73 op, IntDataT *out, IntDataT s1, IntDataT s2);

SimError perform_int_op_32(RV64IntOP73 op, IntDataT *out, IntDataT s1, IntDataT s2);

SimError perform_fp_op(RV64FPParam param, RawDataT *out, RawDataT s1, RawDataT s2, uint64_t *p_fcsr);

SimError perform_fmadd_op(RV64OPCode opcode, RV64FPWidth2 fwid, RawDataT *out, FPDataT s1, FPDataT s2, FPDataT s3, uint64_t *p_fcsr);

};

namespace test {

bool test_operation();

}

#endif

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

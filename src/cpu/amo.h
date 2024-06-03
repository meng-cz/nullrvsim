#ifndef RVSIM_ISA_AMO_H
#define RVSIM_ISA_AMO_H

#include "common.h"
#include "simerror.h"

namespace isa {

enum class RV64LSWidth {
    byte    = 0,
    harf    = 1,
    word    = 2,
    dword   = 3,
    ubyte   = 4,
    uharf   = 5,
    uword   = 6,
    qword   = 8,
};

enum class RV64AMOOP5 {
    ADD     = 0x00,
    SWAP    = 0x01,
    LR      = 0x02,
    SC      = 0x03,
    XOR     = 0x04,
    AND     = 0x0c,
    OR      = 0x08,
    MIN     = 0x10,
    MAX     = 0x14,
    MINU    = 0x18,
    MAXU    = 0x1c
};

typedef struct {
    RV64AMOOP5  op;
    RV64LSWidth wid;
} RV64AMOParam;

/**
 * s1: value currently stored in memory
 * s2: oprand
 * out: value to be stored in memory
*/
SimError perform_amo_op(RV64AMOParam param, IntDataT *out, IntDataT s1, IntDataT s2);

}

#endif

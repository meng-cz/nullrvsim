
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



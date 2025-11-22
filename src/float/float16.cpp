
#include "floatop.h"

#define RAISE_EXPT(name) do { if (expt) [[likely]] {*expt |= ((FexptT)FloatException::name);} } while(0)

/**
 * FP16:
 * 1 sign
 * 5 exp
 * 10 frac
 */

const uint32_t FP16_FRAC_BITS = 10;
const uint32_t FP16_EXP_BITS = 5;

typedef uint32_t FP16FracT;
typedef uint32_t FP16FracEx4T;
typedef int32_t FP16ExpT;
typedef bool FP16SignT;

const FP16ExpT FP16_EXP_FULL = (1 << FP16_EXP_BITS) - 1;
const FP16FracT FP16_FRAC_FULL = (1 << FP16_FRAC_BITS) - 1;
const Float16 FP16_CANONICAL_NAN = (FP16_EXP_FULL << FP16_FRAC_BITS) | (1 << (FP16_FRAC_BITS - 1));

inline FP16ExpT _fp16_extract_exp(Float16 x) {
    return (x >> FP16_FRAC_BITS) & FP16_EXP_FULL;
}
inline FP16FracT _fp16_extract_frac(Float16 x) {
    return x & FP16_FRAC_FULL;
}
inline FP16SignT _fp16_extract_sign(Float16 x) {
    return (x >> (FP16_FRAC_BITS + FP16_EXP_BITS)) & 0x1;
}

inline FP16FracEx4T _fp16_shift_right_jam(FP16FracEx4T frac, uint32_t dist) {
    if (dist >= (sizeof(frac) * 8 - 1)) {
        return (frac != 0)?1:0;
    } else {
        FP16FracEx4T extra = frac & (((FP16FracEx4T)1UL << dist) - 1);
        frac = frac >> dist;
        if (extra) {
            frac |= 1;
        }
        return frac;
    }
}

bool fp16_is_nan(Float16 A) {
    return (_fp16_extract_exp(A) == FP16_EXP_FULL) && (_fp16_extract_frac(A) != 0);
}
bool fp16_is_qnan(Float16 s1) {
    return (_fp16_extract_exp(s1) == FP16_EXP_FULL) && (_fp16_extract_frac(s1) != 0) && (_fp16_extract_frac(s1) & (1 << (FP16_FRAC_BITS - 1)));
}
bool fp16_is_snan(Float16 s1) {
    return (_fp16_extract_exp(s1) == FP16_EXP_FULL) && (_fp16_extract_frac(s1) != 0) && !(_fp16_extract_frac(s1) & (1 << (FP16_FRAC_BITS - 1)));
}
bool fp16_is_pos_infi(Float16 A) {
    return (_fp16_extract_exp(A) == FP16_EXP_FULL) && (_fp16_extract_frac(A) == 0) && _fp16_extract_sign(A);
}
bool fp16_is_neg_infi(Float16 A) {
    return (_fp16_extract_exp(A) == FP16_EXP_FULL) && (_fp16_extract_frac(A) == 0) && !_fp16_extract_sign(A);
}
bool fp16_is_normal(Float16 A) {
    return (_fp16_extract_exp(A) != FP16_EXP_FULL) && (_fp16_extract_exp(A) != 0);
}
bool fp16_is_subnormal(Float16 A) {
    return (_fp16_extract_exp(A) == 0) && (_fp16_extract_frac(A) != 0);
}
bool fp16_is_pos_zero(Float16 A) {
    return (A == 0);
}
bool fp16_is_neg_zero(Float16 A) {
    return (A == (1 << (FP16_FRAC_BITS + FP16_EXP_BITS)));
}
bool fp16_is_zero(Float16 A) {
    return (_fp16_extract_exp(A) == 0) && (_fp16_extract_frac(A) == 0);
}

inline Float16 _fp16_propagate_nan(Float16 s1, Float16 s2, FexptT *expt) {
    if (fp16_is_snan(s1) || fp16_is_snan(s2)) {
        RAISE_EXPT(INVALID);
    }
    return FP16_CANONICAL_NAN;
}

inline Float16 _fp16_pack(FP16SignT sign, FP16ExpT exp, FP16FracT frac) {
    return (sign?(1<<(FP16_FRAC_BITS + FP16_EXP_BITS)):0) | ((exp & FP16_EXP_FULL) << FP16_FRAC_BITS) | (frac & FP16_FRAC_FULL);
}

inline Float16 _fp16_round_pack_normal_ex4(FP16SignT sign, FP16ExpT exp, FP16FracEx4T sigex, FloatRound rm, FexptT *expt) {

    FP16FracEx4T roundBits = sigex & 0xf;
    bool increament = false;

    switch (rm)
    {
    case FloatRound::RNE:
        if (roundBits > 0x8 || (roundBits == 0x8 && (sigex & 0x10))) {
            increament = true;
        }
        break;
    case FloatRound::RDN:
        if (sign && roundBits) {
            increament = true;
        }
        break;
    case FloatRound::RUP:
        if (!sign && roundBits) {
            increament = true;
        }
        break;
    }
    if (increament) {
        sigex += 0x10;
        if (sigex & (1 << (FP16_FRAC_BITS + 5))) {
            sigex = _fp16_shift_right_jam(sigex, 1);
            exp++;
            roundBits = sigex & 0xf;
        }
    }
    if (exp >= FP16_EXP_FULL) {
        RAISE_EXPT(OVERFLOW);
        RAISE_EXPT(INEXACT);
        return _fp16_pack(sign, FP16_EXP_FULL, 0);
    }
    if (roundBits) {
        RAISE_EXPT(INEXACT);
    }
    return _fp16_pack(sign, exp, sigex >> 4);
}

Float16 _fp16_add_mags(Float16 A, Float16 B, FloatRound rm, FexptT *expt) {
    FP16ExpT expA = _fp16_extract_exp(A);
    FP16FracT sigA = _fp16_extract_frac(A);
    FP16ExpT expB = _fp16_extract_exp(B);
    FP16FracT sigB = _fp16_extract_frac(B);

    FP16SignT sign = _fp16_extract_sign(A);

    if (fp16_is_nan(A) || fp16_is_nan(B)) {
        return _fp16_propagate_nan(A, B, expt); // nan
    } else if(expA == FP16_EXP_FULL || expB == FP16_EXP_FULL) {
        return _fp16_pack(sign, FP16_EXP_FULL, 0); // inf
    }

    FP16ExpT exp_sum = std::max(expA, expB);
    if (exp_sum == 0) {
        // subnormal + subnormal
        return A + sigB;
    }

    FP16ExpT exp_diff = expA - expB;
    FP16FracEx4T sigA_ex4 = (sigA + (expA ? (1 << (FP16_FRAC_BITS)) : sigA)) << 4; // x2 when subnormal
    FP16FracEx4T sigB_ex4 = (sigB + (expB ? (1 << (FP16_FRAC_BITS)) : sigB)) << 4;

    if (exp_diff > 0) {
        sigB_ex4 = _fp16_shift_right_jam(sigB_ex4, exp_diff);
    } else if(exp_diff < 0) {
        sigA_ex4 = _fp16_shift_right_jam(sigA_ex4, -exp_diff);
    }
    FP16FracEx4T sig_sum = sigA_ex4 + sigB_ex4;

    // check highest bit position
    if (sig_sum & (1 << (FP16_FRAC_BITS + 5))) {
        sig_sum = _fp16_shift_right_jam(sig_sum, 1);
        exp_sum += 1;
    }

    return _fp16_round_pack_normal_ex4(sign, exp_sum, sig_sum, rm , expt);
}

Float16 _fp16_sub_mags(Float16 A, Float16 B, FloatRound rm, FexptT *expt) {
    FP16ExpT expA = _fp16_extract_exp(A);
    FP16FracT sigA = _fp16_extract_frac(A);
    FP16ExpT expB = _fp16_extract_exp(B);
    FP16FracT sigB = _fp16_extract_frac(B);

    FP16SignT signA = _fp16_extract_sign(A);
    FP16SignT signB = _fp16_extract_sign(B);

    if (fp16_is_nan(A) || fp16_is_nan(B)) {
        return _fp16_propagate_nan(A, B, expt); // nan
    }

    bool infA = (expA == FP16_EXP_FULL);
    bool infB = (expB == FP16_EXP_FULL);
    if (infA && infB) {
        RAISE_EXPT(INVALID);
        return FP16_CANONICAL_NAN; // nan
    } else if(infA) {
        return _fp16_pack(signA, FP16_EXP_FULL, 0); // inf
    } else if(infB) {
        return _fp16_pack(signB, FP16_EXP_FULL, 0); // inf
    }

    FP16ExpT exp_sum = std::max(expA, expB);
    if (exp_sum == 0) {
        // subnormal - subnormal
        if (sigA == sigB) {
            return _fp16_pack((rm == FloatRound::RDN), 0, 0); // zero
        }
        FP16FracT sig_diff = (sigA > sigB)?(sigA - sigB):(sigB - sigA);
        FP16SignT result_sign = (sigA > sigB)?signA:!signB;
        return _fp16_pack(result_sign, 0, sig_diff);
    }

    FP16ExpT exp_diff = expA - expB;
    FP16FracEx4T sigA_ex4 = (sigA + (expA ? (1 << (FP16_FRAC_BITS)) : sigA)) << 4; // x2 when subnormal
    FP16FracEx4T sigB_ex4 = (sigB + (expB ? (1 << (FP16_FRAC_BITS)) : sigB)) << 4;

    if (exp_diff > 0) {
        sigB_ex4 = _fp16_shift_right_jam(sigB_ex4, exp_diff);
    } else if(exp_diff < 0) {
        sigA_ex4 = _fp16_shift_right_jam(sigA_ex4, -exp_diff);
    }

    FP16FracEx4T sig_diff_ex = (sigA_ex4 >= sigB_ex4)?(sigA_ex4 - sigB_ex4):(sigB_ex4 - sigA_ex4);
    FP16SignT result_sign = (sigA_ex4 >= sigB_ex4)?signA:!signB;

    if ((sig_diff_ex >> 4) == 0) {
        return _fp16_pack((rm == FloatRound::RDN), 0, 0); // zero
    }
    int32_t highest_bitpos = count_highest_bitpos_1begin(sig_diff_ex >> 4);
    int32_t need_left_shift = (int32_t)(FP16_FRAC_BITS + 1) - (int32_t)highest_bitpos;
    // check subnormal
    if (need_left_shift > exp_sum - 1) {
        RAISE_EXPT(UNDERFLOW);
        if (sig_diff_ex & 0xf) {
            RAISE_EXPT(INEXACT);
        }
        return _fp16_pack(result_sign, 0, (sig_diff_ex << (exp_sum - 1)) >> 4);
    }

    exp_sum = exp_sum - need_left_shift;
    sig_diff_ex = sig_diff_ex << need_left_shift;
    return _fp16_round_pack_normal_ex4(result_sign, exp_sum, sig_diff_ex, rm , expt);
}

Float16 fp16_add(Float16 A, Float16 B, FloatRound rm, FexptT *expt) {
    FP16SignT signA = _fp16_extract_sign(A);
    FP16SignT signB = _fp16_extract_sign(B);
    if (signA == signB) {
        return _fp16_add_mags(A, B, rm, expt);
    } else {
        return _fp16_sub_mags(A, B, rm, expt);
    }
}

Float16 fp16_sub(Float16 A, Float16 B, FloatRound rm, FexptT *expt) {
    FP16SignT signA = _fp16_extract_sign(A);
    FP16SignT signB = _fp16_extract_sign(B);
    if (signA != signB) {
        return _fp16_add_mags(A, B, rm, expt);
    } else {
        return _fp16_sub_mags(A, B, rm, expt);
    }
}








string test_fp16_to_binary_str(Float16 f) {
    string ret = "";
    for (int i = 15; i >= 0; i--) {
        ret += (f & (1 << i)) ? '1' : '0';
        if (i == 15 || i == 10) ret += '_';
    }
    return ret;
}

bool test_fp16_add_case(uint32_t idx, Float16 A, Float16 B, FloatRound rm, Float16 expected_res, uint32_t expected_expt) {
    FexptT expt = 0;
    Float16 res = fp16_add(A, B, rm, &expt);
    if (res != expected_res) {
        printf("FP16_ADD test failed [%d]: expected %s, got %s\n", idx, test_fp16_to_binary_str(expected_res).c_str(), test_fp16_to_binary_str(res).c_str());
        return false;
    }
    if ((uint32_t)expt != expected_expt) {
        printf("FP16_ADD test failed [%d]: expected expt 0x%x, got 0x%x\n", idx, expected_expt, (uint32_t)expt);
        return false;
    }
    return true;
}

bool test_fp16_add() {
    if(!test_fp16_add_case(0, 0x3c00, 0x3c00, FloatRound::RNE, 0x4000, 0)) return false;
    if(!test_fp16_add_case(1, 0x0001, 0x03ff, FloatRound::RNE, 0x0400, 0)) return false;
    if(!test_fp16_add_case(2, 0x0001, 0x3c00, FloatRound::RNE, 0x3c00, (uint32_t)(FloatException::INEXACT))) return false;
    if(!test_fp16_add_case(3, 0x0001, 0x3c00, FloatRound::RUP, 0x3c01, (uint32_t)(FloatException::INEXACT))) return false;
    if(!test_fp16_add_case(4, 0x7bff, 0x7bff, FloatRound::RNE, 0x7c00, (uint32_t)(FloatException::INEXACT)|(uint32_t)(FloatException::OVERFLOW))) return false;
    if(!test_fp16_add_case(5, 0x3c00, 0x3c00|0x8000, FloatRound::RNE, 0x0000, 0)) return false;
    if(!test_fp16_add_case(6, 0x3c00|0x8000, 0x3c00, FloatRound::RNE, 0x0000, 0)) return false;
    if(!test_fp16_add_case(7, 0x3c00|0x8000, 0x3c00, FloatRound::RDN, 0x8000, 0)) return false;
    if(!test_fp16_add_case(8, 0x03ff, 0x03f0|0x8000, FloatRound::RDN, 0x000f, 0)) return false;
    if(!test_fp16_add_case(9, 0x0400, 0x03ff|0x8000, FloatRound::RDN, 0x0001, (uint32_t)(FloatException::UNDERFLOW))) return false;
    if(!test_fp16_add_case(10, 0x4000, 0x3c00|0x8000, FloatRound::RDN, 0x3c00, 0)) return false;
    if(!test_fp16_add_case(11, 0x07ff, 0x0400|0x8000, FloatRound::RDN, 0x03ff, (uint32_t)(FloatException::UNDERFLOW))) return false;
    if(!test_fp16_add_case(12, 0x07ff|0x8000, 0x0400, FloatRound::RDN, 0x03ff|0x8000, (uint32_t)(FloatException::UNDERFLOW))) return false;
    if(!test_fp16_add_case(13, 0x7c00|0x8000, 0x7c00, FloatRound::RDN, FP16_CANONICAL_NAN, (uint32_t)(FloatException::INVALID))) return false;
    if(!test_fp16_add_case(14, FP16_CANONICAL_NAN, 0x7c00, FloatRound::RDN, FP16_CANONICAL_NAN, 0)) return false;
    if(!test_fp16_add_case(15, 0x7c00, 0x3c00, FloatRound::RDN, 0x7c00, 0)) return false;
    return true;
}

bool test_fp16() {
    if(!test_fp16_add()) return false;
    printf("Test FP16 Passed !!!\n");
    return true;
}

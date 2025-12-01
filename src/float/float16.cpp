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
    } else if (exp_diff < 0) {
        sigA_ex4 = _fp16_shift_right_jam(sigA_ex4, -exp_diff);
    }
    FP16FracEx4T sig_sum = sigA_ex4 + sigB_ex4;

    // check highest bit position
    if (sig_sum & (1 << (FP16_FRAC_BITS + 5))) {
        sig_sum = _fp16_shift_right_jam(sig_sum, 1);
        exp_sum += 1;
    }

    return _fp16_round_pack_normal_ex4(sign, exp_sum, sig_sum, rm, expt);
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
    } else if (infA) {
        return _fp16_pack(signA, FP16_EXP_FULL, 0); // inf
    } else if (infB) {
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
    } else if (exp_diff < 0) {
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
    return _fp16_round_pack_normal_ex4(result_sign, exp_sum, sig_diff_ex, rm, expt);
}

Float16 _fp16_mul_mags(Float16 A, Float16 B, FloatRound rm, FexptT *expt) {
    FP16ExpT expA = _fp16_extract_exp(A);
    FP16FracT sigA = _fp16_extract_frac(A);
    FP16ExpT expB = _fp16_extract_exp(B);
    FP16FracT sigB = _fp16_extract_frac(B);

    FP16SignT signA = _fp16_extract_sign(A);
    FP16SignT signB = _fp16_extract_sign(B);

    if (fp16_is_nan(A) || fp16_is_nan(B)) {
        return _fp16_propagate_nan(A, B, expt);  // nan
    }

    FP16SignT result_sign = signA ^ signB;

    bool infA = (expA == FP16_EXP_FULL);
    bool infB = (expB == FP16_EXP_FULL);
    bool zeroA = fp16_is_zero(A);
    bool zeroB = fp16_is_zero(B);
    if ((infA && zeroB) || (zeroA && infB)) {
        RAISE_EXPT(INVALID);
        return FP16_CANONICAL_NAN;  // nan
    } else if (infA || infB) {
        return _fp16_pack(result_sign, FP16_EXP_FULL, 0);  // inf
    } else if (zeroA || zeroB) {
        return _fp16_pack(result_sign, 0, 0);  // zero
    }

    if (expA == ((1 << (FP16_EXP_BITS - 1)) - 1) && sigA == 0) {
        return _fp16_pack(result_sign, expB, sigB);
    } else if (expB == ((1 << (FP16_EXP_BITS - 1)) - 1) && sigB == 0) {
        return _fp16_pack(result_sign, expA, sigA);
    }

    FP16ExpT exp_product = expA + expB - ((1 << (FP16_EXP_BITS - 1)) - 1);
    FP16FracEx4T sigA_ex4 = (sigA + (expA ? (1 << (FP16_FRAC_BITS)) : sigA)) << 4;  // x2 when subnormal
    FP16FracEx4T sigB_ex4 = (sigB + (expB ? (1 << (FP16_FRAC_BITS)) : sigB)) << 4;

    uint32_t sig_product_ex8 = sigA_ex4 * sigB_ex4;  // 8 extra bits
    FP16FracEx4T sig_product_ex4;

    // dynamic normalize
    uint32_t highest_bitpos = count_highest_bitpos_1begin(sig_product_ex8);
    if (highest_bitpos > 2 * (FP16_FRAC_BITS + 5) - 1) {
        sig_product_ex4 = _fp16_shift_right_jam((FP16FracEx4T)sig_product_ex8, 1);
        exp_product += 1;
        highest_bitpos--;
    } else if (highest_bitpos == 2 * (FP16_FRAC_BITS + 5) - 1) {
        sig_product_ex4 = _fp16_shift_right_jam((FP16FracEx4T)sig_product_ex8, FP16_FRAC_BITS + 4);
    } else if (highest_bitpos < FP16_FRAC_BITS + 5) {
        uint32_t need_left_shift = (FP16_FRAC_BITS + 5) - highest_bitpos;
        sig_product_ex4 = (FP16FracEx4T)sig_product_ex8 << need_left_shift;
        exp_product -= (FP16_FRAC_BITS + 4 + need_left_shift);
    } else {
        uint32_t need_right_shift = highest_bitpos - (FP16_FRAC_BITS + 5);
        sig_product_ex4 = _fp16_shift_right_jam((FP16FracEx4T)sig_product_ex8, need_right_shift);
        exp_product -= (FP16_FRAC_BITS + 4 - need_right_shift);
    }

    // subnormal
    if (exp_product <= 0) {
        uint32_t shift_amount = 1 - exp_product;
        sig_product_ex4 = _fp16_shift_right_jam(sig_product_ex4, shift_amount);
        exp_product = 0;
        RAISE_EXPT(UNDERFLOW);
        if (sig_product_ex4 & 0xF) {
            RAISE_EXPT(INEXACT);
        }
        return _fp16_pack(result_sign, 0, sig_product_ex4 >> 4);
    }

    return _fp16_round_pack_normal_ex4(result_sign, exp_product, sig_product_ex4, rm, expt);
}

Float16 _fp16_div_mags(Float16 A, Float16 B, FloatRound rm, FexptT *expt) {
    FP16ExpT expA = _fp16_extract_exp(A);
    FP16FracT sigA = _fp16_extract_frac(A);
    FP16ExpT expB = _fp16_extract_exp(B);
    FP16FracT sigB = _fp16_extract_frac(B);

    FP16SignT signA = _fp16_extract_sign(A);
    FP16SignT signB = _fp16_extract_sign(B);

    if (fp16_is_nan(A) || fp16_is_nan(B)) {
        return _fp16_propagate_nan(A, B, expt);  // nan
    }

    FP16SignT result_sign = signA ^ signB;

    bool infA = (expA == FP16_EXP_FULL);
    bool infB = (expB == FP16_EXP_FULL);
    bool zeroA = fp16_is_zero(A);
    bool zeroB = fp16_is_zero(B);
    if ((infA && infB) || (zeroA && zeroB)) {
        RAISE_EXPT(INVALID);
        return FP16_CANONICAL_NAN;  // nan
    } else if (infA) {
        return _fp16_pack(result_sign, FP16_EXP_FULL, 0);  // inf
    } else if (zeroB) {
        RAISE_EXPT(DIVBYZERO);
        return _fp16_pack(result_sign, FP16_EXP_FULL, 0);  // inf
    } else if (zeroA || infB) {
        return _fp16_pack(result_sign, 0, 0);  // zero
    }

    if (expB == ((1 << (FP16_EXP_BITS - 1)) - 1) && sigB == 0) {
        return _fp16_pack(result_sign, expA, sigA);
    }

    FP16ExpT exp_div = expA - expB + ((1 << (FP16_EXP_BITS - 1)) - 1);
    FP16FracEx4T sigA_ex4 = (sigA + (expA ? (1 << (FP16_FRAC_BITS)) : sigA)) << (FP16_FRAC_BITS + 8);  // x2 when subnormal
    FP16FracEx4T sigB_ex4 = (sigB + (expB ? (1 << (FP16_FRAC_BITS)) : sigB)) << 4;

    FP16FracEx4T sig_div_ex4 = sigA_ex4 / sigB_ex4;
    FP16FracEx4T remainder = sigA_ex4 % sigB_ex4;
    if (remainder != 0) {
        sig_div_ex4 |= 1;  // jam bit
    }

    // dynamic normalize
    uint32_t highest_bitpos = count_highest_bitpos_1begin(sig_div_ex4);
    if (highest_bitpos >= (FP16_FRAC_BITS + 5)) {
        uint32_t need_right_shift = highest_bitpos - (FP16_FRAC_BITS + 5);
        sig_div_ex4 = _fp16_shift_right_jam(sig_div_ex4, need_right_shift);
        exp_div += need_right_shift;
    } else {
        uint32_t need_left_shift = (FP16_FRAC_BITS + 5) - highest_bitpos;
        sig_div_ex4 = sig_div_ex4 << need_left_shift;
        exp_div -= need_left_shift;
    }

    // subnormal
    if (exp_div <= 0) {
        uint32_t shift_amount = 1 - exp_div;
        sig_div_ex4 = _fp16_shift_right_jam(sig_div_ex4, shift_amount);
        exp_div = 0;
        RAISE_EXPT(UNDERFLOW);
        if (sig_div_ex4 & 0xF) {
            RAISE_EXPT(INEXACT);
        }
        return _fp16_pack(result_sign, 0, sig_div_ex4 >> 4);
    }

    return _fp16_round_pack_normal_ex4(result_sign, exp_div, sig_div_ex4, rm, expt);
}

Float16 _fp16_sqrt_mags(Float16 A, FloatRound rm, FexptT *expt) {
    FP16ExpT exp = _fp16_extract_exp(A);
    FP16FracT sig = _fp16_extract_frac(A);

    FP16SignT sign = _fp16_extract_sign(A);

    if (fp16_is_nan(A)) {
        if (fp16_is_snan(A)) {
            RAISE_EXPT(INVALID);
        }
        return FP16_CANONICAL_NAN;  // nan
    } else if (fp16_is_zero(A)) {
        return A;  // zero
    } else if (sign) {
        RAISE_EXPT(INVALID);
        return FP16_CANONICAL_NAN;  // nan
    } else if (exp == FP16_EXP_FULL) {
        return A;  // +inf
    }

    FP16FracEx4T sig_ex4;

    if (exp == 0 && sig != 0) { // subnormal
        // normalize
        uint32_t highest_bitpos = count_highest_bitpos_1begin(sig);
        uint32_t need_left_shift = FP16_FRAC_BITS + 1 - highest_bitpos;
        sig = sig << need_left_shift;
        exp = exp - (int32_t)need_left_shift + 1;
        sig_ex4 = sig << 4;
    } else {
        sig_ex4 = (sig + (1 << FP16_FRAC_BITS)) << 4;
    }

    FP16ExpT exp_sqrt = exp - ((1 << (FP16_EXP_BITS - 1)) - 1);

    uint32_t highest_bitpos = count_highest_bitpos_1begin(sig_ex4);
    uint32_t need_left_shift = 2 * (FP16_FRAC_BITS + 5) - 1 - highest_bitpos;

    if (exp_sqrt & 1) {
        sig_ex4 = sig_ex4 << 1;
        exp_sqrt -= 1;
    }
    exp_sqrt = (exp_sqrt >> 1) + ((1 << (FP16_EXP_BITS - 1)) - 1);

    uint32_t rem = (sig_ex4 << need_left_shift);
    uint32_t result = 0;
    for (int32_t i = FP16_FRAC_BITS + 4; i >= 0; i--) {
        uint32_t bit = 1 << i;
        uint32_t trial = (result << 1) | bit;  // (2 x result + bit)

        if (rem >= (trial << i)) {  // x bit
            rem -= (trial << i);
            result |= bit;
        }
    }

    if (rem != 0) {
        result |= 1;  // jam bit
    }

    // subnormal
    if (exp_sqrt <= 0) {
        uint32_t shift_amount = 1 - exp_sqrt;
        result = _fp16_shift_right_jam(result, shift_amount);
        exp_sqrt = 0;
        RAISE_EXPT(UNDERFLOW);
        if (result & 0xF) {
            RAISE_EXPT(INEXACT);
        }
        return _fp16_pack(0, 0, result >> 4);
    }

    return _fp16_round_pack_normal_ex4(0, exp_sqrt, result, rm, expt);
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

Float16 fp16_mul(Float16 A, Float16 B, FloatRound rm, FexptT *expt) {
    return _fp16_mul_mags(A, B, rm, expt);
}

Float16 fp16_div(Float16 A, Float16 B, FloatRound rm, FexptT *expt) {
    return _fp16_div_mags(A, B, rm, expt);
}

Float16 fp16_sqrt(Float16 A, FloatRound rm, FexptT *expt) {
    return _fp16_sqrt_mags(A, rm, expt);
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

bool test_fp16_mul_case(uint32_t idx, Float16 A, Float16 B, FloatRound rm, Float16 expected_res, uint32_t expected_expt) {
    FexptT expt = 0;
    Float16 res = fp16_mul(A, B, rm, &expt);
    if (res != expected_res) {
        printf("FP16_MUL test failed [%d]: A=%s, B=%s, expected %s, got %s\n", idx, test_fp16_to_binary_str(A).c_str(), test_fp16_to_binary_str(B).c_str(), test_fp16_to_binary_str(expected_res).c_str(), test_fp16_to_binary_str(res).c_str());
        return false;
    }
    if ((uint32_t)expt != expected_expt) {
        printf("FP16_MUL test failed [%d]: expected expt 0x%x, got 0x%x\n", idx, expected_expt, (uint32_t)expt);
        return false;
    }
    return true;
}

bool test_fp16_mul() {
    if(!test_fp16_mul_case(0, 0x3c00, 0x3c00, FloatRound::RNE, 0x3c00, 0)) return false;
    if(!test_fp16_mul_case(1, 0x4000, 0x3c00, FloatRound::RNE, 0x4000, 0)) return false;
    if(!test_fp16_mul_case(2, 0x4000, 0x4000, FloatRound::RNE, 0x4400, 0)) return false;
    if(!test_fp16_mul_case(3, 0x3800, 0x4000, FloatRound::RNE, 0x3c00, 0)) return false;
    if(!test_fp16_mul_case(4, 0x3c00 | 0x8000, 0x3c00, FloatRound::RNE, 0x3c00 | 0x8000, 0)) return false;
    if(!test_fp16_mul_case(5, 0x3c00 | 0x8000, 0x3c00 | 0x8000, FloatRound::RNE, 0x3c00, 0)) return false;
    if(!test_fp16_mul_case(6, 0x0000, 0x3c00, FloatRound::RNE, 0x0000, 0)) return false;
    if(!test_fp16_mul_case(7, 0x8000, 0x3c00, FloatRound::RNE, 0x8000, 0)) return false;
    if(!test_fp16_mul_case(8, 0x0000, 0x3c00 | 0x8000, FloatRound::RNE, 0x8000, 0)) return false;
    if(!test_fp16_mul_case(9, 0x7c00, 0x3c00, FloatRound::RNE, 0x7c00, 0)) return false;
    if(!test_fp16_mul_case(10, 0x7c00, 0x3c00 | 0x8000, FloatRound::RNE, 0x7c00 | 0x8000, 0)) return false;
    if(!test_fp16_mul_case(11, 0x7c00 | 0x8000, 0x7c00 | 0x8000, FloatRound::RNE, 0x7c00, 0)) return false;
    if(!test_fp16_mul_case(12, 0x7c00, 0x0000, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_mul_case(13, 0x0000, 0x7c00, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_mul_case(14, FP16_CANONICAL_NAN, 0x3c00, FloatRound::RNE, FP16_CANONICAL_NAN, 0)) return false;
    if(!test_fp16_mul_case(15, 0x3c00, FP16_CANONICAL_NAN, FloatRound::RNE, FP16_CANONICAL_NAN, 0)) return false;
    if(!test_fp16_mul_case(16, 0x0001, 0x3c00, FloatRound::RNE, 0x0001, 0)) return false;
    if(!test_fp16_mul_case(17, 0x0001, 0x0001, FloatRound::RNE, 0x0000, (uint32_t)FloatException::UNDERFLOW | (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_mul_case(18, 0x7bff, 0x4000, FloatRound::RNE, 0x7c00, (uint32_t)FloatException::OVERFLOW | (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_mul_case(19, 0x3555, 0x3555, FloatRound::RNE, 0x2f1c, (uint32_t)FloatException::INEXACT)) return false;

    printf("Test FP16 Multiply Passed!\n");
    return true;
}

bool test_fp16_div_case(uint32_t idx, Float16 A, Float16 B, FloatRound rm, Float16 expected_res, uint32_t expected_expt) {
    FexptT expt = 0;
    Float16 res = fp16_div(A, B, rm, &expt);
    if (res != expected_res) {
        printf("FP16_DIV test failed [%d]: A=%s, B=%s, expected %s, got %s\n", idx, test_fp16_to_binary_str(A).c_str(), test_fp16_to_binary_str(B).c_str(), test_fp16_to_binary_str(expected_res).c_str(), test_fp16_to_binary_str(res).c_str());
        return false;
    }
    if ((uint32_t)expt != expected_expt) {
        printf("FP16_DIV test failed [%d]: expected expt 0x%x, got 0x%x\n", idx, expected_expt, (uint32_t)expt);
        return false;
    }
    return true;
}

bool test_fp16_div() {
    if(!test_fp16_div_case(0, 0x3c00, 0x3c00, FloatRound::RNE, 0x3c00, 0)) return false;
    if(!test_fp16_div_case(1, 0x4000, 0x3c00, FloatRound::RNE, 0x4000, 0)) return false;
    if(!test_fp16_div_case(2, 0x3c00, 0x4000, FloatRound::RNE, 0x3800, 0)) return false;
    if(!test_fp16_div_case(3, 0x4400, 0x4000, FloatRound::RNE, 0x4000, 0)) return false;
    if(!test_fp16_div_case(4, 0x3c00 | 0x8000, 0x3c00, FloatRound::RNE, 0x3c00 | 0x8000, 0)) return false;
    if(!test_fp16_div_case(5, 0x3c00 | 0x8000, 0x3c00 | 0x8000, FloatRound::RNE, 0x3c00, 0)) return false;
    if(!test_fp16_div_case(6, 0x3c00, 0x3c00 | 0x8000, FloatRound::RNE, 0x3c00 | 0x8000, 0)) return false;
    if(!test_fp16_div_case(7, 0x3c00, 0x0000, FloatRound::RNE, 0x7c00, (uint32_t)FloatException::DIVBYZERO)) return false;
    if(!test_fp16_div_case(8, 0x3c00, 0x8000, FloatRound::RNE, 0x7c00 | 0x8000, (uint32_t)FloatException::DIVBYZERO)) return false;
    if(!test_fp16_div_case(9, 0x3c00 | 0x8000, 0x0000, FloatRound::RNE, 0x7c00 | 0x8000, (uint32_t)FloatException::DIVBYZERO)) return false;
    if(!test_fp16_div_case(10, 0x0000, 0x0000, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_div_case(11, 0x8000, 0x0000, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_div_case(12, 0x7c00, 0x7c00, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_div_case(13, 0x7c00 | 0x8000, 0x7c00, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_div_case(14, 0x7c00, 0x3c00, FloatRound::RNE, 0x7c00, 0)) return false;
    if(!test_fp16_div_case(15, 0x7c00, 0x3c00 | 0x8000, FloatRound::RNE, 0x7c00 | 0x8000, 0)) return false;
    if(!test_fp16_div_case(16, 0x3c00, 0x7c00, FloatRound::RNE, 0x0000, 0)) return false;
    if(!test_fp16_div_case(17, 0x3c00, 0x7c00 | 0x8000, FloatRound::RNE, 0x8000, 0)) return false;
    if(!test_fp16_div_case(18, 0x0000, 0x3c00, FloatRound::RNE, 0x0000, 0)) return false;
    if(!test_fp16_div_case(19, 0x8000, 0x3c00, FloatRound::RNE, 0x8000, 0)) return false;
    if(!test_fp16_div_case(20, FP16_CANONICAL_NAN, 0x3c00, FloatRound::RNE, FP16_CANONICAL_NAN, 0)) return false;
    if(!test_fp16_div_case(21, 0x3c00, FP16_CANONICAL_NAN, FloatRound::RNE, FP16_CANONICAL_NAN, 0)) return false;
    if(!test_fp16_div_case(22, 0x0001, 0x3c00, FloatRound::RNE, 0x0001, 0)) return false;
    if(!test_fp16_div_case(23, 0x0001, 0x4000, FloatRound::RNE, 0x0000, (uint32_t)FloatException::UNDERFLOW | (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_div_case(24, 0x3c00, 0x4200, FloatRound::RNE, 0x3555, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_div_case(25, 0x3c00, 0x4200, FloatRound::RDN, 0x3555, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_div_case(26, 0x3c00, 0x4200, FloatRound::RUP, 0x3556, (uint32_t)FloatException::INEXACT)) return false;

    printf("Test FP16 Divide Passed!\n");
    return true;
}

bool test_fp16_sqrt_case(uint32_t idx, Float16 A, FloatRound rm, Float16 expected_res, uint32_t expected_expt) {
    FexptT expt = 0;
    Float16 res = fp16_sqrt(A, rm, &expt);
    if (res != expected_res) {
        printf("FP16_SQRT test failed [%d]: A=%s, expected %s, got %s\n", idx, test_fp16_to_binary_str(A).c_str(), test_fp16_to_binary_str(expected_res).c_str(), test_fp16_to_binary_str(res).c_str());
        return false;
    }
    if ((uint32_t)expt != expected_expt) {
        printf("FP16_SQRT test failed [%d]: expected expt 0x%x, got 0x%x\n", idx, expected_expt, (uint32_t)expt);
        return false;
    }
    return true;
}

bool test_fp16_sqrt() {
    if(!test_fp16_sqrt_case(0, 0x3c00, FloatRound::RNE, 0x3c00, 0)) return false;
    if(!test_fp16_sqrt_case(1, 0x4400, FloatRound::RNE, 0x4000, 0)) return false;
    if(!test_fp16_sqrt_case(2, 0x4900, FloatRound::RNE, 0x4253, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(3, 0x4e00, FloatRound::RNE, 0x44e6, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(4, 0x4000, FloatRound::RNE, 0x3da8, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(5, 0x4200, FloatRound::RNE, 0x3eee, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(6, 0x3800, FloatRound::RNE, 0x39a8, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(7, 0x3400, FloatRound::RNE, 0x3800, 0)) return false;
    if(!test_fp16_sqrt_case(8, 0x0000, FloatRound::RNE, 0x0000, 0)) return false;
    if(!test_fp16_sqrt_case(9, 0x8000, FloatRound::RNE, 0x8000, 0)) return false;
    if(!test_fp16_sqrt_case(10, 0x3c00 | 0x8000, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_sqrt_case(11, 0x4000 | 0x8000, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_sqrt_case(12, 0x7c00 | 0x8000, FloatRound::RNE, FP16_CANONICAL_NAN, (uint32_t)FloatException::INVALID)) return false;
    if(!test_fp16_sqrt_case(13, 0x7c00, FloatRound::RNE, 0x7c00, 0)) return false;
    if(!test_fp16_sqrt_case(14, FP16_CANONICAL_NAN, FloatRound::RNE, FP16_CANONICAL_NAN, 0)) return false;
    if(!test_fp16_sqrt_case(15, 0x0001, FloatRound::RNE, 0x0c00, 0)) return false;
    if(!test_fp16_sqrt_case(16, 0x03ff, FloatRound::RNE, 0x1fff, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(17, 0x7bff, FloatRound::RNE, 0x5bff, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(18, 0x4000, FloatRound::RDN, 0x3da8, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(19, 0x4000, FloatRound::RUP, 0x3da9, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(20, 0x3bff, FloatRound::RNE, 0x3bff, (uint32_t)FloatException::INEXACT)) return false;
    if(!test_fp16_sqrt_case(21, 0x3c01, FloatRound::RNE, 0x3c00, (uint32_t)FloatException::INEXACT)) return false;

    printf("Test FP16 Sqrt Passed!\n");
    return true;
}

bool test_fp16() {
    if (!test_fp16_mul())
        return false;
    if (!test_fp16_div())
        return false;
    if (!test_fp16_sqrt())
        return false;
    printf("Test FP16 All Passed !!!\n");
    return true;
}

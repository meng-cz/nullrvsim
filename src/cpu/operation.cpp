
#include <assert.h>
#include <fenv.h>

#include "common.h"
#include "operation.h"

#define GENERATOR_MULH_FUNCTION(len, stype, utype, dstype, dutype) \
inline utype tmp_mulh(utype s1, utype s2) { return (((dutype)s1) * (dutype)s2) >> len; } \
inline stype tmp_mulh(utype s1, stype s2) { return (((dutype)s1) * (dstype)s2) >> len; } \
inline stype tmp_mulh(stype s1, utype s2) { return (((dstype)s1) * (dutype)s2) >> len; } \
inline stype tmp_mulh(stype s1, stype s2) { return (((dstype)s1) * (dstype)s2) >> len; }

GENERATOR_MULH_FUNCTION(32, int32_t, uint32_t, int64_t, uint64_t);
GENERATOR_MULH_FUNCTION(64, int64_t, uint64_t, __int128_t, __uint128_t);

#undef GENERATOR_MULH_FUNCTION

namespace isa {

SimError perform_branch_op_64(RV64BranchOP3 op, IntDataT *out, IntDataT s1, IntDataT s2) {
    assert(out);
    bool res = false;
    switch (op) {
    case RV64BranchOP3::BEQ : res = (RAW_DATA_AS(s1).u64 == RAW_DATA_AS(s2).u64); break;
    case RV64BranchOP3::BNE : res = (RAW_DATA_AS(s1).u64 != RAW_DATA_AS(s2).u64); break;
    case RV64BranchOP3::BLT : res = (RAW_DATA_AS(s1).i64 < RAW_DATA_AS(s2).i64); break;
    case RV64BranchOP3::BGE : res = (RAW_DATA_AS(s1).i64 >= RAW_DATA_AS(s2).i64); break;
    case RV64BranchOP3::BLTU: res = (RAW_DATA_AS(s1).u64 < RAW_DATA_AS(s2).u64); break;
    case RV64BranchOP3::BGEU: res = (RAW_DATA_AS(s1).u64 >= RAW_DATA_AS(s2).u64); break;
    default: return SimError::unsupported;
    }
    *out = (res?1:0);
    return SimError::success;
}

SimError perform_int_op_64(RV64IntOP73 op, IntDataT *out, IntDataT s1, IntDataT s2) {
    assert(out);
    IntDataT ret;
    switch (op)
    {
    case RV64IntOP73::ADD : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) + (RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::SUB : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) - (RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::SLL : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) << (RAW_DATA_AS(s2).u64 & 0x3f); break;
    case RV64IntOP73::SLT : RAW_DATA_AS(ret).i64 = (((RAW_DATA_AS(s1).i64) < (RAW_DATA_AS(s2).i64))?1:0); break;
    case RV64IntOP73::SLTU: RAW_DATA_AS(ret).i64 = (((RAW_DATA_AS(s1).u64) < (RAW_DATA_AS(s2).u64))?1:0); break;
    case RV64IntOP73::XOR : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) ^ (RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::SRL : RAW_DATA_AS(ret).u64 = (RAW_DATA_AS(s1).u64) >> (RAW_DATA_AS(s2).u64 & 0x3f); break;
    case RV64IntOP73::SRA : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) >> (RAW_DATA_AS(s2).u64 & 0x3f); break;
    case RV64IntOP73::OR  : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) | (RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::AND : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) & (RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::MUL : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) * (RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::MULH: RAW_DATA_AS(ret).i64 = tmp_mulh(RAW_DATA_AS(s1).i64, RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::MULHSU: RAW_DATA_AS(ret).i64 = tmp_mulh(RAW_DATA_AS(s1).i64, RAW_DATA_AS(s2).u64); break;
    case RV64IntOP73::MULHU: RAW_DATA_AS(ret).i64 = tmp_mulh(RAW_DATA_AS(s1).u64, RAW_DATA_AS(s2).u64); break;
    case RV64IntOP73::DIV : if(s2==0) return SimError::devidebyzero; RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) / (RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::DIVU: if(s2==0) return SimError::devidebyzero; RAW_DATA_AS(ret).u64 = (RAW_DATA_AS(s1).u64) / (RAW_DATA_AS(s2).u64); break;
    case RV64IntOP73::REM : if(s2==0) return SimError::devidebyzero; RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i64) % (RAW_DATA_AS(s2).i64); break;
    case RV64IntOP73::REMU: if(s2==0) return SimError::devidebyzero; RAW_DATA_AS(ret).u64 = (RAW_DATA_AS(s1).u64) % (RAW_DATA_AS(s2).u64); break;
    default: return SimError::unsupported;
    }
    *out = ret;
    return SimError::success;
}

SimError perform_int_op_32(RV64IntOP73 op, IntDataT *out, IntDataT s1, IntDataT s2) {
    assert(out);
    IntDataT ret;
    switch (op)
    {
    case RV64IntOP73::ADD : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i32) + (RAW_DATA_AS(s2).i32); break;
    case RV64IntOP73::SUB : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i32) - (RAW_DATA_AS(s2).i32); break;
    case RV64IntOP73::SLL : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i32) << (RAW_DATA_AS(s2).u32 & 0x1f); break;
    case RV64IntOP73::SRL : RAW_DATA_AS(ret).u64 = (RAW_DATA_AS(s1).u32) >> (RAW_DATA_AS(s2).u32 & 0x1f); if(RAW_DATA_AS(ret).u64 >> 31) RAW_DATA_AS(ret).u64 |= 0xffffffff00000000UL; break;
    case RV64IntOP73::SRA : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i32) >> (RAW_DATA_AS(s2).u32 & 0x1f); break;
    case RV64IntOP73::MUL : RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i32) * (RAW_DATA_AS(s2).i32); break;
    case RV64IntOP73::DIV : if(s2==0) return SimError::devidebyzero; RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i32) / (RAW_DATA_AS(s2).i32); break;
    case RV64IntOP73::DIVU: if(s2==0) return SimError::devidebyzero; RAW_DATA_AS(ret).u64 = (RAW_DATA_AS(s1).u32) / (RAW_DATA_AS(s2).u32); break;
    case RV64IntOP73::REM : if(s2==0) return SimError::devidebyzero; RAW_DATA_AS(ret).i64 = (RAW_DATA_AS(s1).i32) % (RAW_DATA_AS(s2).i32); break;
    case RV64IntOP73::REMU: if(s2==0) return SimError::devidebyzero; RAW_DATA_AS(ret).u64 = (RAW_DATA_AS(s1).u32) % (RAW_DATA_AS(s2).u32); break;
    default: return SimError::unsupported;
    }
    *out = ret;
    return SimError::success;
}

inline bool _supported_fp_width(RV64FPWidth2 wid) {
    return (wid == RV64FPWidth2::fdword || wid == RV64FPWidth2::fword);
}

SimError _fp_mv_f2f(RV64FPWidth2 dst_wid, RV64FPWidth2 src_wid, FPDataT *out, FPDataT s) {
    if(!_supported_fp_width(src_wid) || !_supported_fp_width(dst_wid)) return SimError::unsupported;
    double value = 0;
    RawDataT ret;
    switch (src_wid)
    {
    case RV64FPWidth2::fdword: value = RAW_DATA_AS(s).f64; break;
    case RV64FPWidth2::fword : value = RAW_DATA_AS(s).f32; break;
    default: return SimError::unsupported;
    }
    switch (dst_wid)
    {
    case RV64FPWidth2::fdword: RAW_DATA_AS(ret).f64 = value; break;
    case RV64FPWidth2::fword : RAW_DATA_AS(ret).f32 = value; break;
    default: return SimError::unsupported;
    }
    *out = ret;
    return SimError::success;
}

SimError _fp_cmp_32(RV64FPCMP3 cmp, IntDataT *out, float v1, float v2) {
    bool res = false;
    switch (cmp)
    {
    case RV64FPCMP3::FEQ: res = (v1 == v2); break;
    case RV64FPCMP3::FLT: res = (v1 < v2); break;
    case RV64FPCMP3::FLE: res = (v1 <= v2); break;
    default: return SimError::unsupported;
    }
    *out = (res?1UL:0UL);
    return SimError::success;
}

SimError _fp_cmp_64(RV64FPCMP3 cmp, IntDataT *out, double v1, double v2) {
    bool res = false;
    switch (cmp)
    {
    case RV64FPCMP3::FEQ: res = (v1 == v2); break;
    case RV64FPCMP3::FLT: res = (v1 < v2); break;
    case RV64FPCMP3::FLE: res = (v1 <= v2); break;
    default: return SimError::unsupported;
    }
    *out = (res?1UL:0UL);
    return SimError::success;
}

SimError _fp_cvt_f2i(RV64FPWidth2 fwid, RV64FPCVTWidth5 iwid, RV64FPRM3 rm, IntDataT *out, FPDataT s) {
    if(!_supported_fp_width(fwid)) return SimError::unsupported;
    double v = 0;
    IntDataT ret;
    switch (fwid)
    {
    case RV64FPWidth2::fdword: v = RAW_DATA_AS(s).f64; break;
    case RV64FPWidth2::fword : v = RAW_DATA_AS(s).f32; break;
    default: return SimError::unsupported;
    }
    switch (rm)
    {
    case RV64FPRM3::RNE : v += 0.5; break;
    case RV64FPRM3::RTZ : v = ((v>0)?floor(v):ceil(v)); break;
    case RV64FPRM3::RDN : v = floor(v); break;
    case RV64FPRM3::RUP : v = ceil(v); break;
    default: return SimError::unsupported;
    }
    if(iwid == RV64FPCVTWidth5::word) {
        if(v < -pow(2., 31.)) RAW_DATA_AS(ret).u64 = 0xffffffff80000000UL;
        else if(v > pow(2., 31.)-1.) RAW_DATA_AS(ret).u64 = 0x000000007fffffffUL;
        else RAW_DATA_AS(ret).i64 = (int32_t)v;
    }
    else if(iwid == RV64FPCVTWidth5::uword) {
        if(v < 0) RAW_DATA_AS(ret).u64 = 0x0000000000000000UL;
        else if(v > pow(2., 32.)-1.) RAW_DATA_AS(ret).u64 = 0x00000000ffffffffUL;
        else RAW_DATA_AS(ret).u64 = (uint32_t)v;
    }
    else if(iwid == RV64FPCVTWidth5::dword) {
        if(v < -pow(2., 63.)) RAW_DATA_AS(ret).u64 = 0x8000000000000000UL;
        else if(v > pow(2., 63.)-1.) RAW_DATA_AS(ret).u64 = 0x7fffffffffffffffUL;
        else RAW_DATA_AS(ret).i64 = (int64_t)v;
    }
    else if(iwid == RV64FPCVTWidth5::udword) {
        if(v < 0) RAW_DATA_AS(ret).u64 = 0x0000000000000000UL;
        else if(v > pow(2., 64.)-1.) RAW_DATA_AS(ret).u64 = 0xffffffffffffffffUL;
        else RAW_DATA_AS(ret).u64 = (uint64_t)v;
    }
    else {
        return SimError::unsupported;
    }
    *out = ret;
    return SimError::success;
}

SimError _fp_cvt_i2f(RV64FPWidth2 fwid, RV64FPCVTWidth5 iwid, RV64FPRM3 rm, FPDataT *out, IntDataT s) {
    if(!_supported_fp_width(fwid)) return SimError::unsupported;
    FPDataT ret;
    double v;
    switch (iwid)
    {
    case RV64FPCVTWidth5::word  : v = RAW_DATA_AS(s).i32; break;
    case RV64FPCVTWidth5::uword : v = RAW_DATA_AS(s).u32; break;
    case RV64FPCVTWidth5::dword : v = RAW_DATA_AS(s).i64; break;
    case RV64FPCVTWidth5::udword: v = RAW_DATA_AS(s).u64; break;
    default: return SimError::unsupported;
    }
    switch (fwid)
    {
    case RV64FPWidth2::fdword: RAW_DATA_AS(ret).f64 = v; break;
    case RV64FPWidth2::fword : RAW_DATA_AS(ret).f32 = v; break;
    default: return SimError::unsupported;
    }
    *out = ret;
    return SimError::success;
}

inline uint64_t _fclass_op_32(float v) {
    uint32_t bit = *((uint32_t*)(&v));
    uint64_t ret = 0;
    if(bit == 0xff800000U) {
        ret |= (1<<0);
    }
    else if(bit == 0x80000000U) {
        ret |= (1<<3);
    }
    else if(bit == 0U) {
        ret |= (1<<4);
    }
    else if(bit == 0x7f800000U) {
        ret |= (1<<7);
    }
    else if((bit & 0x7fc00000U) == 0x7fc00000) {
        ret |= (1<<9);
    }
    else if((bit & 0x7fc00000U) == 0x7f800000) {
        ret |= (1<<8);
    }
    else if(isnormal(v)) {
        if(v > 0) ret |= (1<<6);
        else ret |= (1<<1);
    }
    else {
        if(v > 0) ret |= (1<<5);
        else ret |= (1<<2);
    }
    return ret;
}

inline uint64_t _fclass_op_64(double v) {
    uint64_t bit = *((uint64_t*)(&v));
    uint64_t ret = 0;
    if(bit == 0xfff0000000000000UL) {
        ret |= (1<<0);
    }
    else if(bit == 0x8000000000000000UL) {
        ret |= (1<<3);
    }
    else if(bit == 0UL) {
        ret |= (1<<4);
    }
    else if(bit == 0x7ff0000000000000UL) {
        ret |= (1<<7);
    }
    else if((bit & 0x7ff8000000000000UL) == 0x7ff8000000000000UL) {
        ret |= (1<<9);
    }
    else if((bit & 0x7ff8000000000000UL) == 0x7ff0000000000000UL) {
        ret |= (1<<8);
    }
    else if(isnormal(v)) {
        if(v > 0) ret |= (1<<6);
        else ret |= (1<<1);
    }
    else {
        if(v > 0) ret |= (1<<5);
        else ret |= (1<<2);
    }
    return ret;
}


SimError _fp_mv_f2i(RV64FPWidth2 fwid, RV64FPMVI3 mv, IntDataT *out, FPDataT s) {
    if(!_supported_fp_width(fwid)) return SimError::unsupported;
    IntDataT ret;
    if(mv == RV64FPMVI3::RAW) {
        switch (fwid)
        {
        case RV64FPWidth2::fdword: RAW_DATA_AS(ret).u64 = RAW_DATA_AS(s).u64; break;
        case RV64FPWidth2::fword : RAW_DATA_AS(ret).u64 = RAW_DATA_AS(s).u32; break;
        default: return SimError::unsupported;
        }
    }
    else if(mv == RV64FPMVI3::FCLASS) {
        switch (fwid)
        {
        case RV64FPWidth2::fdword: RAW_DATA_AS(ret).u64 = _fclass_op_64(RAW_DATA_AS(s).f64); break;
        case RV64FPWidth2::fword : RAW_DATA_AS(ret).u64 = _fclass_op_32(RAW_DATA_AS(s).f32); break;
        default: return SimError::unsupported;
        }
    }
    else {
        return SimError::unsupported;
    }
    *out = ret;
    return SimError::success;
}

template<typename FT>
inline SimError _fp_min(RV64FPMIN3 cfg, FT *out, FT v1, FT v2) {
    if(isnan(v1)) {
        *out = v2;
        return SimError::success;
    }
    if(isnan(v2)) {
        *out = v1;
        return SimError::success;
    }
    switch(cfg) {
        case RV64FPMIN3::MIN : *out = std::min(v1, v2); break;
        case RV64FPMIN3::MAX : *out = std::max(v1, v2); break;
        default: return SimError::unsupported;
    }
    return SimError::success;
}

inline SimError _fp_sgnj_32(RV64FPSGNJ3 cfg, float *out, float v1, float v2) {
    uint32_t s1 = ((*((uint32_t*)(&v1))) >> 31U);
    uint32_t s2 = ((*((uint32_t*)(&v2))) >> 31U);
    switch (cfg)
    {
    case RV64FPSGNJ3::SGNJ : *out = ((s1 == s2)?(v1):(-v1)); break;
    case RV64FPSGNJ3::SGNJN: *out = ((s1 != s2)?(v1):(-v1)); break;
    case RV64FPSGNJ3::SGNJX: *out = ((s1 == s2)?(std::abs(v1)):(-std::abs(v1))); break;
    default: return SimError::unsupported;
    }
    return SimError::success;
}
inline SimError _fp_sgnj_64(RV64FPSGNJ3 cfg, double *out, double v1, double v2) {
    uint64_t s1 = ((*((uint64_t*)(&v1))) >> 63U);
    uint64_t s2 = ((*((uint64_t*)(&v2))) >> 63U);
    switch (cfg)
    {
    case RV64FPSGNJ3::SGNJ : *out = ((s1 == s2)?(v1):(-v1)); break;
    case RV64FPSGNJ3::SGNJN: *out = ((s1 != s2)?(v1):(-v1)); break;
    case RV64FPSGNJ3::SGNJX: *out = ((s1 == s2)?(std::abs(v1)):(-std::abs(v1))); break;
    default: return SimError::unsupported;
    }
    return SimError::success;
}

SimError perform_fp_op(RV64FPParam param, RawDataT *out, RawDataT s1, RawDataT s2, uint64_t *p_fcsr) {
    if(!_supported_fp_width(param.fwid)) return SimError::unsupported;
    assert(out);
    RawDataT ret = 0;
    SimError err = SimError::success;
    feclearexcept(FE_ALL_EXCEPT);
    if(param.fwid == RV64FPWidth2::fword) {
        switch (param.op)
        {
        case RV64FPOP5::ADD  : RAW_DATA_AS(ret).f32 = RAW_DATA_AS(s1).f32 + RAW_DATA_AS(s2).f32; break;
        case RV64FPOP5::SUB  : RAW_DATA_AS(ret).f32 = RAW_DATA_AS(s1).f32 - RAW_DATA_AS(s2).f32; break;
        case RV64FPOP5::MUL  : RAW_DATA_AS(ret).f32 = RAW_DATA_AS(s1).f32 * RAW_DATA_AS(s2).f32; break;
        case RV64FPOP5::DIV  : RAW_DATA_AS(ret).f32 = RAW_DATA_AS(s1).f32 / RAW_DATA_AS(s2).f32; break;
        case RV64FPOP5::SQRT :
            if(RAW_DATA_AS(s1).f32 < 0) {
                RAW_DATA_AS(ret).u64 = 0x7FC00000UL;
                *p_fcsr |= RV_FFLAGS_INVALID;
            }
            else {
                RAW_DATA_AS(ret).f32 = sqrt(RAW_DATA_AS(s1).f32);
            } break;
        case RV64FPOP5::SGNJ : err = _fp_sgnj_32(param.funct3.sgnj, (float*)(&ret), RAW_DATA_AS(s1).f32, RAW_DATA_AS(s2).f32); break;
        case RV64FPOP5::MIN  : err = _fp_min<float>(param.funct3.minmax, (float*)(&ret), RAW_DATA_AS(s1).f32, RAW_DATA_AS(s2).f32); break;
        case RV64FPOP5::MVF2F: err = _fp_mv_f2f(param.fwid, (RV64FPWidth2)(param.iwid), &ret, s1); break;
        case RV64FPOP5::CVTF2I: err = _fp_cvt_f2i(param.fwid, param.iwid, ((param.funct3.rm == RV64FPRM3::DYN)?((RV64FPRM3)(((*p_fcsr)>>5)&7)):(param.funct3.rm)), &ret, s1); break;
        case RV64FPOP5::CVTI2F: err = _fp_cvt_i2f(param.fwid, param.iwid, ((param.funct3.rm == RV64FPRM3::DYN)?((RV64FPRM3)(((*p_fcsr)>>5)&7)):(param.funct3.rm)), &ret, s1); break;
        case RV64FPOP5::MVF2I : err = _fp_mv_f2i(param.fwid, param.funct3.mv, &ret, s1); break;
        case RV64FPOP5::MVI2F : RAW_DATA_AS(ret).u64 = (RAW_DATA_AS(s1).u64 & 0xffffffffUL); break;
        case RV64FPOP5::CMP   : err = _fp_cmp_32(param.funct3.cmp, &ret, RAW_DATA_AS(s1).f32, RAW_DATA_AS(s2).f32); break;
        default:
            break;
        }
    }
    else if(param.fwid == RV64FPWidth2::fdword) {
        switch (param.op)
        {
        case RV64FPOP5::ADD  : RAW_DATA_AS(ret).f64 = RAW_DATA_AS(s1).f64 + RAW_DATA_AS(s2).f64; break;
        case RV64FPOP5::SUB  : RAW_DATA_AS(ret).f64 = RAW_DATA_AS(s1).f64 - RAW_DATA_AS(s2).f64; break;
        case RV64FPOP5::MUL  : RAW_DATA_AS(ret).f64 = RAW_DATA_AS(s1).f64 * RAW_DATA_AS(s2).f64; break;
        case RV64FPOP5::DIV  : RAW_DATA_AS(ret).f64 = RAW_DATA_AS(s1).f64 / RAW_DATA_AS(s2).f64; break;
        case RV64FPOP5::SQRT : 
            if(RAW_DATA_AS(s1).f64 < 0) {
                RAW_DATA_AS(ret).u64 = 0x7ff8000000000000UL;
                *p_fcsr |= RV_FFLAGS_INVALID;
            }
            else {
                RAW_DATA_AS(ret).f64 = sqrt(RAW_DATA_AS(s1).f64);
            } break;
        case RV64FPOP5::SGNJ : err = _fp_sgnj_64(param.funct3.sgnj, (double*)(&ret), RAW_DATA_AS(s1).f64, RAW_DATA_AS(s2).f64); break;
        case RV64FPOP5::MIN  : err = _fp_min<double>(param.funct3.minmax, (double*)(&ret), RAW_DATA_AS(s1).f64, RAW_DATA_AS(s2).f64); break;
        case RV64FPOP5::MVF2F: err = _fp_mv_f2f(param.fwid, (RV64FPWidth2)(param.iwid), &ret, s1); break;
        case RV64FPOP5::CVTF2I: err = _fp_cvt_f2i(param.fwid, param.iwid, ((param.funct3.rm == RV64FPRM3::DYN)?((RV64FPRM3)(((*p_fcsr)>>5)&7)):(param.funct3.rm)), &ret, s1); break;
        case RV64FPOP5::CVTI2F: err = _fp_cvt_i2f(param.fwid, param.iwid, ((param.funct3.rm == RV64FPRM3::DYN)?((RV64FPRM3)(((*p_fcsr)>>5)&7)):(param.funct3.rm)), &ret, s1); break;
        case RV64FPOP5::MVF2I : err = _fp_mv_f2i(param.fwid, param.funct3.mv, &ret, s1); break;
        case RV64FPOP5::MVI2F : RAW_DATA_AS(ret).u64 = RAW_DATA_AS(s1).u64; break;
        case RV64FPOP5::CMP   : err = _fp_cmp_64(param.funct3.cmp, &ret, RAW_DATA_AS(s1).f64, RAW_DATA_AS(s2).f64); break;
        default:
            break;
        }
    }
    else return SimError::unsupported;
    uint32_t flg = fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT);
    uint32_t rvflg = 0;
    if(flg & FE_INEXACT) rvflg |= RV_FFLAGS_INEXACT;
    if(flg & FE_UNDERFLOW) rvflg |= RV_FFLAGS_UNDERFLOW;
    if(flg & FE_OVERFLOW) rvflg |= RV_FFLAGS_OVERFLOW;
    if(flg & FE_DIVBYZERO) rvflg |= RV_FFLAGS_DIVBYZERO;
    if(flg & FE_INVALID) rvflg |= RV_FFLAGS_INVALID;
    (*p_fcsr) |= rvflg;
    
    *out = ret;
    return err;
}

SimError perform_fmadd_op(RV64OPCode opcode, RV64FPWidth2 fwid, RawDataT *out, FPDataT s1, FPDataT s2, FPDataT s3, uint64_t *p_fcsr) {
    if(!_supported_fp_width(fwid)) return SimError::unsupported;
    assert(out);
    RawDataT ret = 0;
    feclearexcept(FE_ALL_EXCEPT);
    if(fwid == RV64FPWidth2::fword) {
        switch (opcode) {
        case RV64OPCode::madd  : RAW_DATA_AS(ret).f32 = RAW_DATA_AS(s1).f32 * RAW_DATA_AS(s2).f32 + RAW_DATA_AS(s3).f32; break;
        case RV64OPCode::msub  : RAW_DATA_AS(ret).f32 = RAW_DATA_AS(s1).f32 * RAW_DATA_AS(s2).f32 - RAW_DATA_AS(s3).f32; break;
        case RV64OPCode::nmsub : RAW_DATA_AS(ret).f32 = -(RAW_DATA_AS(s1).f32 * RAW_DATA_AS(s2).f32) + RAW_DATA_AS(s3).f32; break;
        case RV64OPCode::nmadd : RAW_DATA_AS(ret).f32 = -(RAW_DATA_AS(s1).f32 * RAW_DATA_AS(s2).f32) - RAW_DATA_AS(s3).f32; break;
        default: return SimError::unsupported;
        }
    }
    else if(fwid == RV64FPWidth2::fdword) {
        switch (opcode) {
        case RV64OPCode::madd  : RAW_DATA_AS(ret).f64 = RAW_DATA_AS(s1).f64 * RAW_DATA_AS(s2).f64 + RAW_DATA_AS(s3).f64; break;
        case RV64OPCode::msub  : RAW_DATA_AS(ret).f64 = RAW_DATA_AS(s1).f64 * RAW_DATA_AS(s2).f64 - RAW_DATA_AS(s3).f64; break;
        case RV64OPCode::nmsub : RAW_DATA_AS(ret).f64 = -(RAW_DATA_AS(s1).f64 * RAW_DATA_AS(s2).f64) + RAW_DATA_AS(s3).f64; break;
        case RV64OPCode::nmadd : RAW_DATA_AS(ret).f64 = -(RAW_DATA_AS(s1).f64 * RAW_DATA_AS(s2).f64) - RAW_DATA_AS(s3).f64; break;
        default: return SimError::unsupported;
        }
    }
    else return SimError::unsupported;
    uint32_t flg = fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT);
    uint32_t rvflg = 0;
    if(flg & FE_INEXACT) rvflg |= RV_FFLAGS_INEXACT;
    if(flg & FE_UNDERFLOW) rvflg |= RV_FFLAGS_UNDERFLOW;
    if(flg & FE_OVERFLOW) rvflg |= RV_FFLAGS_OVERFLOW;
    if(flg & FE_DIVBYZERO) rvflg |= RV_FFLAGS_DIVBYZERO;
    if(flg & FE_INVALID) rvflg |= RV_FFLAGS_INVALID;
    (*p_fcsr) |= rvflg;

    *out = ret;
    return SimError::success;
}

SimError perform_amo_op(RV64AMOParam param, IntDataT *out, IntDataT s1, IntDataT s2) {
    IntDataT ret = s2;
    if(param.wid == RV64LSWidth::word) {
        switch (param.op) {
        case RV64AMOOP5::ADD : RAW_DATA_AS(ret).i64 = RAW_DATA_AS(s1).i32 + RAW_DATA_AS(s2).i32; break;
        case RV64AMOOP5::AND : RAW_DATA_AS(ret).i64 = RAW_DATA_AS(s1).i32 & RAW_DATA_AS(s2).i32; break;
        case RV64AMOOP5::OR  : RAW_DATA_AS(ret).i64 = RAW_DATA_AS(s1).i32 | RAW_DATA_AS(s2).i32; break;
        case RV64AMOOP5::MAX : RAW_DATA_AS(ret).i64 = std::max(RAW_DATA_AS(s1).i32, RAW_DATA_AS(s2).i32); break;
        case RV64AMOOP5::MIN : RAW_DATA_AS(ret).i64 = std::min(RAW_DATA_AS(s1).i32, RAW_DATA_AS(s2).i32); break;
        case RV64AMOOP5::MAXU: RAW_DATA_AS(ret).i64 = std::max(RAW_DATA_AS(s1).u32, RAW_DATA_AS(s2).u32); break;
        case RV64AMOOP5::MINU: RAW_DATA_AS(ret).i64 = std::min(RAW_DATA_AS(s1).u32, RAW_DATA_AS(s2).u32); break;
        case RV64AMOOP5::SWAP: break;
        default: return SimError::unsupported;
        }
    }
    else if(param.wid == RV64LSWidth::dword) {
        switch (param.op) {
        case RV64AMOOP5::ADD : RAW_DATA_AS(ret).i64 = RAW_DATA_AS(s1).i64 + RAW_DATA_AS(s2).i64; break;
        case RV64AMOOP5::AND : RAW_DATA_AS(ret).i64 = RAW_DATA_AS(s1).i64 & RAW_DATA_AS(s2).i64; break;
        case RV64AMOOP5::OR  : RAW_DATA_AS(ret).i64 = RAW_DATA_AS(s1).i64 | RAW_DATA_AS(s2).i64; break;
        case RV64AMOOP5::MAX : RAW_DATA_AS(ret).i64 = std::max(RAW_DATA_AS(s1).i64, RAW_DATA_AS(s2).i64); break;
        case RV64AMOOP5::MIN : RAW_DATA_AS(ret).i64 = std::min(RAW_DATA_AS(s1).i64, RAW_DATA_AS(s2).i64); break;
        case RV64AMOOP5::MAXU: RAW_DATA_AS(ret).i64 = std::max(RAW_DATA_AS(s1).u64, RAW_DATA_AS(s2).u64); break;
        case RV64AMOOP5::MINU: RAW_DATA_AS(ret).i64 = std::min(RAW_DATA_AS(s1).u64, RAW_DATA_AS(s2).u64); break;
        case RV64AMOOP5::SWAP: break;
        default: return SimError::unsupported;
        }
    }
    else {
        return SimError::unsupported;
    }
    
    *out = ret;
    return SimError::success;
}

};

namespace test {

bool test_operation() {

    return false;
}

}


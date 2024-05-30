
#include "isa.h"

#include "assert.h"

namespace isa {


#define GET_BITS_AT(n, offset, len) (((n) >> (offset)) & ((1<<(len)) - 1))

#define DEF_INST_PART_FUNC(name, offset, len) inline uint32_t name(RVInstT inst) { return GET_BITS_AT(inst, offset, len); }

DEF_INST_PART_FUNC(get_opcode, 0, 7);
DEF_INST_PART_FUNC(get_funct3, 12, 3);
DEF_INST_PART_FUNC(get_funct7, 25, 7);
DEF_INST_PART_FUNC(get_rd, 7, 5);
DEF_INST_PART_FUNC(get_rs1, 15, 5);
DEF_INST_PART_FUNC(get_rs2, 20, 5);
DEF_INST_PART_FUNC(get_rs3, 27, 5);

inline uint64_t get_imm_I(RVInstT inst) {
    uint64_t ret = (GET_BITS_AT(inst, 20, 12));
    if(inst & (1<<31)) ret = ret | 0xfffffffffffff000UL;
    return ret;
}
inline uint64_t get_imm_S(RVInstT inst) {
    uint64_t ret = (GET_BITS_AT(inst, 7, 5)) | (GET_BITS_AT(inst, 25, 7) << 5);
    if(inst & (1<<31)) ret = ret | 0xfffffffffffff000UL;
    return ret;
}
inline uint64_t get_imm_B(RVInstT inst) {
    uint64_t ret = (GET_BITS_AT(inst, 8, 4) << 1) | (GET_BITS_AT(inst, 25, 6) << 5) | (GET_BITS_AT(inst, 7, 1) << 11);
    if(inst & (1<<31)) ret |= 0xfffffffffffff000UL;
    return ret;
}
inline uint64_t get_imm_U(RVInstT inst) {
    uint64_t ret = (inst & 0xfffff000U);
    if(inst & (1<<31)) ret |= 0xffffffff00000000UL;
    return ret;
}
inline uint64_t get_imm_J(RVInstT inst) {
    uint64_t ret = (GET_BITS_AT(inst, 21, 10) << 1) | (GET_BITS_AT(inst, 20, 1) << 11) | (inst & 0xFF000);
    if(inst & (1<<31)) ret = ret | 0xfffffffffff00000UL;
    return ret;
}

typedef struct {
    RVRegIndexT     rd;
    RVRegIndexT     rs1;
    RVRegIndexT     rs2;
    uint16_t        funct3;
    uint16_t        funct7;
} RVInstTypeR;

RVInstTypeR parse_inst_R(RVInstT inst) {
    RVInstTypeR ret;
    ret.rd = get_rd(inst);
    ret.funct3 = get_funct3(inst);
    ret.funct7 = get_funct7(inst);
    ret.rs1 = get_rs1(inst);
    ret.rs2 = get_rs2(inst);
    return ret;
}

typedef struct {
    RVRegIndexT     rd;
    RVRegIndexT     rs1;
    uint16_t        funct3;
    uint64_t        imm;
} RVInstTypeI;

RVInstTypeI parse_inst_I(RVInstT inst) {
    RVInstTypeI ret;
    ret.rd = get_rd(inst);
    ret.funct3 = get_funct3(inst);
    ret.rs1 = get_rs1(inst);
    ret.imm = get_imm_I(inst);
    return ret;
}

typedef struct {
    RVRegIndexT     rs1;
    RVRegIndexT     rs2;
    uint16_t        funct3;
    uint64_t        imm;
} RVInstTypeS;

RVInstTypeS parse_inst_S(RVInstT inst) {
    RVInstTypeS ret;
    ret.funct3 = get_funct3(inst);
    ret.rs1 = get_rs1(inst);
    ret.rs2 = get_rs2(inst);
    ret.imm = get_imm_S(inst);
    return ret;
}

typedef struct {
    RVRegIndexT     rs1;
    RVRegIndexT     rs2;
    uint16_t        funct3;
    uint64_t        imm;
} RVInstTypeB;

RVInstTypeB parse_inst_B(RVInstT inst) {
    RVInstTypeB ret;
    ret.funct3 = get_funct3(inst);
    ret.rs1 = get_rs1(inst);
    ret.rs2 = get_rs2(inst);
    ret.imm = get_imm_B(inst);
    return ret;
}

typedef struct {
    RVRegIndexT     rd;
    uint64_t        imm;
} RVInstTypeU;

RVInstTypeU parse_inst_U(RVInstT inst) {
    RVInstTypeU ret;
    ret.rd = get_rd(inst);
    ret.imm = get_imm_U(inst);
    return ret;
}

typedef struct {
    RVRegIndexT     rd;
    uint64_t        imm;
} RVInstTypeJ;

RVInstTypeJ parse_inst_J(RVInstT inst) {
    RVInstTypeJ ret;
    ret.rd = get_rd(inst);
    ret.imm = get_imm_J(inst);
    return ret;
}

bool decode_rv64c(RVInstT raw, RV64InstDecoded *dec);

bool decode_rv64(RVInstT raw, RV64InstDecoded *dec) {
    dec->opcode = RV64OPCode::nop;
    dec->rs1 = dec->rs2 = dec->rs3 = dec->rd = dec->flag = 0;
    dec->imm = 0;
    dec->debug_name_str.clear();
    memset(&(dec->param), 0, sizeof(dec->param));

    if(isRVC(raw)) {
        return decode_rv64c(raw, dec);
    }

    RV64OPCode op = (RV64OPCode)get_opcode(raw);
    dec->opcode = op;

    if(op == RV64OPCode::lui || op == RV64OPCode::auipc) {
        auto inst = parse_inst_U(raw);
        dec->rd = inst.rd;
        dec->imm = inst.imm;
        dec->flag |= RVINSTFLAG_RDINT;
    }
    else if(op == RV64OPCode::jal) {
        auto inst = parse_inst_J(raw);
        dec->rd = inst.rd;
        dec->imm = inst.imm;
        dec->flag |= RVINSTFLAG_RDINT;
    }
    else if(op == RV64OPCode::jalr) {
        auto inst = parse_inst_I(raw);
        dec->rs1 = inst.rs1;
        dec->rd = inst.rd;
        dec->imm = inst.imm;
        dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
    }
    else if(op == RV64OPCode::branch) {
        auto inst = parse_inst_B(raw);
        dec->rs1 = inst.rs1;
        dec->rs2 = inst.rs2;
        dec->imm = inst.imm;
        dec->param.branch = (RV64BranchOP3)(inst.funct3);
        dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
    }
    else if(op == RV64OPCode::load) {
        auto inst = parse_inst_I(raw);
        dec->rs1 = inst.rs1;
        dec->rd = inst.rd;
        dec->imm = inst.imm;
        dec->param.loadstore = (RV64LSWidth)(inst.funct3);
        dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
    }
    else if(op == RV64OPCode::loadfp) {
        auto inst = parse_inst_I(raw);
        dec->rs1 = inst.rs1;
        dec->rd = inst.rd;
        dec->imm = inst.imm;
        dec->param.loadstore = (RV64LSWidth)(inst.funct3);
        dec->flag |= (RVINSTFLAG_RDFP | RVINSTFLAG_S1INT);
    }
    else if(op == RV64OPCode::store) {
        auto inst = parse_inst_S(raw);
        dec->rs1 = inst.rs1;
        dec->rs2 = inst.rs2;
        dec->imm = inst.imm;
        dec->param.loadstore = (RV64LSWidth)(inst.funct3);
        dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
    }
    else if(op == RV64OPCode::storefp) {
        auto inst = parse_inst_S(raw);
        dec->rs1 = inst.rs1;
        dec->rs2 = inst.rs2;
        dec->imm = inst.imm;
        dec->param.loadstore = (RV64LSWidth)(inst.funct3);
        dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2FP);
    }
    else if(op == RV64OPCode::opimm || op == RV64OPCode::opimm32) {
        auto inst = parse_inst_I(raw);
        dec->rs1 = inst.rs1;
        dec->rd = inst.rd;
        dec->imm = inst.imm;
        dec->param.intop = (RV64IntOP73)(inst.funct3);
        if(dec->param.intop == RV64IntOP73::SLL || dec->param.intop == RV64IntOP73::SRL) {
            if(op == RV64OPCode::opimm) dec->imm &= 0x3fUL;
            else if(op == RV64OPCode::opimm32) dec->imm &= 0x1fUL;
        }
        if(dec->param.intop == RV64IntOP73::SRL && (raw & (1 << 30))) {
            dec->param.intop = RV64IntOP73::SRA;
        }
        dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
    }
    else if(op == RV64OPCode::op || op == RV64OPCode::op32) {
        auto inst = parse_inst_R(raw);
        dec->rs1 = inst.rs1;
        dec->rs2 = inst.rs2;
        dec->rd = inst.rd;
        dec->param.intop = (RV64IntOP73)(inst.funct3 | (inst.funct7 << 3));
        dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
    }
    else if(op == RV64OPCode::amo) {
        auto inst = parse_inst_R(raw);
        dec->rs1 = inst.rs1;
        dec->rs2 = inst.rs2;
        dec->rd = inst.rd;
        dec->param.amo.op = (RV64AMOOP5)(inst.funct7 >> 2);
        dec->param.amo.wid = (RV64LSWidth)(inst.funct3);
        dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
        if(dec->param.amo.op != RV64AMOOP5::LR) dec->flag |= RVINSTFLAG_S2INT;
    }
    else if(op == RV64OPCode::opfp) {
        auto inst = parse_inst_R(raw);
        dec->rs1 = inst.rs1;
        dec->rs2 = inst.rs2;
        dec->rd = inst.rd;
        dec->param.fp.op = (RV64FPOP5)(inst.funct7 >> 2);
        dec->param.fp.fwid = (RV64FPWidth2)(inst.funct7 & 3);
        dec->param.fp.iwid = (RV64FPCVTWidth5)(inst.rs2);
        dec->param.fp.funct3.rm = (RV64FPRM3)(inst.funct3);
        dec->flag |= (rv64_fpop_is_i_rd(dec->param.fp.op)?RVINSTFLAG_RDINT:RVINSTFLAG_RDFP);
        dec->flag |= (rv64_fpop_is_i_s1(dec->param.fp.op)?RVINSTFLAG_S1INT:RVINSTFLAG_S1FP);
        dec->flag |= (rv64_fpop_has_s2(dec->param.fp.op)?RVINSTFLAG_S2FP:0);
    }
    else if(op == RV64OPCode::madd || op == RV64OPCode::msub || op == RV64OPCode::nmsub || op == RV64OPCode::nmadd) {
        auto inst = parse_inst_R(raw);
        dec->rs1 = inst.rs1;
        dec->rs2 = inst.rs2;
        dec->rs3 = (inst.funct7 >> 2);
        dec->rd = inst.rd;
        dec->param.fp.fwid = (RV64FPWidth2)(inst.funct7 & 3);
        dec->param.fp.funct3.rm = (RV64FPRM3)(inst.funct3);
        dec->flag |= (RVINSTFLAG_RDFP | RVINSTFLAG_S1FP | RVINSTFLAG_S2FP | RVINSTFLAG_S3FP);
    }
    else if(op == RV64OPCode::miscmem) {
        auto inst = parse_inst_I(raw);
        dec->rs1 = inst.rs1;
        dec->rd = inst.rd;
        dec->imm = inst.imm;
        dec->flag |= (RVINSTFLAG_UNIQUE);
        if(inst.funct3 == 0) {
            if(raw == 0x8330000) dec->flag |= RVINSTFLAG_FENCETSO;
            else if(raw == 0x0100000f) dec->flag |= RVINSTFLAG_PAUSE;
            else dec->flag |= RVINSTFLAG_FENCE;
        }
        else if(inst.funct3 == 1) {
            dec->flag |= RVINSTFLAG_FENCEI;
        }
        else {
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
        }
    }
    else if(op == RV64OPCode::system) {
        auto inst = parse_inst_I(raw);
        dec->rs1 = inst.rs1;
        dec->rs2 = inst.imm;
        dec->imm = inst.imm;
        dec->rd = inst.rd;
        dec->param.csr.op = (RV64CSROP3)(inst.funct3);
        dec->param.csr.index = (RVCSRIndexT)(inst.imm);
        dec->flag |= (RVINSTFLAG_UNIQUE);
        if(raw == 0x00000073) dec->flag |= RVINSTFLAG_ECALL;
        else if(raw == 0x00100073) dec->flag |= RVINSTFLAG_EBREAK;
        else dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
    }
    else {
        return false;
    }
    return true;
}

bool decode_rv64c(RVInstT raw, RV64InstDecoded *dec) {
    dec->flag |= RVINSTFLAG_RVC;
    uint32_t func = (raw >> 13) & 7;
    if((raw & 3) == 0) {
        if(func == 0) { // C.ADDI4SPN
            dec->opcode = RV64OPCode::opimm;
            dec->rd = GET_BITS_AT(raw, 2, 3) + 8;
            dec->rs1 = 2;
            uint64_t uimm = 0;
            uimm |= (((raw >> 5) & 1) << 3);
            uimm |= (((raw >> 6) & 1) << 2);
            uimm |= (GET_BITS_AT(raw, 7, 4) << 6);
            uimm |= (((raw >> 11) & 1) << 4);
            uimm |= (((raw >> 12) & 1) << 5);
            if((uimm >> 10) & 1) uimm |= 0xfffffffffffffe00UL;
            dec->imm = uimm;
            dec->param.intop = RV64IntOP73::ADD;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 1) { // C.FLD
            dec->opcode = RV64OPCode::loadfp;
            dec->rd = GET_BITS_AT(raw, 2, 3) + 8;
            dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 5, 2) << 6);
            uimm |= (GET_BITS_AT(raw, 10, 3) << 3);
            dec->imm = uimm;
            dec->param.loadstore = RV64LSWidth::dword;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 2) { // C.LW
            dec->opcode = RV64OPCode::load;
            dec->rd = GET_BITS_AT(raw, 2, 3) + 8;
            dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 5, 1) << 6);
            uimm |= (GET_BITS_AT(raw, 6, 1) << 2);
            uimm |= (GET_BITS_AT(raw, 10, 3) << 3);
            dec->imm = uimm;
            dec->param.loadstore = RV64LSWidth::word;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 3) { // C.LD
            dec->opcode = RV64OPCode::load;
            dec->rd = GET_BITS_AT(raw, 2, 3) + 8;
            dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 5, 2) << 6);
            uimm |= (GET_BITS_AT(raw, 10, 3) << 3);
            dec->imm = uimm;
            dec->param.loadstore = RV64LSWidth::dword;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 5) { // C.FSD
            dec->opcode = RV64OPCode::store;
            dec->rs2 = GET_BITS_AT(raw, 2, 3) + 8;
            dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 5, 2) << 6);
            uimm |= (GET_BITS_AT(raw, 10, 3) << 3);
            dec->imm = uimm;
            dec->param.loadstore = RV64LSWidth::dword;
            dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
            return true;
        }
        else if(func == 6) { // C.SW
            dec->opcode = RV64OPCode::store;
            dec->rs2 = GET_BITS_AT(raw, 2, 3) + 8;
            dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 5, 1) << 6);
            uimm |= (GET_BITS_AT(raw, 6, 1) << 2);
            uimm |= (GET_BITS_AT(raw, 10, 3) << 3);
            dec->imm = uimm;
            dec->param.loadstore = RV64LSWidth::word;
            dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
            return true;
        }
        else if(func == 7) { // C.SD
            dec->opcode = RV64OPCode::store;
            dec->rs2 = GET_BITS_AT(raw, 2, 3) + 8;
            dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 5, 2) << 6);
            uimm |= (GET_BITS_AT(raw, 10, 3) << 3);
            dec->imm = uimm;
            dec->param.loadstore = RV64LSWidth::dword;
            dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
            return true;
        }
    }
    else if((raw & 3) == 1) {
        if(func == 0) { // C.ADDI
            dec->opcode = RV64OPCode::opimm;
            dec->param.intop = RV64IntOP73::ADD;
            dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 5);
            uint64_t uimm = GET_BITS_AT(raw, 2, 5);
            if((raw >> 12) & 1) uimm |= 0xffffffffffffffe0UL;
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 1) { // C.ADDIW
            dec->opcode = RV64OPCode::opimm32;
            dec->param.intop = RV64IntOP73::ADD;
            dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 5);
            uint64_t uimm = GET_BITS_AT(raw, 2, 5);
            if((raw >> 12) & 1) uimm |= 0xffffffffffffffe0UL;
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 2 && GET_BITS_AT(raw, 7, 5) != 0) { // C.LI
            dec->opcode = RV64OPCode::lui;
            dec->rd = GET_BITS_AT(raw, 7, 5);
            uint64_t uimm = GET_BITS_AT(raw, 2, 5);
            if((raw >> 12) & 1) uimm |= 0xffffffffffffffe0UL;
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT);
            return true;
        }
        else if(func == 3 && GET_BITS_AT(raw, 7, 5) == 2) { // C.ADDI16SP
            dec->opcode = RV64OPCode::opimm;
            dec->param.intop = RV64IntOP73::ADD;
            dec->rd = dec->rs1 = 2;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 2, 1) << 5);
            uimm |= (GET_BITS_AT(raw, 3, 2) << 7);
            uimm |= (GET_BITS_AT(raw, 5, 1) << 6);
            uimm |= (GET_BITS_AT(raw, 6, 1) << 4);
            if((raw >> 12) & 1) uimm |= 0xfffffffffffffe00UL;
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 3 && GET_BITS_AT(raw, 7, 5)) { // C.LUI
            dec->opcode = RV64OPCode::lui;
            dec->rd = GET_BITS_AT(raw, 7, 5);
            uint64_t uimm = (GET_BITS_AT(raw, 2, 5) << 12);
            if((raw >> 12) & 1) uimm |= 0xfffffffffffe0000UL;
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT);
            return true;
        }
        else if(func == 4) {
            uint32_t f1 = GET_BITS_AT(raw, 10, 2), f2 = GET_BITS_AT(raw, 5, 2);
            if(f1 == 0) { // C.SRLI64
                dec->opcode = RV64OPCode::opimm;
                dec->param.intop = RV64IntOP73::SRL;
                dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
                uint64_t uimm = (GET_BITS_AT(raw, 2, 5));
                uimm |= (((raw >> 12) & 1) << 5);
                dec->imm = uimm;
                dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
                return true;
            }
            else if(f1 == 1) { // C.SRAI64
                dec->opcode = RV64OPCode::opimm;
                dec->param.intop = RV64IntOP73::SRA;
                dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
                uint64_t uimm = (GET_BITS_AT(raw, 2, 5));
                uimm |= (((raw >> 12) & 1) << 5);
                dec->imm = uimm;
                dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
                return true;
            }
            else if(f1 == 2) { // C.ANDI
                dec->opcode = RV64OPCode::opimm;
                dec->param.intop = RV64IntOP73::AND;
                dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
                uint64_t uimm = (GET_BITS_AT(raw, 2, 5));
                if((raw >> 12) & 1) uimm |= 0xffffffffffffffe0UL;
                dec->imm = uimm;
                dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
                return true;
            }
            else if((raw >> 12) & 1) { // C.SUBW  C.ADDW
                dec->opcode = RV64OPCode::op32;
                dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
                dec->rs2 = GET_BITS_AT(raw, 2, 3) + 8;
                dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
                if(f2 == 0) {
                    dec->param.intop = RV64IntOP73::SUB;
                    return true;
                }
                else if(f2 == 1) {
                    dec->param.intop = RV64IntOP73::ADD;
                    return true;
                }
            }
            else {
                dec->opcode = RV64OPCode::op;
                dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
                dec->rs2 = GET_BITS_AT(raw, 2, 3) + 8;
                dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
                if(f2 == 0) { // C.SUB
                    dec->param.intop = RV64IntOP73::SUB;
                    return true;
                }
                else if(f2 == 1) { // C.XOR
                    dec->param.intop = RV64IntOP73::XOR;
                    return true;
                }
                else if(f2 == 2) { // C.OR
                    dec->param.intop = RV64IntOP73::OR;
                    return true;
                }
                else if(f2 == 3) { // C.AND
                    dec->param.intop = RV64IntOP73::AND;
                    return true;
                }
            }
        }
        else if(func == 5) { // C.J
            dec->opcode = RV64OPCode::jal;
            dec->rd = 0;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 2, 1) << 5);
            uimm |= (GET_BITS_AT(raw, 3, 3) << 1);
            uimm |= (GET_BITS_AT(raw, 6, 1) << 7);
            uimm |= (GET_BITS_AT(raw, 7, 1) << 6);
            uimm |= (GET_BITS_AT(raw, 8, 1) << 10);
            uimm |= (GET_BITS_AT(raw, 9, 2) << 8);
            uimm |= (GET_BITS_AT(raw, 11, 1) << 4);
            if((raw >> 12) & 1) uimm |= 0xfffffffffffff800UL;
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT);
            return true;
        }
        else if(func == 6) { // C.BEQZ
            dec->opcode = RV64OPCode::branch;
            dec->param.branch = RV64BranchOP3::BEQ;
            dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
            dec->rs2 = 0;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 2, 1) << 5);
            uimm |= (GET_BITS_AT(raw, 3, 2) << 1);
            uimm |= (GET_BITS_AT(raw, 5, 2) << 6);
            uimm |= (GET_BITS_AT(raw, 10, 2) << 3);
            if((raw >> 12) & 1) uimm |= 0xffffffffffffff00UL;
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
            return true;
        }
        else if(func == 7) { // C.BNEZ
            dec->opcode = RV64OPCode::branch;
            dec->param.branch = RV64BranchOP3::BNE;
            dec->rs1 = GET_BITS_AT(raw, 7, 3) + 8;
            dec->rs2 = 0;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 2, 1) << 5);
            uimm |= (GET_BITS_AT(raw, 3, 2) << 1);
            uimm |= (GET_BITS_AT(raw, 5, 2) << 6);
            uimm |= (GET_BITS_AT(raw, 10, 2) << 3);
            if((raw >> 12) & 1) uimm |= 0xffffffffffffff00UL;
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
            return true;
        }
    }
    else if((raw & 3) == 2) { // C.SLLI64
        if(func == 0 && GET_BITS_AT(raw, 7, 5) != 0) {
            dec->opcode = RV64OPCode::opimm;
            dec->param.intop = RV64IntOP73::SLL;
            dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 5);
            uint64_t uimm = (GET_BITS_AT(raw, 2, 5));
            uimm |= (((raw >> 12) & 1) << 5);
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 1) { // C.FLDSP
            dec->opcode = RV64OPCode::loadfp;
            dec->param.loadstore = RV64LSWidth::dword;
            dec->rd = GET_BITS_AT(raw, 7, 5);
            dec->rs1 = 2;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 2, 3) << 6);
            uimm |= (GET_BITS_AT(raw, 5, 2) << 3);
            uimm |= (GET_BITS_AT(raw, 12, 1) << 5);
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 2) { // C.LWSP
            dec->opcode = RV64OPCode::load;
            dec->param.loadstore = RV64LSWidth::word;
            dec->rd = GET_BITS_AT(raw, 7, 5);
            dec->rs1 = 2;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 2, 2) << 6);
            uimm |= (GET_BITS_AT(raw, 4, 3) << 2);
            uimm |= (GET_BITS_AT(raw, 12, 1) << 5);
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 3) { // C.LDSP
            dec->opcode = RV64OPCode::load;
            dec->param.loadstore = RV64LSWidth::dword;
            dec->rd = GET_BITS_AT(raw, 7, 5);
            dec->rs1 = 2;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 2, 3) << 6);
            uimm |= (GET_BITS_AT(raw, 5, 2) << 3);
            uimm |= (GET_BITS_AT(raw, 12, 1) << 5);
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
            return true;
        }
        else if(func == 4) {
            if(((raw >> 12) & 1) == 0) {
                dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
                if(GET_BITS_AT(raw, 2, 5)) { // C.MV
                    dec->opcode = RV64OPCode::opimm;
                    dec->param.intop = RV64IntOP73::ADD;
                    dec->rd = GET_BITS_AT(raw, 7, 5);
                    dec->rs1 = GET_BITS_AT(raw, 2, 5);
                    dec->imm = 0;
                    return true;
                }
                else { // C.JR
                    dec->opcode = RV64OPCode::jalr;
                    dec->rs1 = GET_BITS_AT(raw, 7, 5);
                    dec->rd = 0;
                    dec->imm = 0;
                    return true;
                }
            }
            else {
                if(GET_BITS_AT(raw, 2, 5) != 0) { // C.ADD
                    dec->opcode = RV64OPCode::op;
                    dec->param.intop = RV64IntOP73::ADD;
                    dec->rd = dec->rs1 = GET_BITS_AT(raw, 7, 5);
                    dec->rs2 = GET_BITS_AT(raw, 2, 5);
                    dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
                    return true;
                }
                else if(GET_BITS_AT(raw, 7, 5)) { // C.JALR
                    dec->opcode = RV64OPCode::jalr;
                    dec->rs1 = GET_BITS_AT(raw, 7, 5);
                    dec->rd = 1;
                    dec->imm = 0;
                    dec->flag |= (RVINSTFLAG_RDINT | RVINSTFLAG_S1INT);
                    return true;
                }
                else { // C.EBREAK
                    dec->opcode = RV64OPCode::system;
                    dec->flag |= (RVINSTFLAG_EBREAK | RVINSTFLAG_UNIQUE);
                    return true;
                }
            }
        }
        else if(func == 5) { // C.FSDSP
            dec->opcode = RV64OPCode::storefp;
            dec->param.loadstore = RV64LSWidth::dword;
            dec->rs2 = GET_BITS_AT(raw, 2, 5);
            dec->rs1 = 2;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 7, 3) << 6);
            uimm |= (GET_BITS_AT(raw, 10, 3) << 3);
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
            return true;
        }
        else if(func == 6) { // C.SWSP
            dec->opcode = RV64OPCode::store;
            dec->param.loadstore = RV64LSWidth::word;
            dec->rs2 = GET_BITS_AT(raw, 2, 5);
            dec->rs1 = 2;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 7, 2) << 6);
            uimm |= (GET_BITS_AT(raw, 9, 4) << 2);
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
            return true;
        }
        else if(func == 7) { // C.SDSP
            dec->opcode = RV64OPCode::store;
            dec->param.loadstore = RV64LSWidth::dword;
            dec->rs2 = GET_BITS_AT(raw, 2, 5);
            dec->rs1 = 2;
            uint64_t uimm = 0;
            uimm |= (GET_BITS_AT(raw, 7, 3) << 6);
            uimm |= (GET_BITS_AT(raw, 10, 3) << 3);
            dec->imm = uimm;
            dec->flag |= (RVINSTFLAG_S1INT | RVINSTFLAG_S2INT);
            return true;
        }
    }
    return false;

}

bool pdec_get_J_target(uint32_t inst, int32_t *target) {
    if(isRVC(inst)) {
        if((inst & 3) == 1 && ((inst>>13) & 7) == 5) {
            uint32_t uimm = 0;
            uimm |= (GET_BITS_AT(inst, 2, 1) << 5);
            uimm |= (GET_BITS_AT(inst, 3, 3) << 1);
            uimm |= (GET_BITS_AT(inst, 6, 1) << 7);
            uimm |= (GET_BITS_AT(inst, 7, 1) << 6);
            uimm |= (GET_BITS_AT(inst, 8, 1) << 10);
            uimm |= (GET_BITS_AT(inst, 9, 2) << 8);
            uimm |= (GET_BITS_AT(inst, 11, 1) << 4);
            if((inst >> 12) & 1) uimm |= 0xfffff800U;
            *target = uimm;
            return true;
        }
    }
    else {
        if((inst & 0x7F) == OP_JAL) {
            uint32_t ret = (GET_BITS_AT(inst, 21, 10) << 1) | (GET_BITS_AT(inst, 20, 1) << 11) | (inst & 0xFF000);
            if(inst & (1<<31)) ret = ret | 0xfff00000U;
            *target = ret;
            return true;
        }
    }
    return false;
}

bool pdec_get_B_target(uint32_t inst, int32_t *target) {
    if(isRVC(inst)) {
        if((inst & 3) == 1 && ((inst>>13) & 6) == 6) {
            uint32_t uimm = 0;
            uimm |= (GET_BITS_AT(inst, 2, 1) << 5);
            uimm |= (GET_BITS_AT(inst, 3, 2) << 1);
            uimm |= (GET_BITS_AT(inst, 5, 2) << 6);
            uimm |= (GET_BITS_AT(inst, 10, 2) << 3);
            if((inst >> 12) & 1) uimm |= 0xffffffffffffff00UL;
            *target = uimm;
            return true;
        }
    }
    else {
        if((inst & 0x7F) == OP_BRANCH) {
            uint64_t ret = (GET_BITS_AT(inst, 8, 4) << 1) | (GET_BITS_AT(inst, 25, 6) << 5) | (GET_BITS_AT(inst, 7, 1) << 11);
            if(inst & (1<<31)) ret |= 0xfffffffffffff000UL;
            *target = ret;
            return true;
        }
    }
    return false;
}


char* IregNameList[32] = {
    (char*)" x0", // x0
    (char*)" ra",   // x1
    (char*)" sp",   // x2
    (char*)" gp",   // x3
    (char*)" tp",   // x4
    (char*)" t0",   // x5
    (char*)" t1",   // x6
    (char*)" t2",   // x7
    (char*)" s0",   // x8
    (char*)" s1",   // x9
    (char*)" a0",   // x10
    (char*)" a1",   // x11
    (char*)" a2",   // x12
    (char*)" a3",   // x13
    (char*)" a4",   // x14
    (char*)" a5",   // x15
    (char*)" a6",   // x16
    (char*)" a7",   // x17
    (char*)" s2",   // x18
    (char*)" s3",   // x19
    (char*)" s4",   // x20
    (char*)" s5",   // x21
    (char*)" s6",   // x22
    (char*)" s7",   // x23
    (char*)" s8",   // x24
    (char*)" s9",   // x25
    (char*)"s10",  // x26
    (char*)"s11",  // x27
    (char*)" t3",   // x28
    (char*)" t4",   // x29
    (char*)" t5",   // x30
    (char*)" t6",   // x31
};

char** ireg_names() {
    return IregNameList;
}

uint16_t ireg_index_of(const char* name) {
    for(uint16_t i = 0; i < 32; i++) {
        const char *p1 = IregNameList[i], *p2 = name;
        while(*p1 && *p2) {
            if(*p1 == ' ') p1++;
            else if(*p2 == ' ') p2++;
            else if(*p1 != *p2) break;
            else {
                p1++; p2++;
            }
        }
        if(*p1 == 0 && *p2 == 0) return i;
    }
    return 0;
}

char* FregNameList[32] = {
    (char*)" ft0",  // x0
    (char*)" ft1",   // x1
    (char*)" ft2",   // x2
    (char*)" ft3",   // x3
    (char*)" ft4",   // x4
    (char*)" ft5",   // x5
    (char*)" ft6",   // x6
    (char*)" ft7",   // x7
    (char*)" fs0",   // x8
    (char*)" fs0",   // x9
    (char*)" fa0",   // x10
    (char*)" fa1",   // x11
    (char*)" fa2",   // x12
    (char*)" fa3",   // x13
    (char*)" fa4",   // x14
    (char*)" fa5",   // x15
    (char*)" fa6",   // x16
    (char*)" fa7",   // x17
    (char*)" fs2",   // x18
    (char*)" fs3",   // x19
    (char*)" fs4",   // x20
    (char*)" fs5",   // x21
    (char*)" fs6",   // x22
    (char*)" fs7",   // x23
    (char*)" fs8",   // x24
    (char*)" fs9",   // x25
    (char*)"fs10",  // x26
    (char*)"fs11",  // x27
    (char*)" ft8",   // x28
    (char*)" ft9",   // x29
    (char*)"ft10",   // x30
    (char*)"ft11",   // x31
};

char** freg_names() {
    return FregNameList;
}

uint16_t freg_index_of(const char* name) {
    for(uint16_t i = 0; i < 32; i++) {
        const char *p1 = FregNameList[i], *p2 = name;
        while(*p1 && *p2) {
            if(*p1 == ' ') p1++;
            else if(*p2 == ' ') p2++;
            else if(*p1 != *p2) break;
            else {
                p1++; p2++;
            }
        }
        if(*p1 == 0 && *p2 == 0) return i;
    }
    return 0;
}


#undef GET_BITS_AT
#undef DEF_INST_PART_FUNC


}

namespace test {

bool test_decoder_rv64() {

    using namespace isa; 

    #define COMPARE_ASSERT(x, v, fmt) if(x != v) {printf("expect " #x " = " fmt ", real value : " fmt "\n", v, x); assert(x == v);}

    #define DECODER_TEST_INT(_inst, _opcode, _iop, _d1, _s1, _s2,  _imms) { \
    RV64InstDecoded dec; \
    assert(isa::decode_rv64(_inst, &dec)); \
    assert(dec.opcode == _opcode); \
    assert(dec.param.intop == _iop); \
    assert(dec.rd == _d1); \
    assert(dec.rs1 == _s1); \
    assert(dec.rs2 == _s2); \
    assert(dec.imm == _imms);}

    // LUI
    DECODER_TEST_INT(0x4581U, RV64OPCode::lui, (RV64IntOP73)0, ireg_index_of("a1"), 0, 0,  0L); // li      a1,0
    DECODER_TEST_INT(0x6441U, RV64OPCode::lui, (RV64IntOP73)0, ireg_index_of("s0"), 0, 0, 0x10000UL); //  lui     s0,0x10000
    DECODER_TEST_INT(0x72fdU, RV64OPCode::lui, (RV64IntOP73)0, ireg_index_of("t0"), 0, 0, -4096UL); //  lui     t0,-4096
    DECODER_TEST_INT(0x000217b7U, RV64OPCode::lui, (RV64IntOP73)0, ireg_index_of("a5"), 0, 0, 0x21000UL); //  lui     a5,0x21000

    // AUIPC
    DECODER_TEST_INT(0x00011197U, RV64OPCode::auipc, (RV64IntOP73)0, ireg_index_of("gp"), 0, 0, 0x11000UL); // auipc   gp,0x11
    DECODER_TEST_INT(0x00042517U, RV64OPCode::auipc, (RV64IntOP73)0, ireg_index_of("a0"), 0, 0, 0x42000UL); // auipc	a0,0x42

    // J
    DECODER_TEST_INT(0xa815U, RV64OPCode::jal, (RV64IntOP73)0, 0, 0, 0, 0x34UL); // j       0x34(pc)
    DECODER_TEST_INT(0xaa75U, RV64OPCode::jal, (RV64IntOP73)0, 0, 0, 0, 444UL); // j       444(pc)
    DECODER_TEST_INT(0x34b010efU, RV64OPCode::jal, (RV64IntOP73)0, ireg_index_of("ra"), 0, 0, 0x1b4aUL); // jal     ra,0x1b4a(pc)
    DECODER_TEST_INT(0x8082U, RV64OPCode::jalr, (RV64IntOP73)0, 0, ireg_index_of("ra"), 0, 0L); // ret

    // OPIMM
    DECODER_TEST_INT(0x91c1U, RV64OPCode::opimm, RV64IntOP73::SRL, ireg_index_of("a1"), ireg_index_of("a1"), 0, 0x30UL); // srli    a1,a1,0x30
    DECODER_TEST_INT(0x1702U, RV64OPCode::opimm, RV64IntOP73::SLL, ireg_index_of("a4"), ireg_index_of("a4"), 0, 0x20UL); // slli    a4,a4,0x20
    DECODER_TEST_INT(0x030cdc93U, RV64OPCode::opimm, RV64IntOP73::SRL, ireg_index_of("s9"), ireg_index_of("s9"), 0, 0x30UL); // srli    s9,s9,0x30
    DECODER_TEST_INT(0x03079493U, RV64OPCode::opimm, RV64IntOP73::SLL, ireg_index_of("s1"), ireg_index_of("a5"), 0, 0x30UL); // slli    s1,a5,0x30
    DECODER_TEST_INT(0x8b91U, RV64OPCode::opimm, RV64IntOP73::AND, ireg_index_of("a5"), ireg_index_of("a5"), 0, 4UL); // andi    a5,a5,4
    DECODER_TEST_INT(0x00f77793U, RV64OPCode::opimm, RV64IntOP73::AND, ireg_index_of("a5"), ireg_index_of("a4"), 0, 15UL); // andi	a5,a4,15
    DECODER_TEST_INT(0x00158713U, RV64OPCode::opimm, RV64IntOP73::ADD, ireg_index_of("a4"), ireg_index_of("a1"), 0, 1UL); // addi	a4,a1,1
    DECODER_TEST_INT(0xea618193U, RV64OPCode::opimm, RV64IntOP73::ADD, ireg_index_of("gp"), ireg_index_of("gp"), 0, -346L); // addi    gp,gp,-346
    DECODER_TEST_INT(0x17c50513U, RV64OPCode::opimm, RV64IntOP73::ADD, ireg_index_of("a0"), ireg_index_of("a0"), 0, 380UL); // addi    a0,a0,380
    DECODER_TEST_INT(0x1141U, RV64OPCode::opimm, RV64IntOP73::ADD, ireg_index_of("sp"), ireg_index_of("sp"), 0, -16L); // addi    sp,sp,-16
    DECODER_TEST_INT(0x1880U, RV64OPCode::opimm, RV64IntOP73::ADD, ireg_index_of("s0"), ireg_index_of("sp"), 0, 112UL); // addi	s0,sp,112
    DECODER_TEST_INT(0x854eU, RV64OPCode::opimm, RV64IntOP73::ADD, ireg_index_of("a0"), ireg_index_of("s3"), 0, 0L); // mv      a0,s3
    DECODER_TEST_INT(0x872aU, RV64OPCode::opimm, RV64IntOP73::ADD, ireg_index_of("a4"), ireg_index_of("a0"), 0, 0L); // mv      a4,a0
    DECODER_TEST_INT(0x00010413U, RV64OPCode::opimm, RV64IntOP73::ADD, ireg_index_of("s0"), ireg_index_of("sp"), 0, 0L); // mv	s0,sp

    DECODER_TEST_INT(0x01079d9bU, RV64OPCode::opimm32, RV64IntOP73::SLL, ireg_index_of("s11"), ireg_index_of("a5"), 0, 0x10UL); // slliw   s11,a5,0x10
    DECODER_TEST_INT(0x410ddd9bU, RV64OPCode::opimm32, RV64IntOP73::SRA, ireg_index_of("s11"), ireg_index_of("s11"), 0, 0x10UL); // sraiw   s11,s11,0x10
    DECODER_TEST_INT(0x2485U, RV64OPCode::opimm32, RV64IntOP73::ADD, ireg_index_of("s1"), ireg_index_of("s1"), 0, 1UL); // addiw   s1,s1,1
    DECODER_TEST_INT(0x0004059bU, RV64OPCode::opimm32, RV64IntOP73::ADD, ireg_index_of("a1"), ireg_index_of("s0"), 0, 0L); // sext.w  a1,s0

    // OP
    DECODER_TEST_INT(0x9d3eU, RV64OPCode::op, RV64IntOP73::ADD, ireg_index_of("s10"), ireg_index_of("s10"), ireg_index_of("a5"), 0L); // add     s10,s10,a5
    DECODER_TEST_INT(0x00278433U, RV64OPCode::op, RV64IntOP73::ADD, ireg_index_of("s0"), ireg_index_of("a5"), ireg_index_of("sp"), 0L); // add     s0,a5,sp
    DECODER_TEST_INT(0x8e09U, RV64OPCode::op, RV64IntOP73::SUB, ireg_index_of("a2"), ireg_index_of("a2"), ireg_index_of("a0"), 0L); // sub     a2,a2,a0
    DECODER_TEST_INT(0x41440433U, RV64OPCode::op, RV64IntOP73::SUB, ireg_index_of("s0"), ireg_index_of("s0"), ireg_index_of("s4"), 0L); // sub     s0,s0,s4
    DECODER_TEST_INT(0x8e5dU, RV64OPCode::op, RV64IntOP73::OR, ireg_index_of("a2"), ireg_index_of("a2"), ireg_index_of("a5"), 0L); // or      a2,a2,a5
    
    DECODER_TEST_INT(0x02e787bbU, RV64OPCode::op32, RV64IntOP73::MUL, ireg_index_of("a5"), ireg_index_of("a5"), ireg_index_of("a4"), 0L); // mulw    a5,a5,a4
    DECODER_TEST_INT(0x00fd87bbU, RV64OPCode::op32, RV64IntOP73::ADD, ireg_index_of("a5"), ireg_index_of("s11"), ireg_index_of("a5"), 0L); // addw    a5,s11,a5

    #undef DECODER_TEST_INT

    #define DECODER_TEST_LS(_inst, _opcode, _wid, _rd, _rs1, _rs2, _imms) { \
    RV64InstDecoded dec; \
    assert(isa::decode_rv64(_inst, &dec)); \
    assert(dec.opcode == _opcode); \
    assert(dec.param.loadstore == _wid); \
    assert(dec.rd == _rd); \
    assert(dec.rs1 == _rs1); \
    assert(dec.rs2 == _rs2); \
    assert(dec.rs3 == 0); \
    assert(dec.imm == _imms);}

    // LD
    DECODER_TEST_LS(0x60e6U, RV64OPCode::load, RV64LSWidth::dword, ireg_index_of("ra"), ireg_index_of("sp"), 0, 88); // ld	ra,88(sp)
    DECODER_TEST_LS(0x6906U, RV64OPCode::load, RV64LSWidth::dword, ireg_index_of("s2"), ireg_index_of("sp"), 0, 64); // ld	s2,64(sp)
    DECODER_TEST_LS(0x6906U, RV64OPCode::load, RV64LSWidth::dword, ireg_index_of("s2"), ireg_index_of("sp"), 0, 64); // ld	s2,64(sp)
    DECODER_TEST_LS(0x449cU, RV64OPCode::load, RV64LSWidth::word, ireg_index_of("a5"), ireg_index_of("s1"), 0, 8); // lw	a5,8(s1)
    DECODER_TEST_LS(0x5772U, RV64OPCode::load, RV64LSWidth::word, ireg_index_of("a4"), ireg_index_of("sp"), 0, 60); // lw      a4,60(sp)
    DECODER_TEST_LS(0x70beU, RV64OPCode::load, RV64LSWidth::dword, ireg_index_of("ra"), ireg_index_of("sp"), 0, 488); // ld	ra,488(sp)

    DECODER_TEST_LS(0xd527b783U, RV64OPCode::load, RV64LSWidth::dword, ireg_index_of("a5"), ireg_index_of("a5"), 0, -686); // ld	a5,-686(a5)
    DECODER_TEST_LS(0xc207b583U, RV64OPCode::load, RV64LSWidth::dword, ireg_index_of("a1"), ireg_index_of("a5"), 0, -992); // ld      a1,-992(a5)
    DECODER_TEST_LS(0x7301b703U, RV64OPCode::load, RV64LSWidth::dword, ireg_index_of("a4"), ireg_index_of("gp"), 0, 1840); // ld      a4,1840(gp)
    DECODER_TEST_LS(0x7401a703U, RV64OPCode::load, RV64LSWidth::word, ireg_index_of("a4"), ireg_index_of("gp"), 0, 1856); // lw      a4,1856(gp)
    DECODER_TEST_LS(0x7401a783U, RV64OPCode::load, RV64LSWidth::word, ireg_index_of("a5"), ireg_index_of("gp"), 0, 1856); // lw      a5,1856(gp)
    DECODER_TEST_LS(0xff67d603U, RV64OPCode::load, RV64LSWidth::uharf, ireg_index_of("a2"), ireg_index_of("a5"), 0, -10); // lhu     a2,-10(a5)

    // ST
    DECODER_TEST_LS(0xec86U, RV64OPCode::store, RV64LSWidth::dword, 0, ireg_index_of("sp"), ireg_index_of("ra"), 88); // sd	ra,88(sp)
    DECODER_TEST_LS(0xe022U, RV64OPCode::store, RV64LSWidth::dword, 0, ireg_index_of("sp"), ireg_index_of("s0"), 0); // sd      s0,0(sp)
    DECODER_TEST_LS(0xde3eU, RV64OPCode::store, RV64LSWidth::word, 0, ireg_index_of("sp"), ireg_index_of("a5"), 60); // sw      a5,60(sp)
    DECODER_TEST_LS(0xf786U, RV64OPCode::store, RV64LSWidth::dword, 0, ireg_index_of("sp"), ireg_index_of("ra"), 488); // sd	ra,488(sp)

    DECODER_TEST_LS(0xe70ce30cU, RV64OPCode::store, RV64LSWidth::dword, 0, ireg_index_of("a4"), ireg_index_of("a1"), 0); // sd      a1,0(a4)
    DECODER_TEST_LS(0xfef41c23U, RV64OPCode::store, RV64LSWidth::harf, 0, ireg_index_of("s0"), ireg_index_of("a5"), -8); // sh      a5,-8(s0)

    // LDFP
    DECODER_TEST_LS(0x2422U, RV64OPCode::loadfp, RV64LSWidth::dword, freg_index_of("fs0"), ireg_index_of("sp"), 0, 8); // fld	fs0,8(sp)
    DECODER_TEST_LS(0x242aU, RV64OPCode::loadfp, RV64LSWidth::dword, freg_index_of("fs0"), ireg_index_of("sp"), 0, 136); // fld     fs0,136(sp)

    DECODER_TEST_LS(0x0006a787U, RV64OPCode::loadfp, RV64LSWidth::word, freg_index_of("fa5"), ireg_index_of("a3"), 0, 0); // flw	fa5,0(a3)
    DECODER_TEST_LS(0xd687a407U, RV64OPCode::loadfp, RV64LSWidth::word, freg_index_of("fs0"), ireg_index_of("a5"), 0, -664); // flw	fs0,-664(a5)

    // STFP
    DECODER_TEST_LS(0xa422U, RV64OPCode::storefp, RV64LSWidth::dword, 0, ireg_index_of("sp"), freg_index_of("fs0"), 8); // fsd	fs0,8(sp)

    DECODER_TEST_LS(0xfefbae27U, RV64OPCode::storefp, RV64LSWidth::word, 0, ireg_index_of("s7"), freg_index_of("fa5"), -4); // fsw	fa5,-4(s7)

    #undef DECODER_TEST_LS

    #define DECODER_TEST_BR(_inst, _br, _rs1, _rs2, _imms) { \
    RV64InstDecoded dec; \
    assert(isa::decode_rv64(_inst, &dec)); \
    assert(dec.opcode == RV64OPCode::branch); \
    assert(dec.param.branch == _br); \
    assert(dec.rd == 0); \
    assert(dec.rs1 == _rs1); \
    assert(dec.rs2 == _rs2); \
    assert(dec.rs3 == 0); \
    assert(dec.imm == _imms);}

    // // B
    DECODER_TEST_BR(0xc519U, RV64BranchOP3::BEQ, ireg_index_of("a0"), 0, 0xe); // beqz    a0,0xe(pc)
    DECODER_TEST_BR(0xcca5U, RV64BranchOP3::BEQ, ireg_index_of("s1"), 0, 0x78); // beqz    s1,0x78(pc)
    DECODER_TEST_BR(0x44fb8a63U, RV64BranchOP3::BEQ, ireg_index_of("s7"), ireg_index_of("a5"), 1108); // beq     s7,a5,1074a
    DECODER_TEST_BR(0x16078063U, RV64BranchOP3::BEQ, ireg_index_of("a5"), 0, 352); // bne     beqz    a5,10476
    DECODER_TEST_BR(0xf8f715e3U, RV64BranchOP3::BNE, ireg_index_of("a4"), ireg_index_of("a5"), -118); // bne     a4,a5,-118(pc)
    DECODER_TEST_BR(0xfcf46ce3U, RV64BranchOP3::BLTU, ireg_index_of("s0"), ireg_index_of("a5"), -40); // bltu    s0,a5,-40(pc)
    DECODER_TEST_BR(0x04904a63U, RV64BranchOP3::BLT, 0, ireg_index_of("s1"), 0x54); // bgtz    s1,0x54(pc)

    #undef DECODER_TEST_BR

    #define DECODER_TEST_FP(_inst, _fop, _fwid, _iwid, _funct3, _rd, _rs1, _rs2)  { \
        RV64InstDecoded dec; \
        assert(isa::decode_rv64(_inst, &dec)); \
        assert(dec.opcode == RV64OPCode::opfp); \
        assert((uint32_t)(dec.param.fp.op) == (uint32_t)(_fop)); \
        assert((uint32_t)(dec.param.fp.fwid) == (uint32_t)(_fwid)); \
        assert((uint32_t)(dec.param.fp.iwid) == (uint32_t)(_iwid)); \
        assert((uint32_t)(dec.param.fp.funct3.rm) == (uint32_t)(_funct3)); \
        assert(dec.rd == _rd); \
        assert(dec.rs1 == _rs1); \
        assert(dec.rs2 == _rs2); \
        assert(dec.rs3 == 0); \
    }

    // FP
    // fmul.s	fa5,fa5,fs0
    DECODER_TEST_FP(0x1087f7d3U, RV64FPOP5::MUL, RV64FPWidth2::fword, freg_index_of("fs0"), RV64FPRM3::DYN, freg_index_of("fa5"), freg_index_of("fa5"), freg_index_of("fs0"));
    
    // fcvt.s.w	fa5,a0
    DECODER_TEST_FP(0xd00577d3U, RV64FPOP5::CVTI2F, RV64FPWidth2::fword, RV64FPCVTWidth5::word, RV64FPRM3::DYN, freg_index_of("fa5"), ireg_index_of("a0"), 0);


    #undef DECODER_TEST_FP

    printf("Pass!!!\n");
    return true;
}


}

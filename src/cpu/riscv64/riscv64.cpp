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

#include "riscv64.h"
#include "intop.h"
#include "amoop.h"
#include "fpop.h"

namespace riscv64 {

const string ireg_names[] = {
    "x0",  "ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
    "s0",  "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
    "a6",  "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
    "s8",  "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"
};
const string freg_names[] = {
    "ft0",  "ft1",  "ft2",  "ft3",  "ft4",  "ft5",  "ft6",  "ft7",
    "fs0",  "fs1",  "fa0",  "fa1",  "fa2",  "fa3",  "fa4",  "fa5",
    "fa6",  "fa7",  "fs2",  "fs3",  "fs4",  "fs5",  "fs6",  "fs7",
    "fs8",  "fs9",  "fs10", "fs11", "ft8",  "ft9",  "ft10", "ft11"
};

string get_ireg_name(RegIdxT index) {
    return ireg_names[index];
}

string get_freg_name(RegIdxT index) {
    return freg_names[index];
}

template<typename T>
inline T get_bits_at(T n, uint32_t offset, uint32_t len) {
    return (((n) >> (offset)) & ((((T)1)<<(len)) - ((T)1)));
}

inline uint64_t get_imm_I(InstT inst) {
    uint64_t ret = (get_bits_at(inst, 20, 12));
    if(inst & (1<<31)) ret = ret | 0xfffffffffffff000UL;
    return ret;
}
inline uint64_t get_imm_S(InstT inst) {
    uint64_t ret = (get_bits_at(inst, 7, 5)) | (get_bits_at(inst, 25, 7) << 5);
    if(inst & (1<<31)) ret = ret | 0xfffffffffffff000UL;
    return ret;
}
inline uint64_t get_imm_B(InstT inst) {
    uint64_t ret = (get_bits_at(inst, 8, 4) << 1) | (get_bits_at(inst, 25, 6) << 5) | (get_bits_at(inst, 7, 1) << 11);
    if(inst & (1<<31)) ret |= 0xfffffffffffff000UL;
    return ret;
}
inline uint64_t get_imm_U(InstT inst) {
    uint64_t ret = (inst & 0xfffff000U);
    if(inst & (1<<31)) ret |= 0xffffffff00000000UL;
    return ret;
}
inline uint64_t get_imm_J(InstT inst) {
    uint64_t ret = (get_bits_at(inst, 21, 10) << 1) | (get_bits_at(inst, 20, 1) << 11) | (inst & 0xFF000);
    if(inst & (1<<31)) ret = ret | 0xfffffffffff00000UL;
    return ret;
}

inline uint32_t get_opcode(InstT inst) {
    return get_bits_at(inst, 0, 7);
}
inline uint32_t get_funct3(InstT inst) {
    return get_bits_at(inst, 12, 3);
}
inline uint32_t get_funct7(InstT inst) {
    return get_bits_at(inst, 25, 7);
}
inline uint32_t get_rd(InstT inst) {
    return get_bits_at(inst, 7, 5);
}
inline uint32_t get_rs1(InstT inst) {
    return get_bits_at(inst, 15, 5);
}
inline uint32_t get_rs2(InstT inst) {
    return get_bits_at(inst, 20, 5);
}
inline uint32_t get_rs3(InstT inst) {
    return get_bits_at(inst, 27, 5);
}

/**
 * I Extension
 */
inline bool _decode_load(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->imm = get_imm_I(inst);
    instinfo->desttype = DestType::IREG;
    instinfo->srctype1 = SrcType::IREG;
    instinfo->exetype = ExeType::LOAD;
    instinfo->exeop = static_cast<ExeOPType>(get_funct3(inst));
    return (instinfo->exeop <= static_cast<ExeOPType>(LOADOPType::LWU));
}

/**
 * I Extension
 */
inline bool _decode_store(InstT inst, InstInfo *instinfo) {
    instinfo->rs1 = get_rs1(inst);
    instinfo->rs2 = get_rs2(inst);
    instinfo->imm = get_imm_S(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::IREG;
    instinfo->exetype = ExeType::STORE;
    instinfo->exeop = static_cast<ExeOPType>(get_funct3(inst));
    return (instinfo->exeop <= static_cast<ExeOPType>(STOREOPType::SD));
}

/**
 * I Extension
 */
inline bool _decode_lui(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->imm = get_imm_U(inst);
    instinfo->srctype1 = SrcType::IMM;
    instinfo->desttype = DestType::IREG;
    instinfo->exetype = ExeType::ALU;
    instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADD);
    return true;
}

/**
 * I Extension
 */
inline bool _decode_auipc(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->imm = get_imm_U(inst);
    instinfo->srctype1 = SrcType::PC;
    instinfo->srctype2 = SrcType::IMM;
    instinfo->desttype = DestType::IREG;
    instinfo->exetype = ExeType::JUMP;
    instinfo->exeop = static_cast<ExeOPType>(JUMPOPType::AUIPC);
    return true;
}

/**
 * I Extension
 */
inline bool _decode_jal(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->imm = get_imm_J(inst);
    instinfo->srctype1 = SrcType::PC;
    instinfo->srctype2 = SrcType::IMM;
    instinfo->desttype = DestType::IREG;
    instinfo->exetype = ExeType::JUMP;
    instinfo->exeop = static_cast<ExeOPType>(JUMPOPType::JAL);
    return true;
}

/**
 * I Extension
 */
inline bool _decode_jalr(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->imm = get_imm_I(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::IMM;
    instinfo->desttype = DestType::IREG;
    instinfo->exetype = ExeType::JUMP;
    instinfo->exeop = static_cast<ExeOPType>(JUMPOPType::JALR);
    return true;
}

/**
 * I Extension
 */
inline bool _decode_branch(InstT inst, InstInfo *instinfo) {
    instinfo->rs1 = get_rs1(inst);
    instinfo->rs2 = get_rs2(inst);
    instinfo->imm = get_imm_B(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::IREG;
    instinfo->exetype = ExeType::BRANCH;
    instinfo->exeop = static_cast<ExeOPType>(get_funct3(inst));
    return (instinfo->exeop <= static_cast<ExeOPType>(BRANCHOPType::BGEU) && instinfo->exeop != 2 && instinfo->exeop != 3);
}

/**
 * I Extension
 */
inline bool _decode_opimm(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->imm = get_imm_I(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::IMM;
    instinfo->desttype = DestType::IREG;
    instinfo->exetype = ExeType::ALU;
    switch (get_funct3(inst) | (get_funct7(inst) << 3))
    {
    case 0b0000000000: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADD); break;
    case 0b0100000000: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SUB); break;
    case 0b0000000001: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SLL); break;
    case 0b0000000010: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SLT); break;
    case 0b0000000011: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SLTU); break;
    case 0b0000000100: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::XOR); break;
    case 0b0000000101: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SRL); break;
    case 0b0100000101: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SRA); break;
    case 0b0000000110: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::OR); break;
    case 0b0000000111: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::AND); break;
    default: return false;
    }
    return true;
}

/**
 * I Extension
 */
inline bool _decode_opimm32(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->imm = get_imm_I(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::IMM;
    instinfo->desttype = DestType::IREG;
    instinfo->exetype = ExeType::ALU;
    switch (get_funct3(inst) | (get_funct7(inst) << 3))
    {
    case 0b0000000000: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADDW); break;
    case 0b0100000000: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SUBW); break;
    case 0b0000000001: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SLLW); break;
    case 0b0000000101: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SRLW); break;
    case 0b0100000101: instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SRAW); break;
    default: return false;
    }
    return true;
}

/**
 * I Extension
 * M Extension
 */
inline bool _decode_op(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->rs2 = get_rs2(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::IREG;
    instinfo->desttype = DestType::IREG;
    switch (get_funct3(inst) | (get_funct7(inst) << 3))
    {
    #define GEN_ITEM(bits, exu, exop) case bits: instinfo->exetype = exu; instinfo->exeop = static_cast<ExeOPType>(exop); break
    GEN_ITEM(0b0000000000, ExeType::ALU, ALUOPType::ADD);
    GEN_ITEM(0b0100000000, ExeType::ALU, ALUOPType::SUB);
    GEN_ITEM(0b0000000001, ExeType::ALU, ALUOPType::SLL);
    GEN_ITEM(0b0000000010, ExeType::ALU, ALUOPType::SLT);
    GEN_ITEM(0b0000000011, ExeType::ALU, ALUOPType::SLTU);
    GEN_ITEM(0b0000000100, ExeType::ALU, ALUOPType::XOR);
    GEN_ITEM(0b0000000101, ExeType::ALU, ALUOPType::SRL);
    GEN_ITEM(0b0100000101, ExeType::ALU, ALUOPType::SRA);
    GEN_ITEM(0b0000000110, ExeType::ALU, ALUOPType::OR);
    GEN_ITEM(0b0000000111, ExeType::ALU, ALUOPType::AND);
    GEN_ITEM(0b0000001000, ExeType::MUL, MULOPType::MUL);
    GEN_ITEM(0b0000001001, ExeType::MUL, MULOPType::MULH);
    GEN_ITEM(0b0000001010, ExeType::MUL, MULOPType::MULHSU);
    GEN_ITEM(0b0000001011, ExeType::MUL, MULOPType::MULHU);
    GEN_ITEM(0b0000001100, ExeType::DIV, DIVOPType::DIV);
    GEN_ITEM(0b0000001101, ExeType::DIV, DIVOPType::DIVU);
    GEN_ITEM(0b0000001110, ExeType::DIV, DIVOPType::REM);
    GEN_ITEM(0b0000001111, ExeType::DIV, DIVOPType::REMU);
    default: return false;
    #undef GEN_ITEM
    }
    return true;
}

/**
 * I Extension
 * M Extension
 */
inline bool _decode_op32(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->rs2 = get_rs2(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::IREG;
    instinfo->desttype = DestType::IREG;
    switch (get_funct3(inst) | (get_funct7(inst) << 3))
    {
    #define GEN_ITEM(bits, exu, exop) case bits: instinfo->exetype = exu; instinfo->exeop = static_cast<ExeOPType>(exop); break
    GEN_ITEM(0b0000000000, ExeType::ALU, ALUOPType::ADDW);
    GEN_ITEM(0b0100000000, ExeType::ALU, ALUOPType::SUBW);
    GEN_ITEM(0b0000000001, ExeType::ALU, ALUOPType::SLLW);
    GEN_ITEM(0b0000000101, ExeType::ALU, ALUOPType::SRLW);
    GEN_ITEM(0b0100000101, ExeType::ALU, ALUOPType::SRAW);
    GEN_ITEM(0b0000001000, ExeType::MUL, MULOPType::MULW);
    GEN_ITEM(0b0000001100, ExeType::DIV, DIVOPType::DIVW);
    GEN_ITEM(0b0000001101, ExeType::DIV, DIVOPType::DIVUW);
    GEN_ITEM(0b0000001110, ExeType::DIV, DIVOPType::REMW);
    GEN_ITEM(0b0000001111, ExeType::DIV, DIVOPType::REMUW);
    default: return false;
    #undef GEN_ITEM
    }
    return true;
}

/**
 * A Extension
 */
inline bool _decode_amo(InstT inst, InstInfo *instinfo) {
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->rs2 = get_rs2(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::IREG;
    instinfo->desttype = DestType::IREG;
    instinfo->exetype = ExeType::AMO;
    switch ((get_funct7(inst) >> 2) | (get_funct3(inst) << 5))
    {
    case static_cast<ExeOPType>(AMOOPType::ADD_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::ADD_W); break;
    case static_cast<ExeOPType>(AMOOPType::SWAP_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::SWAP_W); break;
    case static_cast<ExeOPType>(AMOOPType::LR_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::LR_W); break;
    case static_cast<ExeOPType>(AMOOPType::SC_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::SC_W); break;
    case static_cast<ExeOPType>(AMOOPType::XOR_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::XOR_W); break;
    case static_cast<ExeOPType>(AMOOPType::AND_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::AND_W); break;
    case static_cast<ExeOPType>(AMOOPType::OR_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::OR_W); break;
    case static_cast<ExeOPType>(AMOOPType::MIN_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::MIN_W); break;
    case static_cast<ExeOPType>(AMOOPType::MAX_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::MAX_W); break;
    case static_cast<ExeOPType>(AMOOPType::MINU_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::MINU_W); break;
    case static_cast<ExeOPType>(AMOOPType::MAXU_W) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::MAXU_W); break;
    case static_cast<ExeOPType>(AMOOPType::ADD_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::ADD_D); break;
    case static_cast<ExeOPType>(AMOOPType::SWAP_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::SWAP_D); break;
    case static_cast<ExeOPType>(AMOOPType::LR_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::LR_D); break;
    case static_cast<ExeOPType>(AMOOPType::SC_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::SC_D); break;
    case static_cast<ExeOPType>(AMOOPType::XOR_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::XOR_D); break;
    case static_cast<ExeOPType>(AMOOPType::AND_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::AND_D); break;
    case static_cast<ExeOPType>(AMOOPType::OR_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::OR_D); break;
    case static_cast<ExeOPType>(AMOOPType::MIN_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::MIN_D); break;
    case static_cast<ExeOPType>(AMOOPType::MAX_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::MAX_D); break;
    case static_cast<ExeOPType>(AMOOPType::MINU_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::MINU_D); break;
    case static_cast<ExeOPType>(AMOOPType::MAXU_D) : instinfo->exeop = static_cast<ExeOPType>(AMOOPType::MAXU_D); break;
    default: return false;
    }
    return true;
}

/**
 * I Extension
 * Zicsr Extension
 */
bool _decode_system(InstT inst, InstInfo *instinfo) {
    instinfo->exetype = ExeType::CSR;
    switch (inst)
    {
    case 0x00000073U: instinfo->exeop = static_cast<ExeOPType>(CSROPType::ECALL); return true;
    case 0x00100073U: instinfo->exeop = static_cast<ExeOPType>(CSROPType::EBREAK); return true;
    case 0x10200073U: instinfo->exeop = static_cast<ExeOPType>(CSROPType::SRET); return true;
    case 0x30200073U: instinfo->exeop = static_cast<ExeOPType>(CSROPType::MRET); return true;
    case 0x10500073U: instinfo->exeop = static_cast<ExeOPType>(CSROPType::WFI); return true;
    }

    if (get_funct7(inst) == 0b0001001) { // sfence.vma
        instinfo->exetype = ExeType::FENCE;
        instinfo->exeop = static_cast<ExeOPType>(FENCEOPType::SFENCE);
        instinfo->rs1 = get_rs1(inst);
        instinfo->rs2 = get_rs2(inst);
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IREG;
        if (instinfo->rs1) instinfo->exeop |= FENCEOP_SFENCE_VA_VALID_MASK;
        if (instinfo->rs2) instinfo->exeop |= FENCEOP_SFENCE_ASID_VALID_MASK;
        return true;
    }

    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->imm = get_rs1(inst);
    instinfo->desttype = DestType::IREG;
    instinfo->srctype1 = (get_funct3(inst) > 4)?SrcType::IMM:SrcType::IREG;
    instinfo->exeop = get_funct7(inst) << 8;
    switch (get_funct3(inst))
    {
    case 0b001: instinfo->exeop |= static_cast<ExeOPType>(CSROPType::RW); return true;
    case 0b010: instinfo->exeop |= static_cast<ExeOPType>(CSROPType::RS); return true;
    case 0b011: instinfo->exeop |= static_cast<ExeOPType>(CSROPType::RC); return true;
    case 0b101: instinfo->exeop |= static_cast<ExeOPType>(CSROPType::RWI); return true;
    case 0b110: instinfo->exeop |= static_cast<ExeOPType>(CSROPType::RSI); return true;
    case 0b111: instinfo->exeop |= static_cast<ExeOPType>(CSROPType::RCI); return true;
    }
    return false;
}

/**
 * I Extension
 * Zifencei Extension
 * Zihintpause Extension
 */
bool _decode_miscmem(InstT inst, InstInfo *instinfo) {
    instinfo->exetype = ExeType::FENCE;
    switch (inst)
    {
    case 0x8330000fU: instinfo->exeop = static_cast<ExeOPType>(FENCEOPType::FENCETSO); return true;
    case 0x0100000fU: instinfo->exeop = static_cast<ExeOPType>(FENCEOPType::PAUSE); return true;
    }

    switch (get_funct3(inst))
    {
    case 0b000:
        instinfo->exeop = static_cast<ExeOPType>(FENCEOPType::FENCE) | (((inst >> 20) & 0xff) << 8);
        return true;
    case 0b001:
        instinfo->exeop = static_cast<ExeOPType>(FENCEOPType::FENCEI);
        return true;
    }
    
}

/**
 * F/D/Q/Zfh Extension
 */
bool _decode_loadfp(InstT inst, InstInfo *instinfo) {
    instinfo->exetype = ExeType::LOAD;
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->imm = get_imm_I(inst);
    instinfo->desttype = DestType::FREG;
    instinfo->srctype1 = SrcType::IREG;
    switch (get_funct3(inst))
    {
    case 0b001: instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LH); return true;
    case 0b010: instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LW); return true;
    case 0b011: instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LD); return true;
    case 0b100: instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LQ); return true;
    }
    return false;
}

/**
 * F/D/Q/Zfh Extension
 */
bool _decode_storefp(InstT inst, InstInfo *instinfo) {
    instinfo->exetype = ExeType::STORE;
    instinfo->rs1 = get_rs1(inst);
    instinfo->rs2 = get_rs2(inst);
    instinfo->imm = get_imm_S(inst);
    instinfo->srctype1 = SrcType::IREG;
    instinfo->srctype2 = SrcType::FREG;
    switch (get_funct3(inst))
    {
    case 0b001: instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SH); return true;
    case 0b010: instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SW); return true;
    case 0b011: instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SD); return true;
    case 0b100: instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SQ); return true;
    }
    return false;
}

/**
 * F/D/Q/Zfh Extension
 */
bool _decode_fpop(InstT inst, InstInfo *instinfo) {
    ExeOPType fpu_optype = fp_generate_optype(inst);
    instinfo->exetype = fp_detect_exe_type(fp_extract_optype_op5(fpu_optype));
    instinfo->exeop = fpu_optype;
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->rs2 = get_rs2(inst);
    switch (instinfo->exetype)
    {
    case ExeType::FALU:
    case ExeType::FMUL:
    case ExeType::FDIV:
        instinfo->srctype1 = SrcType::FREG;
        instinfo->srctype2 = SrcType::FREG;
        instinfo->desttype = DestType::FREG;
        return true;
    case ExeType::FCVT:
    case ExeType::FSQRT:
        instinfo->srctype1 = SrcType::FREG;
        instinfo->desttype = DestType::FREG;
        return true;
    case ExeType::FCMP:
        instinfo->srctype1 = SrcType::FREG;
        instinfo->srctype2 = SrcType::FREG;
        instinfo->desttype = DestType::IREG;
        return true;
    case ExeType::I2F:
        instinfo->srctype1 = SrcType::IREG;
        instinfo->desttype = DestType::FREG;
        return true;
    case ExeType::F2I:
        instinfo->srctype1 = SrcType::FREG;
        instinfo->desttype = DestType::IREG;
        return true;
    }
    return false;
}

/**
 * F/D/Q/Zfh Extension
 */
bool _decode_fma(InstT inst, InstInfo *instinfo) {
    instinfo->exetype = ExeType::FMA;
    instinfo->exeop = fma_generate_optype(inst);
    instinfo->rd = get_rd(inst);
    instinfo->rs1 = get_rs1(inst);
    instinfo->rs2 = get_rs2(inst);
    instinfo->rs3 = get_rs3(inst);
    instinfo->srctype1 = SrcType::FREG;
    instinfo->srctype2 = SrcType::FREG;
    instinfo->srctype3 = SrcType::FREG;
    instinfo->desttype = DestType::FREG;
    return true;
}


bool decode_inst(InstCT inst, InstInfo *instinfo) {

    /* C Extension */
    if ((inst & 3) != 3) return decode_rvc_inst(inst, instinfo);

    memset(instinfo, 0, sizeof(InstInfo));

    OPCode opcode = static_cast<OPCode>(get_opcode(inst));
    instinfo->opcode = opcode;

    switch (opcode) {
    case OPCode::load:     return _decode_load(inst, instinfo);
    case OPCode::store:    return _decode_store(inst, instinfo);
    case OPCode::lui:      return _decode_lui(inst, instinfo);
    case OPCode::opimm:    return _decode_opimm(inst, instinfo);
    case OPCode::opimm32:  return _decode_opimm32(inst, instinfo);
    case OPCode::op:       return _decode_op(inst, instinfo);
    case OPCode::op32:     return _decode_op32(inst, instinfo);
    case OPCode::auipc:    return _decode_auipc(inst, instinfo);
    case OPCode::jal:      return _decode_jal(inst, instinfo);
    case OPCode::jalr:     return _decode_jalr(inst, instinfo);
    case OPCode::amo:      return _decode_amo(inst, instinfo);
    case OPCode::miscmem:  return _decode_miscmem(inst, instinfo);
    case OPCode::system:   return _decode_system(inst, instinfo);
    case OPCode::loadfp:   return _decode_loadfp(inst, instinfo);
    case OPCode::storefp:  return _decode_storefp(inst, instinfo);
    case OPCode::opfp:     return _decode_fpop(inst, instinfo);
    case OPCode::madd:
    case OPCode::msub:
    case OPCode::nmsub:
    case OPCode::nmadd:    return _decode_fma(inst, instinfo);
    default:               return false;
    }

}






inline bool _decode_rvc_op0(InstCT inst, InstInfo *instinfo) {
    uint8_t funct3 = (inst >> 13) & 0x7;

    uint64_t uimm = 0;

    if (funct3 < 0b100) {
        instinfo->rd = 8 + ((inst >> 2) & 0x7);
        instinfo->rs1 = 8 + ((inst >> 7) & 0x7);
        instinfo->srctype1 = SrcType::IREG;
    } else {
        instinfo->rs2 = 8 + ((inst >> 2) & 0x7);
        instinfo->rs1 = 8 + ((inst >> 7) & 0x7);
        instinfo->srctype1 = SrcType::IREG;
    }

    switch (funct3)
    {
    case 0b000: // c.addi4spn
        instinfo->opcode = OPCode::opimm;
        instinfo->rs1 = 2; // sp
        uimm |= (((inst >> 5) & 1) << 3);
        uimm |= (((inst >> 6) & 1) << 2);
        uimm |= (((inst >> 7) & 0xf) << 6);
        uimm |= (((inst >> 11) & 3) << 4);
        instinfo->imm = uimm;
        instinfo->desttype = DestType::IREG;
        instinfo->exetype = ExeType::ALU;
        instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADD);
        return true;
    case 0b001: // c.fld
        instinfo->opcode = OPCode::loadfp;
        uimm |= (((inst >> 5) & 0x3) << 6);
        uimm |= (((inst >> 10) & 0x7) << 3);
        instinfo->imm = uimm;
        instinfo->desttype = DestType::FREG;
        instinfo->exetype = ExeType::LOAD;
        instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LD);
        return true;
    case 0b010: // c.lw
        instinfo->opcode = OPCode::load;
        uimm |= (((inst >> 5) & 1) << 6);
        uimm |= (((inst >> 6) & 1) << 2);
        uimm |= (((inst >> 10) & 0x7) << 3);
        instinfo->imm = uimm;
        instinfo->desttype = DestType::IREG;
        instinfo->exetype = ExeType::LOAD;
        instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LW);
        return true;
    case 0b011: // c.ld
        instinfo->opcode = OPCode::load;
        uimm |= (((inst >> 5) & 0x3) << 6);
        uimm |= (((inst >> 10) & 0x7) << 3);
        instinfo->imm = uimm;
        instinfo->desttype = DestType::IREG;
        instinfo->exetype = ExeType::LOAD;
        instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LD);
        return true;
    case 0b101: // c.fsd
        instinfo->opcode = OPCode::storefp;
        uimm |= (((inst >> 5) & 0x3) << 6);
        uimm |= (((inst >> 10) & 0x7) << 3);
        instinfo->imm = uimm;
        instinfo->srctype2 = SrcType::FREG;
        instinfo->exetype = ExeType::STORE;
        instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SD);
        return true;
    case 0b110: // c.sw
        instinfo->opcode = OPCode::store;
        uimm |= (((inst >> 5) & 1) << 6);
        uimm |= (((inst >> 6) & 1) << 2);
        uimm |= (((inst >> 10) & 0x7) << 3);
        instinfo->imm = uimm;
        instinfo->srctype2 = SrcType::IREG;
        instinfo->exetype = ExeType::STORE;
        instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SW);
        return true;
    case 0b111: // c.sd
        instinfo->opcode = OPCode::store;
        uimm |= (((inst >> 5) & 0x3) << 6);
        uimm |= (((inst >> 10) & 0x7) << 3);
        instinfo->imm = uimm;
        instinfo->srctype2 = SrcType::IREG;
        instinfo->exetype = ExeType::STORE;
        instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SD);
        return true;
    default:
        break;
    }

    return false;
}

inline bool _decode_rvc_op1(InstCT inst, InstInfo *instinfo) {
    uint8_t funct3 = (inst >> 13) & 0x7;

    uint64_t uimm = 0;
    uint64_t aluf1 = (inst >> 10) & 3;
    uint64_t aluf2 = ((inst >> 5) & 3) | (((inst >> 12) & 1) << 2);

    switch (funct3)
    {
    case 0b000: // c.addi
        instinfo->opcode = OPCode::opimm;
        instinfo->rd = (inst >> 7) & 0x1f;
        instinfo->rs1 = (inst >> 7) & 0x1f;
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IMM;
        instinfo->desttype = DestType::IREG;
        uimm = (inst >> 2) & 0x1f;
        if ((inst >> 12) & 1) {
            uimm |= 0xffffffffffffffe0UL;
        }
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::ALU;
        instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADD);
        return true;
    case 0b001: // C.ADDIW
        instinfo->opcode = OPCode::opimm32;
        instinfo->rd = (inst >> 7) & 0x1f;
        instinfo->rs1 = (inst >> 7) & 0x1f;
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IMM;
        instinfo->desttype = DestType::IREG;
        uimm = (inst >> 2) & 0x1f;
        if ((inst >> 12) & 1) {
            uimm |= 0xffffffffffffffe0UL;
        }
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::ALU;
        instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADDW);
        return true;
    case 0b010: // c.li
        instinfo->opcode = OPCode::opimm;
        instinfo->rd = (inst >> 7) & 0x1f;
        instinfo->srctype1 = SrcType::IMM;
        instinfo->desttype = DestType::IREG;
        uimm = (inst >> 2) & 0x1f;
        if ((inst >> 12) & 1) {
            uimm |= 0xffffffffffffffe0UL;
        }
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::ALU;
        instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADD);
        return true;
    case 0b011: // c.addi16sp/c.lui
        if (((inst >> 7) & 0x1f) == 2) { // c.addi16sp
            instinfo->opcode = OPCode::opimm;
            instinfo->rd = 2;
            instinfo->rs1 = 2;
            instinfo->srctype1 = SrcType::IREG;
            instinfo->srctype2 = SrcType::IMM;
            uimm |= (((inst >> 6) & 1) << 4);
            uimm |= (((inst >> 2) & 1) << 5);
            uimm |= (((inst >> 5) & 1) << 6);
            uimm |= (((inst >> 3) & 3) << 7);
            if((inst >> 12) & 1) uimm |= 0xfffffffffffffe00UL;
        } else { // c.lui
            instinfo->opcode = OPCode::lui;
            instinfo->rd = (inst >> 7) & 0x1f;
            instinfo->srctype1 = SrcType::IMM;
            uimm = ((inst >> 2) & 0x1f) << 12;
            if ((inst >> 12) & 1) {
                uimm |= 0xfffffffffffe0000UL;
            }
        }
        instinfo->desttype = DestType::IREG;
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::ALU;
        instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADD);
        return true;
    case 0b100: // c.srli, c.srai, c.andi, c.sub, c.xor, c.or, c.and
        instinfo->exetype = ExeType::ALU;
        instinfo->rs1 = 8 + ((inst >> 7) & 0x7);
        instinfo->rd = 8 + ((inst >> 7) & 0x7);
        instinfo->srctype1 = SrcType::IREG;
        instinfo->desttype = DestType::IREG;
        switch (aluf1)
        {
        case 0: // c.srli
            instinfo->opcode = OPCode::opimm;
            uimm |= ((inst >> 2) & 0x1f);
            uimm |= (((inst >> 12) & 1) << 5);
            instinfo->imm = uimm;
            instinfo->srctype2 = SrcType::IMM;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SRL);
            return true;
        case 1: // c.srai
            instinfo->opcode = OPCode::opimm;
            uimm |= ((inst >> 2) & 0x1f);
            uimm |= (((inst >> 12) & 1) << 5);
            instinfo->imm = uimm;
            instinfo->srctype2 = SrcType::IMM;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SRA);
            return true;
        case 2: // c.andi
            instinfo->opcode = OPCode::opimm;
            uimm |= ((inst >> 2) & 0x1f);
            if((inst >> 12) & 1) uimm |= 0xffffffffffffffe0UL;
            instinfo->imm = uimm;
            instinfo->srctype2 = SrcType::IMM;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::AND);
            return true;
        }
        instinfo->rs2 = 8 + ((inst >> 2) & 0x7);
        instinfo->srctype2 = SrcType::IREG;
        switch (aluf2)
        {
        case 0b000: // c.sub
            instinfo->opcode = OPCode::op;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SUB);
            return true;
        case 0b001: // c.xor
            instinfo->opcode = OPCode::op;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::XOR);
            return true;
        case 0b010: // c.or
            instinfo->opcode = OPCode::op;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::OR);
            return true;
        case 0b011: // c.and
            instinfo->opcode = OPCode::op;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::AND);
            return true;
        case 0b100: // c.subw
            instinfo->opcode = OPCode::op32;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SUBW);
            return true;
        case 0b101: // c.addw
            instinfo->opcode = OPCode::op32;
            instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADDW);
            return true;
        }
        break;
    case 0b101: // c.j
        uimm |= (((inst >> 2) & 1) << 5);
        uimm |= (((inst >> 3) & 7) << 1);
        uimm |= (((inst >> 6) & 1) << 7);
        uimm |= (((inst >> 7) & 1) << 6);
        uimm |= (((inst >> 8) & 1) << 10);
        uimm |= (((inst >> 9) & 3) << 8);
        uimm |= (((inst >> 11) & 1) << 4);
        if((inst >> 12) & 1) uimm |= 0xfffffffffffff800UL;
        instinfo->imm = uimm;
        instinfo->opcode = OPCode::jal;
        instinfo->srctype1 = SrcType::PC;
        instinfo->srctype2 = SrcType::IMM;
        instinfo->exetype = ExeType::JUMP;
        instinfo->exeop = static_cast<ExeOPType>(JUMPOPType::JAL);
        return true;
    case 0b110: // c.beqz
        uimm |= (((inst >> 2) & 1) << 5);
        uimm |= (((inst >> 3) & 3) << 1);
        uimm |= (((inst >> 5) & 3) << 6);
        uimm |= (((inst >> 10) & 3) << 3);
        if((inst >> 12) & 1) uimm |= 0xffffffffffffff00UL;
        instinfo->imm = uimm;
        instinfo->opcode = OPCode::branch;
        instinfo->srctype1 = SrcType::IREG;
        instinfo->rs1 = 8 + ((inst >> 7) & 0x7);
        instinfo->exetype = ExeType::BRANCH;
        instinfo->exeop = static_cast<ExeOPType>(BRANCHOPType::BEQ);
        return true;
    case 0b111: // c.bnez
        uimm |= (((inst >> 2) & 1) << 5);
        uimm |= (((inst >> 3) & 3) << 1);
        uimm |= (((inst >> 5) & 3) << 6);
        uimm |= (((inst >> 10) & 3) << 3);
        if((inst >> 12) & 1) uimm |= 0xffffffffffffff00UL;
        instinfo->imm = uimm;
        instinfo->opcode = OPCode::branch;
        instinfo->srctype1 = SrcType::IREG;
        instinfo->rs1 = 8 + ((inst >> 7) & 0x7);
        instinfo->exetype = ExeType::BRANCH;
        instinfo->exeop = static_cast<ExeOPType>(BRANCHOPType::BNE);
        return true;
    }

    return false;
}

inline bool _decode_rvc_op2(InstCT inst, InstInfo *instinfo) {
    uint8_t funct3 = (inst >> 13) & 0x7;

    uint64_t uimm = 0;
    uint8_t rs1_bits = ((inst >> 7) & 0x1f);
    uint8_t rs2_bits = ((inst >> 2) & 0x1f);

    switch (funct3)
    {
    case 0b000: // c.slli
        instinfo->opcode = OPCode::opimm;
        instinfo->rd = rs1_bits;
        instinfo->rs1 = rs1_bits;
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IMM;
        instinfo->desttype = DestType::IREG;
        uimm = (inst >> 2) & 0x1f;
        uimm |= (((inst >> 12) & 1) << 5);
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::ALU;
        instinfo->exeop = static_cast<ExeOPType>(ALUOPType::SLL);
        return true;
    case 0b001: // c.fldsp
        instinfo->opcode = OPCode::loadfp;
        instinfo->rd = rs1_bits;
        instinfo->rs1 = 2; // sp
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IMM;
        instinfo->desttype = DestType::FREG;
        uimm |= (((inst >> 2) & 7) << 6);
        uimm |= (((inst >> 5) & 3) << 3);
        uimm |= (((inst >> 12) & 1) << 5);
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::LOAD;
        instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LD);
        return true;
    case 0b010: // c.lwsp
        instinfo->opcode = OPCode::load;
        instinfo->rd = rs1_bits;
        instinfo->rs1 = 2; // sp
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IMM;
        instinfo->desttype = DestType::IREG;
        uimm |= (((inst >> 2) & 3) << 6);
        uimm |= (((inst >> 4) & 7) << 2);
        uimm |= (((inst >> 12) & 1) << 5);
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::LOAD;
        instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LW);
        return true;
    case 0b011: // c.ldsp
        instinfo->opcode = OPCode::load;
        instinfo->rd = rs1_bits;
        instinfo->rs1 = 2; // sp
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IMM;
        instinfo->desttype = DestType::IREG;
        uimm |= (((inst >> 2) & 7) << 6);
        uimm |= (((inst >> 5) & 3) << 3);
        uimm |= (((inst >> 12) & 1) << 5);
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::LOAD;
        instinfo->exeop = static_cast<ExeOPType>(LOADOPType::LD);
        return true;
    case 0b100: // c.jr, c.mv, c.ebreak, c.jalr, c.add
        if ((inst >> 12) & 1) {
            if (rs2_bits && rs1_bits) { // c.add
                instinfo->opcode = OPCode::op;
                instinfo->rd = rs1_bits;
                instinfo->rs1 = rs1_bits;
                instinfo->rs2 = rs2_bits;
                instinfo->srctype1 = SrcType::IREG;
                instinfo->srctype2 = SrcType::IREG;
                instinfo->desttype = DestType::IREG;
                instinfo->exetype = ExeType::ALU;
                instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADD);
                return true;
            } else if(rs1_bits && !rs2_bits) { // c.jalr
                instinfo->opcode = OPCode::jalr;
                instinfo->rd = 1;
                instinfo->rs1 = rs1_bits;
                instinfo->srctype1 = SrcType::IREG;
                instinfo->desttype = DestType::IREG;
                instinfo->exetype = ExeType::JUMP;
                instinfo->exeop = static_cast<ExeOPType>(JUMPOPType::JALR);
                return true;
            } else if(!rs1_bits && !rs2_bits) { // e.ebreak
                instinfo->opcode = OPCode::system;
                instinfo->exetype = ExeType::CSR;
                instinfo->exeop = static_cast<ExeOPType>(CSROPType::EBREAK);
                return true;
            }
        } else {
            if (rs2_bits && rs1_bits) { // c.mv
                instinfo->opcode = OPCode::opimm;
                instinfo->rd = rs1_bits;
                instinfo->rs1 = rs1_bits;
                instinfo->srctype1 = SrcType::IREG;
                instinfo->desttype = DestType::IREG;
                instinfo->exetype = ExeType::ALU;
                instinfo->exeop = static_cast<ExeOPType>(ALUOPType::ADD);
                return true;
            } else if(rs1_bits && !rs2_bits) { // c.jr
                instinfo->opcode = OPCode::jalr;
                instinfo->rs1 = rs1_bits;
                instinfo->srctype1 = SrcType::IREG;
                instinfo->exetype = ExeType::JUMP;
                instinfo->exeop = static_cast<ExeOPType>(JUMPOPType::JALR);
                return true;
            }
        }
        break;
    case 0b101: // c.fsdsp
        instinfo->opcode = OPCode::storefp;
        instinfo->rs1 = 2; // sp
        instinfo->rs2 = rs2_bits;
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::FREG;
        uimm |= (((inst >> 7) & 7) << 6);
        uimm |= (((inst >> 10) & 7) << 3);
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::STORE;
        instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SD);
        return true;
    case 0b110: // c.swsp
        instinfo->opcode = OPCode::store;
        instinfo->rs1 = 2; // sp
        instinfo->rs2 = rs2_bits;
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IREG;
        uimm |= (((inst >> 7) & 3) << 6);
        uimm |= (((inst >> 9) & 0xf) << 2);
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::STORE;
        instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SW);
        return true;
    case 0b111: // c.sdsp
        instinfo->opcode = OPCode::storefp;
        instinfo->rs1 = 2; // sp
        instinfo->rs2 = rs2_bits;
        instinfo->srctype1 = SrcType::IREG;
        instinfo->srctype2 = SrcType::IREG;
        uimm |= (((inst >> 7) & 7) << 6);
        uimm |= (((inst >> 10) & 7) << 3);
        instinfo->imm = uimm;
        instinfo->exetype = ExeType::STORE;
        instinfo->exeop = static_cast<ExeOPType>(STOREOPType::SD);
        return true;
    }

    return false;
}

bool decode_rvc_inst(InstCT inst, InstInfo *instinfo) {
    uint8_t opcode = inst & 0x3;

    memset(instinfo, 0, sizeof(InstInfo));
    instinfo->rawinst = inst;
    if ((inst & 0x3) != 0) return false;
    instinfo->isrvc = true;

    switch (opcode)
    {
    case 0: return _decode_rvc_op0(inst, instinfo);
    case 1: return _decode_rvc_op1(inst, instinfo);
    case 2: return _decode_rvc_op2(inst, instinfo);
    }

    return false;
}




}

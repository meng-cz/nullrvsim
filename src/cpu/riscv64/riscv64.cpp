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

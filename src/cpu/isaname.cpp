
#include "isa.h"

namespace isa {

void init_rv64_inst_name_str(RV64InstDecoded *inst) {
    char buf[64];
    string ret = "";
    ret.reserve(64);
    if(inst->pc) {
        sprintf(buf, "0x%lx: ", inst->pc);
        ret += buf;
    }
    if(inst->flag & RVINSTFLAG_RVC) ret += "C.";
    switch (inst->opcode) {
    case RV64OPCode::load:
        switch (inst->param.loadstore) {
        case RV64LSWidth::byte: ret += "LB "; break;
        case RV64LSWidth::ubyte: ret += "LBU "; break;
        case RV64LSWidth::harf: ret += "LH "; break;
        case RV64LSWidth::uharf: ret += "LHU "; break;
        case RV64LSWidth::word: ret += "LW "; break;
        case RV64LSWidth::uword: ret += "LWU "; break;
        case RV64LSWidth::dword: ret += "LD "; break;
        }
        break;
    case RV64OPCode::loadfp:
        switch (inst->param.loadstore) {
        case RV64LSWidth::word: ret += "FLW "; break;
        case RV64LSWidth::dword: ret += "FLD "; break;
        }
        break;
    case RV64OPCode::miscmem:
        if(inst->flag & RVINSTFLAG_FENCE) ret += "FENCE ";
        if(inst->flag & RVINSTFLAG_FENCEI) ret += "FENCE.I ";
        if(inst->flag & RVINSTFLAG_FENCETSO) ret += "FENCE.TSO ";
        if(inst->flag & RVINSTFLAG_PAUSE) ret += "PAUSE ";
        break;
    case RV64OPCode::opimm:
    case RV64OPCode::opimm32:
    case RV64OPCode::op:
    case RV64OPCode::op32:
        switch (inst->param.intop) {
        case RV64IntOP73::ADD  : ret += "ADD"; break;
        case RV64IntOP73::SUB  : ret += "SUB"; break;
        case RV64IntOP73::SLL  : ret += "SLL"; break;
        case RV64IntOP73::SLT  : ret += "SLT"; break;
        case RV64IntOP73::SLTU : ret += "SLTU"; break;
        case RV64IntOP73::XOR  : ret += "XOR"; break;
        case RV64IntOP73::SRL  : ret += "SRL"; break;
        case RV64IntOP73::OR   : ret += "OR"; break;
        case RV64IntOP73::AND  : ret += "AND"; break;
        case RV64IntOP73::MULH : ret += "MULH"; break;
        case RV64IntOP73::MULHSU: ret += "MULHSU"; break;
        case RV64IntOP73::MULHU : ret += "MULHU"; break;
        case RV64IntOP73::DIV  : ret += "DIV"; break;
        case RV64IntOP73::DIVU : ret += "DIVU"; break;
        case RV64IntOP73::REM  : ret += "REM"; break;
        case RV64IntOP73::REMU : ret += "REMU"; break;
        }
        if(inst->opcode == RV64OPCode::opimm || inst->opcode == RV64OPCode::opimm32) ret += "I";
        if(inst->opcode == RV64OPCode::opimm32 || inst->opcode == RV64OPCode::op32) ret += "W";
        ret += " ";
        break;
    case RV64OPCode::auipc: ret += "AUIPC"; break;
    case RV64OPCode::store:
        switch (inst->param.loadstore) {
        case RV64LSWidth::byte: ret += "SB "; break;
        case RV64LSWidth::harf: ret += "SH "; break;
        case RV64LSWidth::word: ret += "SW "; break;
        case RV64LSWidth::dword: ret += "SD "; break;
        }
        break;
    case RV64OPCode::storefp:
        switch (inst->param.loadstore) {
        case RV64LSWidth::word: ret += "FSW "; break;
        case RV64LSWidth::dword: ret += "FSD "; break;
        }
        break;
    case RV64OPCode::amo: 
        switch (inst->param.amo.op)
        {
        case RV64AMOOP5::ADD : ret += "AMOADD"; break;
        case RV64AMOOP5::SWAP: ret += "AMOSWAP"; break;
        case RV64AMOOP5::LR  : ret += "LR"; break;
        case RV64AMOOP5::SC  : ret += "SC"; break;
        case RV64AMOOP5::XOR : ret += "AMOXOR"; break;
        case RV64AMOOP5::AND : ret += "AMOAND"; break;
        case RV64AMOOP5::OR  : ret += "AMOOR"; break;
        case RV64AMOOP5::MIN : ret += "AMOMIN"; break;
        case RV64AMOOP5::MAX : ret += "AMOMAX"; break;
        case RV64AMOOP5::MINU: ret += "AMOMINU"; break;
        case RV64AMOOP5::MAXU: ret += "AMOMAXU"; break;
        }
        switch (inst->param.amo.wid) {
        case RV64LSWidth::word: ret += ".W"; break;
        case RV64LSWidth::dword: ret += ".D"; break;
        }
        ret += " ";
        break;
    case RV64OPCode::lui: ret += "LUI "; break;
    case RV64OPCode::madd: ret += "MADD"; switch (inst->param.fp.fwid)
        {
        case RV64FPWidth2::fword : ret += ".S"; break;
        case RV64FPWidth2::fdword : ret += ".D"; break;
        } ret += ""; break;
    case RV64OPCode::msub: ret += "MSUB"; switch (inst->param.fp.fwid)
        {
        case RV64FPWidth2::fword : ret += ".S"; break;
        case RV64FPWidth2::fdword : ret += ".D"; break;
        } ret += ""; break;
    case RV64OPCode::nmsub: ret += "NMSUB"; switch (inst->param.fp.fwid)
        {
        case RV64FPWidth2::fword : ret += ".S"; break;
        case RV64FPWidth2::fdword : ret += ".D"; break;
        } ret += ""; break;
    case RV64OPCode::nmadd: ret += "NMADD"; switch (inst->param.fp.fwid)
        {
        case RV64FPWidth2::fword : ret += ".S"; break;
        case RV64FPWidth2::fdword : ret += ".D"; break;
        } ret += ""; break;
    case RV64OPCode::opfp:
        switch (inst->param.fp.op) {
        case RV64FPOP5::ADD  : ret += "FADD"; break;
        case RV64FPOP5::SUB  : ret += "FSUB"; break;
        case RV64FPOP5::MUL  : ret += "FMUL"; break;
        case RV64FPOP5::DIV  : ret += "FDIV"; break;
        case RV64FPOP5::SQRT : ret += "FSQRT"; break;
        case RV64FPOP5::SGNJ :
            switch (inst->param.fp.funct3.sgnj) {
            case RV64FPSGNJ3::SGNJ : ret += "FSGNJ"; break;
            case RV64FPSGNJ3::SGNJN: ret += "FSGNJN"; break;
            case RV64FPSGNJ3::SGNJX: ret += "FSGNJX"; break;
            }
            break;
        case RV64FPOP5::MIN  :
            switch (inst->param.fp.funct3.minmax) {
            case RV64FPMIN3::MIN : ret += "FMIN"; break;
            case RV64FPMIN3::MAX : ret += "FMAX"; break;
            }
            break;
        case RV64FPOP5::CMP  :
            switch (inst->param.fp.funct3.cmp) {
            case RV64FPCMP3::FEQ : ret += "FEQ"; break;
            case RV64FPCMP3::FLE : ret += "FLE"; break;
            case RV64FPCMP3::FLT : ret += "FLT"; break;
            }
            break;
        case RV64FPOP5::CVTF2I: ret += "FCVT"; break;
        case RV64FPOP5::CVTI2F: ret += "FCVT"; break;
        case RV64FPOP5::MVF2I:
            switch (inst->param.fp.funct3.mv) {
            case RV64FPMVI3::RAW : ret += "FMV"; break;
            case RV64FPMVI3::FCLASS : ret += "FCLASS"; break;
            }
            break;
        case RV64FPOP5::MVI2F: ret += "FMV"; break;
        case RV64FPOP5::MVF2F: ret += "FMV"; break;
        }
        if(inst->param.fp.op == RV64FPOP5::CVTF2I || inst->param.fp.op == RV64FPOP5::MVF2I) {
            switch (inst->param.fp.iwid)
            {
            case RV64FPCVTWidth5::word : ret += ".X.W"; break;
            case RV64FPCVTWidth5::uword : ret += ".X.WU"; break;
            case RV64FPCVTWidth5::dword : ret += ".X.L"; break;
            case RV64FPCVTWidth5::udword : ret += ".X.LU"; break;
            }
        }
        switch (inst->param.fp.fwid)
        {
        case RV64FPWidth2::fword : ret += ".S"; break;
        case RV64FPWidth2::fdword : ret += ".D"; break;
        }
        if(inst->param.fp.op == RV64FPOP5::CVTI2F || inst->param.fp.op == RV64FPOP5::MVI2F) {
            switch (inst->param.fp.iwid)
            {
            case RV64FPCVTWidth5::word : ret += ".W.X"; break;
            case RV64FPCVTWidth5::uword : ret += ".WU.X"; break;
            case RV64FPCVTWidth5::dword : ret += ".L.X"; break;
            case RV64FPCVTWidth5::udword : ret += ".LU.X"; break;
            }
        }
        ret += " ";
        break;
    case RV64OPCode::branch: 
        switch (inst->param.branch) {
        case RV64BranchOP3::BEQ : ret += "BEQ "; break;
        case RV64BranchOP3::BNE : ret += "BNE "; break;
        case RV64BranchOP3::BLT : ret += "BLT "; break;
        case RV64BranchOP3::BGE : ret += "BGE "; break;
        case RV64BranchOP3::BLTU: ret += "BLTU "; break;
        case RV64BranchOP3::BGEU: ret += "BGEU "; break;
        }
        break;
    case RV64OPCode::jalr: ret += "JALR "; break;
    case RV64OPCode::jal: ret += "JAL "; break;
    case RV64OPCode::system:
        if(inst->flag & RVINSTFLAG_ECALL) ret += "ECALL ";
        else if(inst->flag & RVINSTFLAG_EBREAK) ret += "EBREAK ";
        else {
            switch (inst->param.csr.op)
            {
            case RV64CSROP3::CSRRW : ret += "CSRRW "; break;
            case RV64CSROP3::CSRRS : ret += "CSRRS "; break;
            case RV64CSROP3::CSRRC : ret += "CSRRC "; break;
            case RV64CSROP3::CSRRWI: ret += "CSRRWI "; break;
            case RV64CSROP3::CSRRSI: ret += "CSRRSI "; break;
            case RV64CSROP3::CSRRCI: ret += "CSRRCI "; break;
            }
        }
        break;
    case RV64OPCode::nop: ret += "NOP "; break;
    }
    switch (inst->opcode)
    {
    case RV64OPCode::auipc:
    case RV64OPCode::lui:
    case RV64OPCode::jal:
        // rd, imm
        ret += ireg_names()[inst->rd];
        ret += ", ";
        ret += to_string(RAW_DATA_AS(inst->imm).i64);
        break;
    case RV64OPCode::opimm:
    case RV64OPCode::opimm32:
    case RV64OPCode::jalr:
    case RV64OPCode::load:
    case RV64OPCode::loadfp:
        // rd, rs1, imm
        ret += ireg_names()[inst->rd];
        ret += ", ";
        ret += ireg_names()[inst->rs1];
        ret += ", ";
        ret += to_string(RAW_DATA_AS(inst->imm).i64);
        break;
    case RV64OPCode::store:
    case RV64OPCode::storefp:
    case RV64OPCode::branch:
        // rs1, rs2, imm
        ret += ireg_names()[inst->rs1];
        ret += ", ";
        ret += ireg_names()[inst->rs2];
        ret += ", ";
        ret += to_string(RAW_DATA_AS(inst->imm).i64);
        break;
    case RV64OPCode::amo:
    case RV64OPCode::op:
    case RV64OPCode::op32:
        // rd, rs1, rs2
        ret += ireg_names()[inst->rd];
        ret += ", ";
        ret += ireg_names()[inst->rs1];
        ret += ", ";
        ret += ireg_names()[inst->rs2];
        break;
    case RV64OPCode::opfp:
        // rd, rs1, rs2
        ret += freg_names()[inst->rd];
        ret += ", ";
        ret += freg_names()[inst->rs1];
        ret += ", ";
        ret += freg_names()[inst->rs2];
        break;
    case RV64OPCode::madd:
    case RV64OPCode::msub:
    case RV64OPCode::nmsub:
    case RV64OPCode::nmadd:
        // rd, rs1, rs2, rs3
        ret += freg_names()[inst->rd];
        ret += ", ";
        ret += freg_names()[inst->rs1];
        ret += ", ";
        ret += freg_names()[inst->rs2];
        ret += ", ";
        ret += freg_names()[inst->rs3];
        break;
    case RV64OPCode::system:
        if(inst->flag & RVINSTFLAG_ECALL) ret += "ECALL ";
        else if(inst->flag & RVINSTFLAG_EBREAK) ret += "EBREAK ";
        else {
            // rd, rs1, imm
            ret += ireg_names()[inst->rd];
            ret += ", ";
            ret += ireg_names()[inst->rs1];
            ret += ", ";
            ret += to_string(RAW_DATA_AS(inst->imm).i64);
        }
    default:
        break;
    }
    inst->debug_name_str = ret;
}

}

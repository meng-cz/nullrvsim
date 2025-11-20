
#include "intop.h"

namespace riscv64 {

uint64_t perform_alu_op(ALUOPType optype, uint64_t s1, uint64_t s2) {
	switch (optype) {
		case ALUOPType::ADD:
			return s1 + s2;
		case ALUOPType::SUB:
			return s1 - s2;
		case ALUOPType::SLL: {
			unsigned sh = static_cast<unsigned>(s2 & 0x3fULL);
			return s1 << sh;
		}
		case ALUOPType::SLT:
			return (static_cast<int64_t>(s1) < static_cast<int64_t>(s2)) ? 1ULL : 0ULL;
		case ALUOPType::SLTU:
			return (s1 < s2) ? 1ULL : 0ULL;
		case ALUOPType::XOR:
			return s1 ^ s2;
		case ALUOPType::SRL: {
			unsigned sh = static_cast<unsigned>(s2 & 0x3fULL);
			return s1 >> sh; // logical right (s1 is unsigned)
		}
		case ALUOPType::SRA: {
			unsigned sh = static_cast<unsigned>(s2 & 0x3fULL);
			return static_cast<uint64_t>(static_cast<int64_t>(s1) >> sh);
		}
		case ALUOPType::OR:
			return s1 | s2;
		case ALUOPType::AND:
			return s1 & s2;

		/* 32-bit (W) variants: compute in 32-bit, then sign-extend to 64-bit */
		case ALUOPType::ADDW: {
			int32_t a = static_cast<int32_t>(s1);
			int32_t b = static_cast<int32_t>(s2);
			int32_t sr = a + b;
			return static_cast<uint64_t>(static_cast<int64_t>(sr));
		}
		case ALUOPType::SUBW: {
			int32_t a = static_cast<int32_t>(s1);
			int32_t b = static_cast<int32_t>(s2);
			int32_t sr = a - b;
			return static_cast<uint64_t>(static_cast<int64_t>(sr));
		}
		case ALUOPType::SLLW: {
			int32_t v = static_cast<int32_t>(s1);
			unsigned sh = static_cast<unsigned>(s2 & 0x1fU);
			int32_t sr = v << sh;
			return static_cast<uint64_t>(static_cast<int64_t>(sr));
		}
		case ALUOPType::SLTW: {
			int32_t a = static_cast<int32_t>(static_cast<uint32_t>(s1));
			int32_t b = static_cast<int32_t>(static_cast<uint32_t>(s2));
			return (a < b) ? 1ULL : 0ULL;
		}
		case ALUOPType::SLTUW: {
			uint32_t a = static_cast<uint32_t>(s1);
			uint32_t b = static_cast<uint32_t>(s2);
			return (a < b) ? 1ULL : 0ULL;
		}
		case ALUOPType::XORW: {
			uint32_t r = static_cast<uint32_t>(s1) ^ static_cast<uint32_t>(s2);
			int32_t sr = static_cast<int32_t>(r);
			return static_cast<uint64_t>(static_cast<int64_t>(sr));
		}
		case ALUOPType::SRLW: {
			uint32_t v = static_cast<uint32_t>(s1);
			unsigned sh = static_cast<unsigned>(s2 & 0x1fU);
			uint32_t r = v >> sh; // logical on 32-bit
			int32_t sr = static_cast<int32_t>(r);
			return static_cast<uint64_t>(static_cast<int64_t>(sr));
		}
		case ALUOPType::SRAW: {
			int32_t v = static_cast<int32_t>(static_cast<uint32_t>(s1));
			unsigned sh = static_cast<unsigned>(s2 & 0x1fU);
			int32_t r = v >> sh; // arithmetic on 32-bit
			return static_cast<uint64_t>(static_cast<int64_t>(r));
		}
		case ALUOPType::ORW: {
			uint32_t r = static_cast<uint32_t>(s1) | static_cast<uint32_t>(s2);
			int32_t sr = static_cast<int32_t>(r);
			return static_cast<uint64_t>(static_cast<int64_t>(sr));
		}
		case ALUOPType::ANDW: {
			uint32_t r = static_cast<uint32_t>(s1) & static_cast<uint32_t>(s2);
			int32_t sr = static_cast<int32_t>(r);
			return static_cast<uint64_t>(static_cast<int64_t>(sr));
		}

		default:
			return 0ULL;
	}

    return 0ULL;
}

uint64_t perform_mul_op(MULOPType optype, uint64_t s1, uint64_t s2) {

	switch (optype) {
		case MULOPType::MUL: {
            int64_t a = static_cast<int64_t>(s1);
            int64_t b = static_cast<int64_t>(s2);
			return static_cast<uint64_t>(a * b);
		}
		case MULOPType::MULH: {
			__int128 a = static_cast<__int128>(static_cast<int64_t>(s1));
			__int128 b = static_cast<__int128>(static_cast<int64_t>(s2));
			__int128 prod = a * b;
			return static_cast<uint64_t>(prod >> 64);
		}
		case MULOPType::MULHSU: {
			__int128 a = static_cast<__int128>(static_cast<int64_t>(s1));
			__int128 b = static_cast<__int128>(static_cast<uint64_t>(s2));
			__int128 prod = a * b;
			return static_cast<uint64_t>(prod >> 64);
		}
		case MULOPType::MULHU: {
			__uint128_t prod = static_cast<__uint128_t>(s1) * static_cast<__uint128_t>(s2);
			return static_cast<uint64_t>(prod >> 64);
		}
		case MULOPType::MULW: {
			int32_t a = static_cast<int32_t>(s1);
			int32_t b = static_cast<int32_t>(s2);
			int32_t sr = a * b;
			return static_cast<uint64_t>(static_cast<int64_t>(sr));
		}

		default:
			return 0ULL;
	}

    return 0ULL;
}

uint64_t perform_div_op(DIVOPType optype, uint64_t s1, uint64_t s2) {
    
    int64_t ss1 = static_cast<int64_t>(s1);
    int64_t ss2 = static_cast<int64_t>(s2);

    int32_t sw1 = static_cast<int32_t>(s1);
    int32_t sw2 = static_cast<int32_t>(s2);

    uint32_t uw1 = static_cast<uint32_t>(s1);
    uint32_t uw2 = static_cast<uint32_t>(s2);

    switch (optype)
    {
    case DIVOPType::DIV: return ss1 / ss2;
    case DIVOPType::DIVU: return s1 / s2;
    case DIVOPType::REM: return ss1 % ss2;
    case DIVOPType::REMU: return s1 % s2;
    case DIVOPType::DIVW: return static_cast<int64_t>(sw1 / sw2);
    case DIVOPType::DIVUW: return uw1 / uw2;
    case DIVOPType::REMW: return static_cast<int64_t>(sw1 % sw2);
    case DIVOPType::REMUW: return uw1 % uw2;
    default:
        return 0ULL;
    }

    return 0ULL;
}



}

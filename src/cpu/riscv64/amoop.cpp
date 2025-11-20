
#include "amoop.h"

namespace riscv64 {

uint64_t perform_amo_op(AMOOPType optype, uint64_t current, uint64_t oprand) {

    int64_t d1 = static_cast<int64_t>(current);
    int64_t d2 = static_cast<int64_t>(oprand);
    uint64_t ud1 = current;
    uint64_t ud2 = oprand;

    int32_t w1 = static_cast<int32_t>(current);
    int32_t w2 = static_cast<int32_t>(oprand);
    uint32_t uw1 = static_cast<uint32_t>(current);
    uint32_t uw2 = static_cast<uint32_t>(oprand);

    switch (optype) {
        case AMOOPType::ADD_D: return d1 + d2;
        case AMOOPType::SWAP_D: return d2;
        case AMOOPType::XOR_D: return d1 ^ d2;
        case AMOOPType::AND_D: return d1 & d2;
        case AMOOPType::OR_D: return d1 | d2;
        case AMOOPType::MIN_D: return (d1 < d2)?d1:d2;
        case AMOOPType::MAX_D: return (d1 > d2)?d1:d2;
        case AMOOPType::MINU_D: return (ud1 < ud2)?ud1:ud2;
        case AMOOPType::MAXU_D: return (ud1 > ud2)?ud1:ud2;
        case AMOOPType::ADD_W: return static_cast<int64_t>(w1 + w2);
        case AMOOPType::SWAP_W: return static_cast<int64_t>(w2);
        case AMOOPType::XOR_W: return static_cast<int64_t>(w1 ^ w2);
        case AMOOPType::AND_W: return static_cast<int64_t>(w1 & w2);
        case AMOOPType::OR_W: return static_cast<int64_t>(w1 | w2);
        case AMOOPType::MIN_W: return static_cast<int64_t>((w1 < w2)?w1:w2);
        case AMOOPType::MAX_W: return static_cast<int64_t>((w1 > w2)?w1:w2);
        case AMOOPType::MINU_W: return static_cast<int64_t>((uw1 < uw2)?uw1:uw2);
        case AMOOPType::MAXU_W: return static_cast<int64_t>((uw1 > uw2)?uw1:uw2);
        default: return 0ULL;
    }

    return 0ULL;
}




}

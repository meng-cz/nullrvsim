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

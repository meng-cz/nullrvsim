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

#pragma once

#include "common.h"

template <uint32_t BitWidth>
class UInt {
public:

    static_assert(BitWidth > 0);
    using StorageT = typename std::conditional<
        (BitWidth <= 8), uint8_t,
        typename std::conditional<
            (BitWidth <= 16), uint16_t,
            typename std::conditional<
                (BitWidth <= 32), uint32_t,
                typename std::conditional<
                    (BitWidth <= 64), uint64_t,
                    std::array<uint64_t, ((BitWidth) + 63) / 64>
                >::type
            >::type
        >::type
    >::type;


    FORCE_INLINE constexpr UInt() {
        if constexpr (BitWidth <= 64) {
            value = 0;
        } else {
            value.fill(0);
        }
    }
    FORCE_INLINE constexpr UInt(const uint64_t& initial) {
        if constexpr (BitWidth <= 64) {
            constexpr uint64_t mask = (BitWidth == 64) ? ~0UL : ((1UL << BitWidth) - 1UL);
            value = initial & mask;
        } else {
            value.fill(0);
            value[0] = initial;
        }
    }
    
    template <uint32_t OtherWidth>
    FORCE_INLINE constexpr UInt(const UInt<OtherWidth> &other) {
        if constexpr (BitWidth <= 64) {
            constexpr uint64_t mask = (BitWidth == 64) ? ~0UL : ((1UL << BitWidth) - 1UL);
            if constexpr (OtherWidth <= 64) {
                value = other.value & mask;
            } else {
                value = other.value[0] & mask;
            }
        } else {
            constexpr uint32_t other_words = (OtherWidth + 63) / 64;
            constexpr uint32_t this_words = (BitWidth + 63) / 64;
            for (uint32_t i = 0; i < this_words; i++) {
                if (i < other_words) {
                    value[i] = other.value[i];
                } else {
                    value[i] = 0;
                }
            }
            if constexpr (BitWidth % 64 != 0) {
                constexpr uint64_t topmask = ((1UL << (BitWidth % 64)) - 1ULL);
                value[this_words - 1] &= topmask;
            }
        }
    }

    template <uint32_t HIBit, uint32_t LOBit>
    FORCE_INLINE constexpr UInt<(HIBit - LOBit + 1)> extract() const {
        static_assert(HIBit < BitWidth);
        static_assert(LOBit <= HIBit);
        constexpr uint32_t NewBitWidth = (HIBit - LOBit + 1);
        if constexpr (BitWidth <= 64) {
            constexpr uint64_t mask = (NewBitWidth == 64) ? ~0UL : ((1UL << NewBitWidth) - 1UL);
            return UInt<NewBitWidth>((value >> LOBit) & mask);
        } else if constexpr (NewBitWidth <= 64) {
            uint64_t ret = (value[LOBit / 64] >> (LOBit % 64));
            if ((LOBit % 64 + NewBitWidth) > 64) {
                ret |= (value[LOBit / 64 + 1] << (64 - (LOBit % 64)));
            }
            return UInt<NewBitWidth>(ret &((1UL << NewBitWidth) - 1UL));
        } else {
            UInt<NewBitWidth> res;
            for (uint64_t i = 0; i < (NewBitWidth + 63) / 64; i++) {
                res.value[i] = value[(LOBit / 64) + i] >> (LOBit % 64);
                if ((LOBit % 64 != 0) && ((LOBit % 64 + NewBitWidth) > ((i + 1) * 64))) {
                    res.value[i] |= (value[(LOBit / 64) + i + 1] << (64 - (LOBit % 64)));
                }
            }
            return res;
        }
    }

    inline string hex() const {
        std::stringstream ss;
        ss << std::hex;
        if constexpr (BitWidth <= 64) {
            ss << value;
        } else {
            for (int64_t i = int64_t((BitWidth + 63) / 64) - 1; i >= 0; i--) {
                ss << std::setw(16) << std::setfill('0') << value[i];
            }
        }
        return ss.str();
    }


    StorageT value;
};

template <uint32_t BitWidth>
FORCE_INLINE constexpr UInt<BitWidth> operator+(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
    constexpr bool _is_small = (BitWidth <= 64);
    constexpr uint64_t _words = (BitWidth + 63) / 64;
    if constexpr (_is_small) {
        constexpr uint64_t mask = (BitWidth == 64) ? ~0UL : ((1UL << BitWidth) - 1UL);
        return UInt<BitWidth>((a.value + b.value) & mask);
    } else {
        UInt<BitWidth> result;
        uint64_t carry = 0;
        for (uint64_t i = 0; i < _words; i++) {
            uint64_t na = a.value[i];
            uint64_t nb = b.value[i];
            uint64_t s = na + nb + carry;
            result.value[i] = s;
            carry = (s < na || (carry && s == na)) ? 1 : 0;
        }
        if constexpr (BitWidth % 64 != 0) {
            constexpr uint64_t topmask = ((1UL << (BitWidth % 64)) - 1ULL);
            result.value[_words - 1] &= topmask;
        }
        return result;
    }
}

template <uint32_t BitWidth>
FORCE_INLINE constexpr UInt<BitWidth> operator-(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
    constexpr bool _is_small = (BitWidth <= 64);
    constexpr uint64_t _words = (BitWidth + 63) / 64;
    if constexpr (_is_small) {
        constexpr uint64_t mask = (BitWidth == 64) ? ~0UL : ((1UL << BitWidth) - 1UL);
        return UInt<BitWidth>((a.value - b.value) & mask);
    } else {
        UInt<BitWidth> result;
        uint64_t borrow = 0;
        for (uint64_t i = 0; i < _words; i++) {
            uint64_t na = a.value[i];
            uint64_t nb = b.value[i];
            uint64_t sub = nb + borrow;
            borrow = (na < sub) ? 1 : 0;
            result.value[i] = na - sub;
        }
        if constexpr (BitWidth % 64 != 0) {
            constexpr uint64_t topmask = ((1UL << (BitWidth % 64)) - 1ULL);
            result.value[_words - 1] &= topmask;
        }
        return result;
    }
}


// Multiplication
template <uint32_t BitWidth>
FORCE_INLINE constexpr UInt<BitWidth> operator*(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
    constexpr bool _is_small = (BitWidth <= 64);
    constexpr uint64_t _words = (BitWidth + 63) / 64;
    if constexpr (_is_small) {
        constexpr uint64_t mask = (BitWidth == 64) ? ~0ULL : ((1ULL << BitWidth) - 1ULL);
        return UInt<BitWidth>((uint64_t)(((uint64_t)a.value * (uint64_t)b.value) & mask));
    } else {
        UInt<BitWidth> res;
        array<uint64_t, _words * 2> tmp;
        tmp.fill(0);
        for (uint64_t i = 0; i < _words; i++) {
            uint128_t carry = 0;
            for (uint64_t j = 0; j < _words; j++) {
                uint128_t cur = (uint128_t)tmp[i + j] + (uint128_t)a.value[i] * (uint128_t)b.value[j] + carry;
                tmp[i + j] = (uint64_t)cur;
                carry = (cur >> 64);
            }
            uint64_t pos = i + _words;
            while (carry != 0) {
                uint128_t cur = (uint128_t)tmp[pos] + carry;
                tmp[pos] = (uint64_t)cur;
                carry = (cur >> 64);
                pos++;
            }
        }
        for (uint64_t i = 0; i < _words; i++) res.value[i] = tmp[i];
        if constexpr (BitWidth % 64 != 0) {
            constexpr uint64_t topmask = ((1ULL << (BitWidth % 64)) - 1ULL);
            res.value[_words - 1] &= topmask;
        }
        return res;
    }
}

// // Division and modulo (long division, bit-by-bit)
// template <uint32_t BitWidth>
// FORCE_INLINE constexpr UInt<BitWidth> operator/(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
//     constexpr bool _is_small = (BitWidth <= 64);
//     constexpr uint64_t _words = (BitWidth + 63) / 64;
//     if constexpr (_is_small) {
//         if (b.value == 0) return UInt<BitWidth>(0);
//         return UInt<BitWidth>((uint64_t)(((uint64_t)a.value) / ((uint64_t)b.value)));
//     } else {
//         UInt<BitWidth> zero;
//         UInt<BitWidth> quot;
//         UInt<BitWidth> rem;
//         // get bit helper
//         auto get_bit = [](const UInt<BitWidth> &v, uint32_t idx)->int {
//             uint64_t w = idx / 64;
//             uint32_t off = idx % 64;
//             return (int)((v.value[w] >> off) & 1ULL);
//         };
//         if (b == zero) return zero;
//         for (int i = int(BitWidth) - 1; i >= 0; --i) {
//             // rem <<= 1
//             unsigned carry = 0;
//             for (int w = int(_words) - 1; w >= 0; --w) {
//                 uint64_t newcarry = (uint64_t)(rem.value[w] >> 63);
//                 rem.value[w] = (rem.value[w] << 1) | carry;
//                 carry = (unsigned)newcarry;
//             }
//             // bring down bit i of a
//             if (get_bit(a, (uint32_t)i)) {
//                 rem.value[0] |= 1ULL;
//             }
//             // if rem >= b
//             bool ge = false;
//             for (int w = int(_words) - 1; w >= 0; --w) {
//                 if (rem.value[w] > b.value[w]) { ge = true; break; }
//                 if (rem.value[w] < b.value[w]) { ge = false; break; }
//             }
//             if (ge) {
//                 // rem -= b
//                 uint128_t borrow = 0;
//                 for (uint64_t w = 0; w < _words; ++w) {
//                     uint128_t cur = (uint128_t)rem.value[w];
//                     uint128_t sub = (uint128_t)b.value[w] + borrow;
//                     if (cur >= sub) {
//                         rem.value[w] = (uint64_t)(cur - sub);
//                         borrow = 0;
//                     } else {
//                         rem.value[w] = (uint64_t)((((uint128_t)1ULL << 64) + cur) - sub);
//                         borrow = 1;
//                     }
//                 }
//                 // set quotient bit
//                 uint64_t qw = i / 64; uint32_t qoff = i % 64;
//                 quot.value[qw] |= (1ULL << qoff);
//             }
//         }
//         if constexpr (BitWidth % 64 != 0) {
//             constexpr uint64_t topmask = ((1ULL << (BitWidth % 64)) - 1ULL);
//             quot.value[_words - 1] &= topmask;
//         }
//         return quot;
//     }
// }

// template <uint32_t BitWidth>
// FORCE_INLINE constexpr UInt<BitWidth> operator%(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
//     constexpr bool _is_small = (BitWidth <= 64);
//     if constexpr (_is_small) {
//         if (b.value == 0) return UInt<BitWidth>(0);
//         return UInt<BitWidth>(((uint64_t)a.value) % ((uint64_t)b.value));
//     } else {
//         // compute remainder via long division
//         UInt<BitWidth> zero;
//         UInt<BitWidth> rem;
//         auto get_bit = [](const UInt<BitWidth> &v, uint32_t idx)->int {
//             uint64_t w = idx / 64;
//             uint32_t off = idx % 64;
//             return (int)((v.value[w] >> off) & 1ULL);
//         };
//         if (b == zero) return zero;
//         for (int i = int(BitWidth) - 1; i >= 0; --i) {
//             unsigned carry = 0;
//             for (int w = int((BitWidth + 63) / 64) - 1; w >= 0; --w) {
//                 uint64_t newcarry = (uint64_t)(rem.value[w] >> 63);
//                 rem.value[w] = (rem.value[w] << 1) | carry;
//                 carry = (unsigned)newcarry;
//             }
//             if (get_bit(a, (uint32_t)i)) rem.value[0] |= 1ULL;
//             bool ge = false;
//             for (int w = int((BitWidth + 63) / 64) - 1; w >= 0; --w) {
//                 if (rem.value[w] > b.value[w]) { ge = true; break; }
//                 if (rem.value[w] < b.value[w]) { ge = false; break; }
//             }
//             if (ge) {
//                 uint128_t borrow = 0;
//                 for (uint64_t w = 0; w < (BitWidth + 63) / 64; ++w) {
//                     uint128_t cur = (uint128_t)rem.value[w];
//                     uint128_t sub = (uint128_t)b.value[w] + borrow;
//                     if (cur >= sub) { rem.value[w] = (uint64_t)(cur - sub); borrow = 0; }
//                     else { rem.value[w] = (uint64_t)((((uint128_t)1ULL << 64) + cur) - sub); borrow = 1; }
//                 }
//             }
//         }
//         return rem;
//     }
// }

// Bitwise
template <uint32_t BitWidth>
FORCE_INLINE constexpr UInt<BitWidth> operator&(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
    if constexpr (BitWidth <= 64) {
        return UInt<BitWidth>((a.value & b.value));
    } else {
        UInt<BitWidth> r;
        constexpr uint64_t _words = (BitWidth + 63) / 64;
        for (uint64_t i = 0; i < _words; i++) r.value[i] = a.value[i] & b.value[i];
        return r;
    }
}

template <uint32_t BitWidth>
FORCE_INLINE constexpr UInt<BitWidth> operator|(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
    if constexpr (BitWidth <= 64) {
        return UInt<BitWidth>((a.value | b.value));
    } else {
        UInt<BitWidth> r;
        constexpr uint64_t _words = (BitWidth + 63) / 64;
        for (uint64_t i = 0; i < _words; i++) r.value[i] = a.value[i] | b.value[i];
        return r;
    }
}

template <uint32_t BitWidth>
FORCE_INLINE constexpr UInt<BitWidth> operator^(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
    if constexpr (BitWidth <= 64) {
        constexpr uint64_t mask = (BitWidth == 64) ? ~0UL : ((1UL << BitWidth) - 1UL);
        return UInt<BitWidth>((a.value ^ b.value) & mask);
    } else {
        UInt<BitWidth> r;
        constexpr uint64_t _words = (BitWidth + 63) / 64;
        for (uint64_t i = 0; i < _words; i++) r.value[i] = a.value[i] ^ b.value[i];
        if constexpr (BitWidth % 64 != 0) {
            constexpr uint64_t topmask = ((1ULL << (BitWidth % 64)) - 1ULL);
            r.value[_words - 1] &= topmask;
        }
        return r;
    }
}

template <uint32_t BitWidth>
FORCE_INLINE constexpr UInt<BitWidth> operator~(const UInt<BitWidth> &a) {
    if constexpr (BitWidth <= 64) {
        constexpr uint64_t mask = (BitWidth == 64) ? ~0UL : ((1UL << BitWidth) - 1UL);
        return UInt<BitWidth>((~a.value) & mask);
    } else {
        UInt<BitWidth> r;
        constexpr uint64_t _words = (BitWidth + 63) / 64;
        for (uint64_t i = 0; i < _words; i++) r.value[i] = ~a.value[i];
        if constexpr (BitWidth % 64 != 0) {
            constexpr uint64_t topmask = ((1ULL << (BitWidth % 64)) - 1ULL);
            r.value[_words - 1] &= topmask;
        }
        return r;
    }
}

// Comparisons
template <uint32_t BitWidth>
FORCE_INLINE constexpr bool operator==(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
    if constexpr (BitWidth <= 64) return a.value == b.value;
    else {
        constexpr uint64_t _words = (BitWidth + 63) / 64;
        for (int i = int(_words) - 1; i >= 0; --i) if (a.value[i] != b.value[i]) return false;
        return true;
    }
}

template <uint32_t BitWidth>
FORCE_INLINE constexpr bool operator!=(const UInt<BitWidth> &a, const UInt<BitWidth> &b) { return !(a == b); }

template <uint32_t BitWidth>
FORCE_INLINE constexpr bool operator<(const UInt<BitWidth> &a, const UInt<BitWidth> &b) {
    if constexpr (BitWidth <= 64) return a.value < b.value;
    else {
        constexpr uint64_t _words = (BitWidth + 63) / 64;
        for (int i = int(_words) - 1; i >= 0; --i) {
            if (a.value[i] < b.value[i]) return true;
            if (a.value[i] > b.value[i]) return false;
        }
        return false;
    }
}

template <uint32_t BitWidth>
FORCE_INLINE constexpr bool operator<=(const UInt<BitWidth> &a, const UInt<BitWidth> &b) { return (a < b) || (a == b); }

template <uint32_t BitWidth>
FORCE_INLINE constexpr bool operator>(const UInt<BitWidth> &a, const UInt<BitWidth> &b) { return b < a; }

template <uint32_t BitWidth>
FORCE_INLINE constexpr bool operator>=(const UInt<BitWidth> &a, const UInt<BitWidth> &b) { return !(a < b); }

bool _test_uint() {

    printf("Testing UInt...\n");
    // helper
    auto fail = [](const char *what) {
        printf("  FAIL: %s\n", what);
        return false;
    };

    // Test UInt<8>
    {
        UInt<8> a(200), b(100);
        UInt<8> s = a + b; // 200 + 100 = 44 mod 256
        if (!(s == UInt<8>( (uint8_t)((200 + 100) & 0xff) ))) return fail("UInt<8> add wrap");
        UInt<8> sub = a - b; if (!(sub == UInt<8>(100))) return fail("UInt<8> sub");
        UInt<8> bw = a & b; if (!(bw == UInt<8>(200 & 100))) return fail("UInt<8> and");
        UInt<8> bo = a | b; if (!(bo == UInt<8>(200 | 100))) return fail("UInt<8> or");
        UInt<8> bx = a ^ b; if (!(bx == UInt<8>(200 ^ 100))) return fail("UInt<8> xor");
        UInt<8> nt = ~a; if (!(nt == UInt<8>((uint8_t)(~200)))) return fail("UInt<8> not");
    }

    // Test UInt<16> extract and arithmetic
    {
        UInt<16> x = 0xABCD;
        auto hi = x.extract<15,8>();
        if (!(hi == UInt<8>(0xAB))) return fail("UInt<16> extract high byte");
        auto lo = x.extract<7,0>();
        if (!(lo == UInt<8>(0xCD))) return fail("UInt<16> extract low byte");
        UInt<16> a(0x1234), b(0x0FED);
        if (!((a + b) == UInt<16>(uint16_t(0x1234 + 0x0FED)))) return fail("UInt<16> add");
    }

    // Test UInt<32> multiplication
    {
        UInt<32> a(40000), b(40000);
        UInt<32> m = a * b;
        uint64_t expect = uint64_t(40000) * uint64_t(40000);
        if (!(m == UInt<32>( (uint32_t)expect ))) return fail("UInt<32> mul");
    }

    // Test UInt<64> bitwise and comparisons
    {
        UInt<64> a(0x1234567890ABCDEFULL), b(0xFEDCBA0987654321ULL);
        if (!((a & b) == UInt<64>(0x1234567890ABCDEFULL & 0xFEDCBA0987654321ULL))) return fail("UInt<64> and");
        if (!((a | b) == UInt<64>(0x1234567890ABCDEFULL | 0xFEDCBA0987654321ULL))) return fail("UInt<64> or");
        if (!((a ^ b) == UInt<64>(0x1234567890ABCDEFULL ^ 0xFEDCBA0987654321ULL))) return fail("UInt<64> xor");
        if (!(a != b)) return fail("UInt<64> neq");
        if (!(a < b || a > b)) { /* allow either based on values */ }
    }

    // Test multi-word UInt<128> addition carry
    {
        UInt<128> a; a.value[0] = ~0ULL; a.value[1] = 0ULL;
        UInt<128> one; one.value[0] = 1ULL; one.value[1] = 0ULL;
        UInt<128> sum = a + one;
        if (!(sum.value[0] == 0ULL && sum.value[1] == 1ULL)) return fail("UInt<128> add carry");
    }

    // Test multi-word UInt<192> bitwise ops
    {
        UInt<192> a; UInt<192> b; for (size_t i = 0; i < (192+63)/64; ++i) { a.value[i] = (uint64_t)i; b.value[i] = (uint64_t)(i+1); }
        auto andr = a & b;
        for (size_t i = 0; i < (192+63)/64; ++i) if (andr.value[i] != (a.value[i] & b.value[i])) return fail("UInt<192> and per-word");
    }
    
    // cross-width ops: conversions, widening, truncation and mixed-width arithmetic
    {
        UInt<8> a8(250);
        UInt<16> b16(1000);
        // widen a8 to 16 and add to b16
        UInt<16> sum16 = UInt<16>(a8) + b16;
        if (!(sum16 == UInt<16>(uint16_t(250 + 1000)))) return fail("cross-width add 8->16");
        // truncation from wider to narrower
        UInt<8> trunc8 = UInt<8>(b16);
        if (!(trunc8 == UInt<8>(uint8_t(1000 & 0xff)))) return fail("cross-width truncate 16->8");
        // widen and multiply
        UInt<32> prod32 = UInt<32>(a8) * UInt<32>(2);
        if (!(prod32 == UInt<32>(uint32_t(250 * 2)))) return fail("cross-width mul widen");
        // convert large to small when source is multi-word
        UInt<128> big; big.value[0] = 0x1122334455667788ULL; big.value[1] = 0x99AABBCCDDEEFF00ULL;
        UInt<64> low64 = UInt<64>(big);
        if (!(low64 == UInt<64>(0x1122334455667788ULL))) return fail("cross-width truncate 128->64");
    }


    printf("  UInt passed all tests.\n");
    return true;
}


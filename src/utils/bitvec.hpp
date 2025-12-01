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

template<typename ValueT>
class BitVec {
public:
    BitVec() : data(0) {}
    BitVec(ValueT v) : data(v) {}

    inline void set_bit(ValueT pos) {
        data |= (1 << pos);
    }
    inline void clear_bit(ValueT pos) {
        data &= ~(1 << pos);
    }
    inline bool get_bit(ValueT pos) const {
        return (data & (1 << pos)) != 0;
    }
    inline ValueT get_data() const {
        return data;
    }

    inline bool operator==(const BitVec<ValueT> &other) const {
        return data == other.data;
    }
    inline bool operator!=(const BitVec<ValueT> &other) const {
        return data != other.data;
    }
    inline BitVec<ValueT> operator&(const BitVec<ValueT> &other) const {
        return BitVec<ValueT>(data & other.data);
    }
    inline BitVec<ValueT> operator|(const BitVec<ValueT> &other) const {
        return BitVec<ValueT>(data | other.data);
    }
    inline BitVec<ValueT> operator~() const {
        return BitVec<ValueT>(~data);
    }
    inline BitVec<ValueT>& operator=(const BitVec<ValueT> &other) {
        data = other.data;
        return *this;
    }
    inline BitVec<ValueT>& operator=(ValueT v) {
        data = v;
        return *this;
    }
    inline BitVec<ValueT>& operator|=(const BitVec<ValueT> &other) {
        data |= other.data;
        return *this;
    }
    inline BitVec<ValueT>& operator&=(const BitVec<ValueT> &other) {
        data &= other.data;
        return *this;
    }
    inline bool operator[](ValueT pos) const {
        return get_bit(pos);
    }
protected:
    ValueT data = 0;
};

typedef BitVec<uint8_t>  BitVec8;
typedef BitVec<uint16_t> BitVec16;
typedef BitVec<uint32_t> BitVec32;
typedef BitVec<uint64_t> BitVec64;

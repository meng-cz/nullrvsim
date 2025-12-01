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

template<uint8_t MaxV>
class SaturateCounter8 {
public:
    SaturateCounter8() : value(MaxV / 2) {}

    inline uint8_t get_increment() {
        return (value < MaxV) ? (value + 1) : MaxV;
    }

    inline uint8_t get_decrement() {
        return (value > 0) ? (value - 1) : 0;
    }

    inline uint8_t get() const {
        return value;
    }

    inline bool is_positive() const {
        return value >= (MaxV / 2);
    }
    inline bool is_saturate_positive() const {
        return value == MaxV;
    }
    inline bool is_saturate_negative() const {
        return value == 0;
    }
    inline bool is_negative() const {
        return value < (MaxV / 2);
    }

    inline uint8_t get_netural() const {
        return MaxV / 2;
    }
    inline uint8_t get_max() const {
        return MaxV;
    }

    inline uint8_t operator=(uint8_t v) {
        value = (v > MaxV) ? MaxV : v;
        return value;
    }

protected:
    uint8_t value;
};

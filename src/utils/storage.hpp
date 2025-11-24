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

template <typename T>
class StorageNext {
public:

    StorageNext() {
        memset(&value, 0, sizeof(value));
        memset(&nextvalue, 0, sizeof(nextvalue));
    }
    StorageNext(const T initial) {
        value = initial;
        nextvalue = initial;
    }

    inline T & get() {
        return value;
    }
    inline void setnext(T & value, uint8 priority) {
        if(!updated || priority <= update_priority) {
            nextvalue = value;
            updated = true;
            update_priority = priority;
        }
    }
    inline void apply_next_tick() {
        if(updated) {
            value = nextvalue;
            updated = false;
            update_priority = 0;
        }
    }

protected:
    T value;
    T nextvalue;
    bool updated = false;
    uint8 update_priority = 0;
};

template <typename T, unsigned int N>
class StorageNextArray {
public:
    
    StorageNextArray() {}
    
    StorageNextArray(const T initial) {
        for(auto &item : data) {
            item = StorageNext<T>(initial);
        }
    }
    StorageNextArray(const T * initial) {
        for(unsigned int i = 0; i < N; i++) {
            data[i] = StorageNext<T>(initial[i]);
        }
    }

    inline T & get(unsigned int index) {
        return data[index].get();
    }
    inline void setnext(unsigned int index, T & value, uint8 priority) {
        data[index].setnext(value, priority);
    }
    inline void apply_next_tick() {
        for(auto &item : data) {
            item.apply_next_tick();
        }
    }

protected:
    std::array<StorageNext<T>, N> data;
};


template <typename T>
class StorageNextVector {
public:

    StorageNextVector() {}
    
    StorageNextVector(int64 size) {
        data.resize(size);
    }
    StorageNextVector(int64 size, const T initial) {
        data.assign(size, StorageNext<T>(initial));
    }
    StorageNextVector(int64 size, const T * initial) {
        data.resize(size);
        for(int64 i = 0; i < size; i++) {
            data[i] = StorageNext<T>(initial[i]);
        }
    }

    inline T & get(int64 index) {
        return data[index].get();
    }
    inline void setnext(int64 index, T & value, uint8 priority) {
        data[index].setnext(value, priority);
    }
    inline void apply_next_tick() {
        for(auto &item : data) {
            item.apply_next_tick();
        }
    }

protected:
    std::vector<StorageNext<T>> data;
};


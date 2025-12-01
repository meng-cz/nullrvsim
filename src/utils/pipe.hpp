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

/**
 * A simple pipe between stages, with 1-cycle latency.
 * Without any buffer, only one data can be stored in the pipe.
 * Without any handshake, the producer just overwrite the data in the pipe.
 * The consumer always get the latest data in the pipe.
 */
template<typename DataT>
class SimpleStagePipe {
public:
    inline DataT &get_input_buffer() {
        return _input_buffer;
    }
    inline DataT &get_output_buffer() {
        return _output_buffer;
    }

    inline void push(const DataT &data) {
        _input_buffer = data;
    }
    inline void top(DataT *data) {
        *data = _output_buffer;
    }

    inline void apply_next_tick() {
        _output_buffer = _input_buffer;
    }
protected:
    DataT _input_buffer;
    DataT _output_buffer;
};

template<typename DataT>
class SimpleStageValidPipe {
public:
    inline ValidData<DataT> &get_input_buffer() {
        return _input_buffer;
    }
    inline ValidData<DataT> &get_output_buffer() {
        return _output_buffer;
    }

    inline void keep() {
        _input_buffer.valid = true;
    }
    inline void push(const DataT &data) {
        _input_buffer.data = data;
        _input_buffer.valid = true;
    }
    inline bool top(DataT *data) {
        *data = _output_buffer.data;
        return _output_buffer.valid;
    }

    inline void apply_next_tick() {
        _output_buffer = _input_buffer;
        _input_buffer.valid = false;
    }
protected:
    ValidData<DataT> _input_buffer;
    ValidData<DataT> _output_buffer;
};

template <typename T>
class PipePopPort {
public:
    inline bool can_pop() {
        return valid;
    }
    inline T & top() {
        return value;
    }
    inline void pop() {
        if (!valid) return;
        valid = false;
        accepted = true;
    }

    bool valid = false;
    T value;
    bool accepted = false;
};
template <typename T>
class PipePushPort {
public:
    inline bool can_push() {
        return ready;
    }
    inline void push(const T & value) {
        if (!ready) return;
        accepted = true;
        ready = false;
        this->value = value;
    }
    bool ready = true;
    T value;
    bool accepted = false;
};


template <typename T>
class SimpleHandshakePipe {

public:

    array<PipePopPort<T>, 1> outputs;
    array<PipePushPort<T>, 1> inputs;

    inline void apply_next_tick() {
        if (clear_flag) {
            clear_flag = false;
            outputs[0].valid = false;
            outputs[0].accepted = false;
            inputs[0].ready = true;
            inputs[0].accepted = false;
            return;
        }

        if (outputs[0].accepted) {
            outputs[0].accepted = false;
        }
        if (inputs[0].accepted && !outputs[0].valid) {
            outputs[0].valid = true;
            outputs[0].value = inputs[0].value;
            inputs[0].accepted = false;
            inputs[0].ready = true;
        }
    }

    inline void clear() {
        clear_flag = true;
    }

protected:
    bool clear_flag = false;
};

template <typename T, int Depth = 0, int InNum = 1, int OutNum = 1>
class Pipe {
public:

static_assert(InNum > 0 && OutNum > 0, "Pipe input_num and output_num must be greater than 0");
static_assert(Depth >= 0, "Pipe depth must be non-negative");

    array<PipePopPort<T>, OutNum> outputs;
    array<PipePushPort<T>, InNum> inputs;

    inline void apply_next_tick() {
        if (clear_flag) {
            clear_flag = false;
            fifo.clear();
            for(uint32 i = 0; i < OutNum; i++) {
                outputs[i].valid = false;
                outputs[i].accepted = false;
            }
            for(uint32 i = 0; i < InNum; i++) {
                inputs[i].ready = true;
                inputs[i].accepted = false;
            }
            return;
        }
        

        uint32 remained_output = 0;
        for(uint32 i = 0; i < OutNum; i++) {
            if(outputs[i].valid && outputs[i].accepted) {
                outputs[i].valid = false;
                outputs[i].accepted = false;
            }
            else if(outputs[i].valid) {
                outputs[remained_output].valid = true;
                outputs[remained_output].value = outputs[i].value;
                outputs[remained_output].accepted = false;
                remained_output++;
            }
        }

        // 1. 处理输入端，将数据推进fifo
        for (auto &input : inputs) {
            if (input.ready && input.accepted) {
                fifo.push_back(input.value);
                input.accepted = false;
            }
        }

        // 2. 处理输出端，从fifo分发数据
        for(uint32 i = remained_output; i < OutNum; i++) {
            if(!fifo.empty()) {
                outputs[i].valid = true;
                outputs[i].value = fifo.front();
                fifo.pop_front();
            } else {
                outputs[i].valid = false;
            }
        }
        // 3. 更新输入端ready信号（fifo未满才可写）
        uint32 valid_input = ((Depth + InNum) > fifo.size()) ? ((Depth + InNum) - fifo.size()) : 0;
        for(uint32 i = 0; i < InNum; i++) {
            inputs[i].ready = (i < valid_input);
        }
    }

    inline void clear() {
        clear_flag = true;
    }

protected:
    bool clear_flag = false;
    list<T> fifo;
}; 


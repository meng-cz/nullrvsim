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

#ifndef RVSIM_SIM_ERROR_H
#define RVSIM_SIM_ERROR_H

#include "common.h"

enum class SimError {
    success = 0,
    illegalinst,        // 执行了非法指令
    unsupported,        // 执行了不支持的运算参数
    devidebyzero,       // 发生了除0运算
    invalidaddr,        // 访问了页表中未定义的内存地址
    unaligned,          // 访问的地址没有按照规定对齐
    unaccessable,       // 访问的地址没有需求的权限
    invalidpc,          // Icache访问了未定义的内存地址
    unexecutable,       // Icache访问了无执行权限的内存地址
    miss,               // 目标组件中暂时没有需要的内容
    processing,         // 目标组件正在处理之前周期收到的相同的请求
    busy,               // 目标组件在本周期内无法处理更多请求
    coherence,          // 目标地址因处于一致性协议变化中而无法响应
    pagefault,          // 目标地址触发了一次缺页中断
    unconditional,      // S.C.操作获取标志位失败
    slreorder,          // 该Load指令提前获取了错误的数据,需要重新执行
    llreorder,          // 该Load指令错误的提前到了另一个同地址Load之前,需要重新执行
};

typedef struct {
    SimError    type;
    uint64_t    args[3];
} SimErrorData;

string error_name(SimError err);

#endif

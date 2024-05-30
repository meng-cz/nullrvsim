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

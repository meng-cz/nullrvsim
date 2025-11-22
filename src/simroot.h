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

#ifndef RVSIM_SIMROOT_H
#define RVSIM_SIMROOT_H

#include "common.h"

#define simroot_assert(expr) {if(!(static_cast <bool> (expr))) [[unlikely]] { simroot::dump_core(); fflush(stdout); assert (expr);}}

#define simroot_assertf(expr, fmt, ...) {if(!(static_cast <bool> (expr))) [[unlikely]] { simroot::dump_core(); printf(fmt "\n", ##__VA_ARGS__); fflush(stdout); assert (expr);}}

namespace simroot {

// --------- 模拟对象管理 ------------

void add_sim_object(SimObject *p_obj, std::string name, int latency=1);
void add_sim_object_next_thread(SimObject *p_obj, std::string name, int latency=1);

void clear_sim_object();

void start_sim();

void stop_sim_and_exit();


// --------- 日志管理 ------------

void print_log_info(std::string str);

void clear_statistic();

void dump_core();

typedef void* LogFileT;

/// @brief 创建一个log文件
/// @param path 文件路径
/// @param linecnt >0:保留最后n行。=0:无效。<0:直接输出每一行到文件
/// @return 
LogFileT create_log_file(string path, int32_t linecnt);

void log_line(LogFileT fd, string &str);
void log_line(LogFileT fd, const char* cstr);

void destroy_log_file(LogFileT fd);

void log_stdout(const char *buf, uint64_t sz);
void log_stderr(const char *buf, uint64_t sz);

// --------- 周期与时间管理 ------------

void set_current_tick(uint64_t tick);
uint64_t get_current_tick();

uint64_t get_wall_time_tick();

uint64_t get_global_freq();

uint64_t get_wall_time_freq();

uint64_t get_sim_time_us();

uint64_t get_sim_tick_per_real_sec();

}


namespace test {

bool test_simroot();

}

#endif

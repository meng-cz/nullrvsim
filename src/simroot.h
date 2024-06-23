#ifndef RVSIM_SIMROOT_H
#define RVSIM_SIMROOT_H

#include "common.h"

#define simroot_assert(expr) {if(!(static_cast <bool> (expr))) [[unlikely]] { simroot::dump_core(); fflush(stdout); __assert_fail (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION);}}

#define simroot_assertf(expr, fmt, ...) {if(!(static_cast <bool> (expr))) [[unlikely]] { simroot::dump_core(); printf(fmt, ##__VA_ARGS__); fflush(stdout); __assert_fail (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION);}}

namespace simroot {

// --------- 模拟对象管理 ------------

void add_sim_object(SimObject *p_obj, std::string name, int latency=1);

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


// --------- 周期与时间管理 ------------

uint64_t get_current_tick();

uint64_t get_wall_time_tick();

uint64_t get_global_freq();

uint64_t get_wall_time_freq();

uint64_t get_sim_time_us();


}


namespace test {

bool test_simroot();

}

#endif

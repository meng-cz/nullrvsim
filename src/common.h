#ifndef RVSIM_COMMON_H
#define RVSIM_COMMON_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <list>
#include <map>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>

#include <pthread.h>

#include "spinlocks.h"
#include "easylogging++.h"

using std::vector;
using std::list;
using std::unordered_map;
using std::unordered_multimap;
using std::set;
using std::unordered_set;
using std::make_pair;
using std::move;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::shared_ptr;
using std::make_shared;
using std::make_unique;
using std::make_pair;

typedef __int128_t int128_t;
typedef __uint128_t uint128_t;

static_assert(sizeof(uint128_t) == 16, "sizeof(uint128_t) == 16");
static_assert(sizeof(int128_t) == 16, "sizeof(int128_t) == 16");
static_assert(sizeof(uint64_t) == 8, "sizeof(uint64_t) == 8");
static_assert(sizeof(int64_t) == 8, "sizeof(int64_t) == 8");
static_assert(sizeof(uint32_t) == 4, "sizeof(uint32_t) == 4");
static_assert(sizeof(int32_t) == 4, "sizeof(int32_t) == 4");
static_assert(sizeof(uint16_t) == 2, "sizeof(uint16_t) == 2");
static_assert(sizeof(int16_t) == 2, "sizeof(int16_t) == 2");
static_assert(sizeof(uint8_t) == 1, "sizeof(uint8_t) == 1");
static_assert(sizeof(int8_t) == 1, "sizeof(int8_t) == 1");


typedef uint64_t RawDataT;
typedef RawDataT IntDataT;
typedef RawDataT FPDataT;

typedef union {
    uint64_t    u64;
    int64_t     i64;
    uint32_t    u32;
    int32_t     i32;
    uint16_t    u16;
    int16_t     i16;
    double      f64;
    float       f32;
} __internal_data_t;
#define RAW_DATA_AS(data) (*((__internal_data_t*)(&(data))))


typedef uint64_t SizeT;
typedef uint32_t Size32T;

typedef uint32_t PidT;
typedef uint16_t AsidT;

typedef uint64_t VirtAddrT;
typedef uint64_t PhysAddrT;
typedef uint64_t HostAddrT;

#define CACHE_LINE_ADDR_OFFSET (6)
static_assert(CACHE_LINE_ADDR_OFFSET > 3);
#define CACHE_LINE_LEN_BYTE (1<<CACHE_LINE_ADDR_OFFSET)
#define CACHE_LINE_LEN_I64 (1<<(CACHE_LINE_ADDR_OFFSET-3))
#define PAGE_ADDR_OFFSET (12)
static_assert(PAGE_ADDR_OFFSET > CACHE_LINE_ADDR_OFFSET);
#define PAGE_LEN_BYTE (1<<PAGE_ADDR_OFFSET)

typedef uint64_t LineAddrT;
typedef uint64_t LineIndexT;
typedef uint64_t VLineAddrT;
typedef uint64_t VLineIndexT;
typedef uint64_t PageAddrT;
typedef uint64_t PageIndexT;
typedef uint64_t VPageAddrT;
typedef uint64_t VPageIndexT;

inline LineAddrT addr_to_line_addr(PhysAddrT addr) {
    return ((addr >> CACHE_LINE_ADDR_OFFSET) << CACHE_LINE_ADDR_OFFSET);
};

inline LineIndexT addr_to_line_index(PhysAddrT addr) {
    return (addr >> CACHE_LINE_ADDR_OFFSET);
};

inline LineAddrT line_index_to_line_addr(LineIndexT index) {
    return (index << CACHE_LINE_ADDR_OFFSET);
}

inline PageAddrT addr_to_page_addr(PhysAddrT addr) {
    return ((addr >> PAGE_ADDR_OFFSET) << PAGE_ADDR_OFFSET);
};

inline PageIndexT addr_to_page_index(PhysAddrT addr) {
    return (addr >> PAGE_ADDR_OFFSET);
};

inline PageAddrT page_index_to_page_addr(PageIndexT index) {
    return (index << PAGE_ADDR_OFFSET);
};

inline void cache_line_copy(void* dst, void *src) {
    // for(int i = 0; i < (CACHE_LINE_LEN_BYTE / sizeof(uint64_t)); i++) {
    //     ((uint64_t*)dst)[i] = ((uint64_t*)src)[i];
    // }
    memcpy(dst, src, CACHE_LINE_LEN_BYTE);
}

#define PGFLAG_R        (1U<<0)
#define PGFLAG_W        (1U<<1)
#define PGFLAG_X        (1U<<2)
#define PGFLAG_PRIV     (1U<<3)
#define PGFLAG_SHARE    (1U<<4)
#define PGFLAG_ANON     (1U<<5)
#define PGFLAG_COW      (1U<<6)

#define PGFLAG_ELF      (1U<<16)
#define PGFLAG_STACK    (1U<<17)

typedef uint32_t PageFlagT;

template<typename PT, typename VT>
inline PT find_iteratable(PT begin, PT end, VT key) {
    for(;begin != end; begin++) {
        if(*begin == key) {
            return begin;
        }
    }
    return end;
}

#define RAND(from, to) ((rand()%((to)-(from)))+(from))

inline uint64_t rand_long() {
    return ((uint64_t)rand() | ((uint64_t)rand() << 32UL));
}

#define CEIL_DIV(x,y) (((x) + (y) - 1) / (y))
#define ALIGN(x,y) ((y)*CEIL_DIV((x),(y)))
#define BOEQ(a, b) ((!(a))==(!(b)))
#define INC(num, maxv) do { if(num < maxv) num++; } while(0)
#define DEC(num, minv) do { if(num > minv) num--; } while(0)

inline uint64_t get_current_time_us()
{
    struct timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    return ( cur_time.tv_sec * 1000*1000 + cur_time.tv_nsec / 1000);
}

template<typename T>
inline uint32_t pop_count(T n) {
    uint32_t cnt = 0;
    for(; n; n = (n >> ((T)1))) cnt += (n & ((T)1));
    return cnt;
}

template<typename T>
inline uint32_t count_highest_bitpos_1begin(T n) {
    uint32_t cnt = 0;
    if (n == 0) return 0;
    while (n) {
        n = (n >> 1);
        cnt++;
    }
    return cnt;
}


template<typename T>
inline void unpack_bit_position(T n, std::vector<uint32_t> *out) {
    out->clear();
    uint32_t cnt = 0;
    for(; n; n = (n >> ((T)1)), cnt++) {
        if(n & ((T)1)) {
            out->push_back(cnt);
        }
    }
}

class Avg64 {
public:
    uint64_t cnt = 0;
    double val = 0;
    inline void insert(double v) {
        val = ((v - val) / (cnt + 1) + val);
        cnt ++;
    }
};

class SimObject {
public:
    uint32_t do_on_current_tick = 1;
    uint32_t do_apply_next_tick = 1;
    virtual void on_current_tick() {};
    virtual void apply_next_tick() {};

    virtual void clear_statistic() {};
    virtual void print_statistic(std::ofstream &ofile) {};
    virtual void print_setup_info(std::ofstream &ofile) {};
    virtual void dump_core(std::ofstream &ofile) {};
};

class SimWorkload {
public:
    string file_path;
    string syscall_proxy_lib_path = "rvsrc/ecallproxy/libecallproxy.so";
    uint64_t stack_size = 8192UL * 1024UL;
    std::vector<string> argv;
    std::vector<std::string> envs;
    std::vector<string> ldpaths;
};

#endif


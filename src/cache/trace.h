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

#ifndef RVSIM_CACHE_TRACE_H
#define RVSIM_CACHE_TRACE_H

#include "common.h"
#include "simroot.h"

namespace simcache {

enum class CacheEvent {
    L1_LD_MISS,
    L1_ST_MISS,
    L1_FINISH,
    L1_TRANSMIT,
    L2_HIT,
    L2_MISS,
    L2_FORWARD,
    L2_TRANSMIT,
    L2_FINISH,
    L3_HIT,
    L3_MISS,
    L3_FORWARD,
    MEM_HANDLE,
};

class CacheEventTrace : public SimObject {

public:
    CacheEventTrace();
    ~CacheEventTrace();

    virtual void clear_statistic();
    virtual void print_statistic(std::ofstream &ofile);

    void insert_event(uint32_t trans_id, CacheEvent event);

    void cancel_transaction(uint32_t trans_id);

    uint32_t alloc_trans_id() {
        if(cur_trans_id == 0) [[unlikely]] {
            cur_trans_id = 1;
        }
        return cur_trans_id++;
    }

private:

    simroot::LogFileT log = nullptr;

    uint32_t cur_trans_id = 1;

    typedef struct {
        uint64_t    tick = 0;
        uint32_t    trans_id = 0;
        CacheEvent  event;
    } EventNode;

    unordered_map<uint32_t, vector<EventNode>> events;

    typedef struct {
        uint64_t    cnt = 0;
        uint64_t    tick = 0;
    } Statis;

    struct {
        /**
         * L1_MISS - L2_HIT - L1_FINISH
         */
        Statis  l1miss_l2hit; 

        /**
         * L1_MISS - L2_FORWARD - L1_TRANSMIT - L1_FINISH
         */
        Statis  l1miss_l2forward;
        Avg64   l1miss_l2forward_l1_l2;
        Avg64   l1miss_l2forward_l2_ol1;
        Avg64   l1miss_l2forward_ol1_l1;

        /**
         * L1_MISS - L2_MISS - L3_HIT - L2_FINISH - L1_FINISH
         */
        Statis  l1miss_l2miss_l3hit;
        Avg64   l1miss_l2miss_l1_l2;
        Avg64   l1miss_l2miss_l2_l3;
        Avg64   l1miss_l2miss_l3_l2;
        Avg64   l1miss_l2miss_l2_l1;

        /**
         * L1_MISS - L2_MISS - L3_FORWARD - L2_TRANSMIT - L2_FINISH - L1_FINISH
         */
        Statis  l1miss_l2miss_l3forward;
        Avg64   l1miss_l2miss_l3forward_l1_l2;
        Avg64   l1miss_l2miss_l3forward_l2_l3;
        Avg64   l1miss_l2miss_l3forward_l3_ol2;
        Avg64   l1miss_l2miss_l3forward_ol2_l2;
        Avg64   l1miss_l2miss_l3forward_l2_l1;

        /**
         * L1_MISS - L2_MISS - L3_MISS - MEM_HANDLE - L2_FINISH - L1_FINISH
         */
        Statis  l1miss_l2miss_l3miss;
        Avg64   l1miss_l2miss_l3miss_l1_l2;
        Avg64   l1miss_l2miss_l3miss_l2_l3;
        Avg64   l1miss_l2miss_l3miss_l3_mem;
        Avg64   l1miss_l2miss_l3miss_mem_l2;
        Avg64   l1miss_l2miss_l3miss_l2_l1;
    } statistic;

};

CacheEventTrace *get_global_cache_event_trace();

}

#endif

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



#include "cpu/pipeline5/pipeline5.h"

#include "cache/simplecc/sccl1.h"
#include "cache/simplecc/sccllc.h"
#include "cache/dma.h"

#include "sys/multicore.h"

#include "simroot.h"
#include "configuration.h"

using simcpu::CPUInterface;
using simcpu::CPUSystemInterface;
using simcpu::cpup5::PipeLine5CPU;

using simcache::CacheParam;
using simcache::DMAAsCore;
using simcache::SimpleCoherentL1;
using simcache::SimpleCoherentLLCWithMem;

namespace launch {


bool mp_scc_l1l2(std::vector<string> &argv) {

    SimWorkload workload;
    workload.argv.assign(argv.begin(), argv.end());
    workload.file_path = argv[0];
    workload.stack_size = (uint64_t)(conf::get_int("workload", "stack_size_mb", 8)) * 1024UL * 1024UL;
    string ldpath = conf::get_str("workload", "ld_path", "");
    {
        std::stringstream ss(ldpath);
        string item = "";
        while (std::getline(ss, item, ';')) {
            if (!item.empty()) {
                workload.ldpaths.push_back(item);
            }
            else {
                break;
            }
        }
    }

    uint32_t cpu_num = conf::get_int("multicore", "cpu_number", 4);
    uint64_t mem_sz = ((uint64_t)(conf::get_int("multicore", "mem_size_mb", 256))) * 1024UL * 1024UL;

    CacheParam l2param, l1dparam, l1iparam;
    l2param.set_offset = conf::get_int("llc", "blk_set_offset", 7);
    l2param.way_cnt = conf::get_int("llc", "blk_way_count", 8);
    l2param.dir_set_offset = conf::get_int("llc", "dir_set_offset", 7);
    l2param.dir_way_cnt = conf::get_int("llc", "dir_way_count", 32);
    l2param.mshr_num = conf::get_int("llc", "mshr_num", 8);
    l2param.index_latency = conf::get_int("llc", "index_cycle", 4);
    l2param.index_width = 1;

    l1dparam.set_offset = conf::get_int("l1cache", "dcache_set_offset", 5);
    l1dparam.way_cnt = conf::get_int("l1cache", "dcache_way_count", 8);
    l1dparam.mshr_num = conf::get_int("l1cache", "dcache_mshr_num", 6);
    l1dparam.index_latency = conf::get_int("l1cache", "dcache_index_latency", 2);
    l1dparam.index_width = conf::get_int("l1cache", "dcache_index_width", 1);
    l1iparam.set_offset = conf::get_int("l1cache", "icache_set_offset", 4);
    l1iparam.way_cnt = conf::get_int("l1cache", "icache_way_count", 8);
    l1iparam.mshr_num = conf::get_int("l1cache", "icache_mshr_num", 4);
    l1iparam.index_latency = conf::get_int("l1cache", "icache_index_latency", 2);
    l1iparam.index_width = conf::get_int("l1cache", "icache_index_width", 2);

    unique_ptr<SimpleCoherentLLCWithMem> l2 = make_unique<SimpleCoherentLLCWithMem>(l2param, cpu_num * 2 + 1, 32, mem_sz, 0, 12);
    vector<unique_ptr<SimpleCoherentL1>> l1s;
    l1s.resize(cpu_num * 2 + 1);

    for(uint32_t i = 0; i < (cpu_num * 2 + 1); i++) {
        if(i & 1) l1s[i] = make_unique<SimpleCoherentL1>(l1dparam, l2->get_port(i));
        else l1s[i] = make_unique<SimpleCoherentL1>(l1iparam, l2->get_port(i));
    }

    unique_ptr<SimSystemMultiCore> sys = make_unique<SimSystemMultiCore>();

    vector<unique_ptr<PipeLine5CPU>> cpus;
    cpus.resize(cpu_num);
    vector<CPUInterface*> cpu_interfaces;
    cpu_interfaces.assign(cpu_num, nullptr);

    for(uint32_t i = 0; i < cpu_num; i++) {
        cpus[i] = make_unique<PipeLine5CPU>(l1s[i*2].get(), l1s[i*2+1].get(), sys.get(), i);
        cpu_interfaces[i] = cpus[i].get();
    }

    unique_ptr<PhysPageAllocator> ppman = make_unique<PhysPageAllocator>(0, mem_sz, l2->get_pmem());

    unique_ptr<DMAAsCore> dma = make_unique<DMAAsCore>(l1s[cpu_num*2].get());
    dma->set_handler(sys.get());

    sys->init(workload, cpu_interfaces, ppman.get(), dma.get());

    char strbuf[128];

    for(uint32_t i = 0; i < cpu_num; i++) {
        sprintf(strbuf, "CPU%d", i);
        simroot::add_sim_object_next_thread(cpus[i].get(), strbuf, 1);
    }
    simroot::add_sim_object_next_thread(l2.get(), "L2cache&Mem", 1);
    simroot::add_sim_object(dma.get(), "DMA", 1);

    simroot::print_log_info("Start Simulator !!!");

    simroot::start_sim();

    return true;
}


}




#include "launch.h"

#include "cpu/xiangshan/xiangshan.h"
#include "cpu/pipeline5/pipeline5.h"

#include "cache/moesi/l1l2v2.h"
#include "cache/moesi/lastlevelcache.h"
#include "cache/moesi/dmaasl1.h"
#include "cache/moesi/memnode.h"

#include "bus/routetable.h"
#include "bus/symmulcha.h"

#include "sys/multicore.h"

#include "simroot.h"
#include "configuration.h"

using simcpu::CPUSystemInterface;
using simcpu::cpup5::PipeLine5CPU;
using simcpu::xs::XiangShanCPU;

using simcache::moesi::PrivL1L2Moesi;
using simcache::moesi::PrivL1L2MoesiL1IPort;
using simcache::moesi::PrivL1L2MoesiL1DPort;
using simcache::moesi::LLCMoesiDirNoi;
using simcache::moesi::DMAL1MoesiDirNoi;
using simcache::moesi::MemoryNode;

using simcache::get_global_cache_event_trace;
using simcache::MemCtrlLineAddrMap;

using simbus::BusNodeT;
using simbus::BusPortT;
using simbus::BusPortMapping;
using simbus::BusRouteTable;
using simbus::SymmetricMultiChannelBus;

namespace launch {

/**
   +------+  +------+  +------+                     
   |  L3  |  |  L3  |  |  L3  |                     
   |Shared|  |Shared|  |Shared|                     
   +--+---+  +--+---+  +--+---+    Double Ring Bus  
      |         |         |                         
<-----+---------+---------+---------+---------+-----
------+---------+---------+---------+---------+---->
      |         |         |         |         |     
   +--+---+  +--+---+  +--+---+ +---+----+ +--+--+  
   |  L2  |  |  L2  |  |  L2  | |  Main  | | DMA |  
   +------+  +------+  +------+ | Memory | +-----+  
   +------+  +------+  +------+ +--------+          
   |  L1  |  |  L1  |  |  L1  |                     
   +------+  +------+  +------+                     
   +------+  +------+  +------+                     
   | CPU0 |  | CPU1 |  | CPU2 |                     
   +------+  +------+  +------+                     
 */

typedef struct {
    uint32_t    cpu_num = 4;
    uint64_t    mem_sz = 0x10000000UL;
    uint32_t    mem_node_num = 1;
} MPL3Param;

class MultiCoreL3BusMapping : public BusPortMapping {

public:

    MultiCoreL3BusMapping(MPL3Param &param) : param(param) {
        simroot_assertf(param.cpu_num > 0, "MPL3Param Check: cpu_num > 0");
        simroot_assertf(param.mem_sz > 0, "MPL3Param Check: mem_sz > 0");
        simroot_assertf(param.mem_node_num > 0, "MPL3Param Check: mem_node_num > 0");
        simroot_assertf((param.mem_sz % (PAGE_LEN_BYTE * param.mem_node_num)) == 0, "MPL3Param Check: mem_sz %% PAGE_LEN_BYTE == 0");

        lindex_sz = (param.mem_sz >> CACHE_LINE_ADDR_OFFSET);

        // 安排内存节点的位置
        vector<bool> ismem;
        ismem.assign(param.cpu_num + param.mem_node_num, false);
        double step = ((double)(param.cpu_num + param.mem_node_num)) / ((double)(param.mem_node_num));
        double cur = step / 2.;
        for(uint32_t i = 0; i < param.mem_node_num; i++) {
            assert(!ismem[(uint32_t)cur]);
            ismem[(uint32_t)cur] = true;
            cur += step;
        }

        BusPortT cur_port = 0;
        for(uint32_t i = 0; i < param.cpu_num + param.mem_node_num; i++) {
            if(ismem[i]) {
                mem_ports.push_back(cur_port);
                ports.push_back(cur_port);
                port2node.push_back(i);
                cur_port++;
            }
            else {
                cpu_index.emplace(cur_port, l2_ports.size());
                l2_ports.push_back(cur_port);
                ports.push_back(cur_port);
                port2node.push_back(i);
                cur_port++;
                l3_ports.push_back(cur_port);
                ports.push_back(cur_port);
                port2node.push_back(i);
                cur_port++;
            }
        }
        dma_port = cur_port;
        cpu_index.emplace(cur_port, l2_ports.size());
        l2_ports.push_back(cur_port);
        ports.push_back(cur_port);
        port2node.push_back(param.cpu_num + param.mem_node_num);
        cur_port++;

        vector<BusNodeT> nodes;
        for(uint32_t i = 0; i <= param.cpu_num + param.mem_node_num; i++) {
            nodes.push_back(i);
        }
        simbus::genroute_double_ring(nodes, route_table);
    }

    virtual bool get_homenode_port(LineIndexT line, BusPortT *out) {
        if(line >= lindex_sz) [[unlikely]] {
            return false;
        }
        if(out) [[likely]] {
            *out = l3_ports[line % param.cpu_num];
        }
        return true;
    }

    virtual bool get_subnode_port(LineIndexT line, BusPortT *out) {
        if(line >= lindex_sz) [[unlikely]] {
            return false;
        }
        if(out) [[likely]] {
            *out = mem_ports[line % param.mem_node_num];
        }
        return true;
    }

    virtual bool get_reqnode_port(uint32_t index, BusPortT *out) {
        if(index >= l2_ports.size()) [[unlikely]] {
            return false;
        }
        if(out) [[likely]] {
            *out = l2_ports[index];
        }
        return true;
    }

    virtual bool get_reqnode_index(BusPortT port, uint32_t *out) {
        auto res = cpu_index.find(port);
        if(res == cpu_index.end()) [[unlikely]] {
            return false;
        }
        if(out) [[likely]] {
            *out = res->second;
        }
        return true;
    }

    MPL3Param param;
    uint64_t lindex_sz = 0;

    unordered_map<BusPortT, uint32_t> cpu_index;
    vector<BusPortT> l2_ports;
    vector<BusPortT> l3_ports;
    vector<BusPortT> mem_ports;
    BusPortT dma_port = 0;

    vector<BusPortT> ports;
    vector<BusNodeT> port2node;
    BusRouteTable route_table;
};

class MultiCoreL3AddrMap : public MemCtrlLineAddrMap {
public:
    MultiCoreL3AddrMap(MPL3Param &param, uint32_t myid) : param(param), id(myid) {
        simroot_assertf(param.mem_sz > 0, "MPL3Param Check: mem_sz > 0");
        simroot_assertf(param.mem_node_num > 0, "MPL3Param Check: mem_node_num > 0");
        simroot_assertf((param.mem_sz % (PAGE_LEN_BYTE * param.mem_node_num)) == 0, "MPL3Param Check: mem_sz %% PAGE_LEN_BYTE == 0");
        lindex_max = ((param.mem_sz) >> CACHE_LINE_ADDR_OFFSET);
    };
    virtual uint64_t get_local_mem_offset(LineIndexT lindex) {
        return (lindex << CACHE_LINE_ADDR_OFFSET);
    };
    virtual bool is_responsible(LineIndexT lindex) {
        return (lindex < lindex_max && (lindex % param.mem_node_num) == id);
    };

    MPL3Param param;
    uint32_t id = 0;
    LineIndexT lindex_max = 0;
};


bool mp_moesi_l3(std::vector<string> &argv) {
    SimWorkload workload;
    workload.argv.assign(argv.begin(), argv.end());
    workload.file_path = argv[0];
    workload.stack_size = (uint64_t)(conf::get_int("workload", "stack_size_mb", 8)) * 1024UL * 1024UL;
    workload.envs.emplace_back(generate_ld_library_path_str());
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

    MPL3Param param;
    param.cpu_num = conf::get_int("multicore", "cpu_number", 4);
    param.mem_node_num = conf::get_int("multicore", "mem_node_num", 1);
    param.mem_sz = ((uint64_t)(conf::get_int("multicore", "mem_size_mb", 1024))) * 1024UL * 1024UL;
    
    MultiCoreL3BusMapping busmap(param);

    vector<uint32_t> cha_width(simcache::moesi::CHANNEL_CNT);
    cha_width[simcache::moesi::CHANNEL_ACK] = simcache::moesi::CHANNEL_WIDTH_ACK;
    cha_width[simcache::moesi::CHANNEL_RESP] = simcache::moesi::CHANNEL_WIDTH_RESP;
    cha_width[simcache::moesi::CHANNEL_REQ] = simcache::moesi::CHANNEL_WIDTH_REQ;
    unique_ptr<SymmetricMultiChannelBus> bus = make_unique<SymmetricMultiChannelBus>(
        busmap.ports, busmap.port2node, cha_width, busmap.route_table, "Bus"
    );
    simroot::add_sim_object(bus.get(), "Bus", 1);
    
    uint8_t *pmem = new uint8_t[param.mem_sz];
    unique_ptr<PhysPageAllocator> ppman = make_unique<PhysPageAllocator>(0UL, param.mem_sz, pmem);

    vector<unique_ptr<MultiCoreL3AddrMap>> mem_addr_maps(param.mem_node_num);
    vector<unique_ptr<MemoryNode>> mem_nodes(param.mem_node_num);
    for(uint32_t i = 0; i < param.mem_node_num; i++) {
        mem_addr_maps[i] = make_unique<MultiCoreL3AddrMap>(param, i);
        mem_nodes[i] = std::make_unique<MemoryNode>(
            pmem, mem_addr_maps[i].get(), bus.get(), busmap.mem_ports[i], 32, get_global_cache_event_trace()
        );
        simroot::add_sim_object(mem_nodes[i].get(), "MemoryNode" + to_string(i), 1);
        
    }

    simcache::CacheParam cp;
    cp.set_offset = conf::get_int("llc", "blk_set_offset", 9);
    cp.way_cnt = conf::get_int("llc", "blk_way_count", 8);
    cp.dir_set_offset = conf::get_int("llc", "dir_set_offset", 9);
    cp.dir_way_cnt = conf::get_int("llc", "dir_way_count", 32);
    cp.mshr_num = conf::get_int("llc", "mshr_num", 8);
    cp.index_latency = conf::get_int("llc", "index_cycle", 10);
    cp.index_width = 1;
    cp.nuca_num = param.cpu_num;
    cp.nuca_index = 0;

    vector<unique_ptr<LLCMoesiDirNoi>> l3s(param.cpu_num);
    for(uint32_t i = 0; i < param.cpu_num; i++) {
        cp.nuca_index = i;
        l3s[i] = make_unique<LLCMoesiDirNoi>(
            cp, bus.get(), busmap.l3_ports[i], &busmap, "L3Cache" + to_string(i), get_global_cache_event_trace()
        );
        simroot::add_sim_object(l3s[i].get(), "L3Cache" + to_string(i), 1);
    }

    unique_ptr<SimSystemMultiCore> simsys = make_unique<SimSystemMultiCore>();

    simcache::CacheParam icp, dcp;
    dcp.set_offset = conf::get_int("l1cache", "dcache_set_offset", 5);
    dcp.way_cnt = conf::get_int("l1cache", "dcache_way_count", 8);
    dcp.mshr_num = conf::get_int("l1cache", "dcache_mshr_num", 6);
    dcp.index_latency = conf::get_int("l1cache", "dcache_index_latency", 2);
    dcp.index_width = conf::get_int("l1cache", "dcache_index_width", 1);
    icp.set_offset = conf::get_int("l1cache", "icache_set_offset", 4);
    icp.way_cnt = conf::get_int("l1cache", "icache_way_count", 8);
    icp.mshr_num = conf::get_int("l1cache", "icache_mshr_num", 4);
    icp.index_latency = conf::get_int("l1cache", "icache_index_latency", 2);
    icp.index_width = conf::get_int("l1cache", "icache_index_width", 2);

    cp.set_offset = conf::get_int("l2cache", "set_offset", 7);
    cp.way_cnt = conf::get_int("l2cache", "way_count", 8);
    cp.mshr_num = conf::get_int("l2cache", "mshr_num", 8);
    cp.index_latency = conf::get_int("l2cache", "index_latency", 4);
    cp.index_width = conf::get_int("l2cache", "index_width", 1);
    cp.nuca_index = 0;
    cp.nuca_num = 1;

    vector<unique_ptr<PrivL1L2Moesi>> l2s(param.cpu_num);
    vector<unique_ptr<PrivL1L2MoesiL1IPort>> l1is(param.cpu_num);
    vector<unique_ptr<PrivL1L2MoesiL1DPort>> l1ds(param.cpu_num);

    for(uint32_t i = 0; i < param.cpu_num; i++) {
        l2s[i] = make_unique<PrivL1L2Moesi>(
            cp, dcp, icp, bus.get(), busmap.l2_ports[i], &busmap, "L2Cache" + to_string(i), get_global_cache_event_trace()
        );
        simroot::add_sim_object(l2s[i].get(), "L2Cache" + to_string(i), 1);
        l1is[i] = make_unique<PrivL1L2MoesiL1IPort>(l2s[i].get());
        l1ds[i] = make_unique<PrivL1L2MoesiL1DPort>(l2s[i].get());
    }

    vector<unique_ptr<CPUInterface>> cpus(param.cpu_num);
    string cpu_type = conf::get_str("sys", "cpu_type", "pipeline5");
    if(cpu_type.compare("pipeline5") == 0) {
        for(uint32_t i = 0; i < param.cpu_num; i++) {
            cpus[i] = make_unique<PipeLine5CPU>(l1is[i].get(), l1ds[i].get(), simsys.get(), i);
        }
    }
    else if(cpu_type.compare("xiangshan") == 0) {
        for(uint32_t i = 0; i < param.cpu_num; i++) {
            cpus[i] = make_unique<XiangShanCPU>(l1is[i].get(), l1ds[i].get(), simsys.get(), i);
        }
    }
    else {
        LOG(ERROR) << "Unknown cpu_type : " << cpu_type;
        assert(0);
    }

    for(uint32_t i = 0; i < param.cpu_num; i++) {
        cpus[i]->halt();
        simroot::add_sim_object(cpus[i].get(), "CPU" + to_string(i), 1);
    }

    unique_ptr<DMAL1MoesiDirNoi> dma = std::make_unique<DMAL1MoesiDirNoi>(bus.get(), busmap.dma_port, &busmap);
    dma->set_handler(simsys.get());
    simroot::add_sim_object(dma.get(), "DMA", 1);

    vector<CPUInterface*> cpu_ptrs(param.cpu_num);
    for(uint32_t i = 0; i < param.cpu_num; i++) {
        cpu_ptrs[i] = cpus[i].get();
    }
    simsys->init(workload, cpu_ptrs, ppman.get(), dma.get());

    simroot::print_log_info("Start Simulator !!!");

    simroot::start_sim();

    delete[] pmem;

    return true;
}




}

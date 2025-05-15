
#include "launch.h"

#include "cpu/xiangshan/xiangshan.h"
#include "cpu/pipeline5/pipeline5.h"

#include "cache/moesi/l1cachev2.h"
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

using simcache::moesi::L1CacheMoesiDirNoiV2;
using simcache::moesi::LLCMoesiDirNoi;
using simcache::moesi::DMAL1MoesiDirNoi;
using simcache::moesi::MemoryNode;

using simcache::MemCtrlLineAddrMap;

using simbus::BusPortT;
using simbus::BusPortMapping;
using simbus::BusRouteTable;
using simbus::SymmetricMultiChannelBus;

namespace launch {

/**
                Double Ring Bus                 
<-----+--------+--------------+-----------+-------
------+--------+--------------+-----------+------>
      |        |              |           |       
   +--+---+ +--+---+      +---+----+ +----+----+  
   |  L1  | |  L1  | ...  |   L2   | |   MEM   |  
   +------+ +------+      +--------+ +---------+  
   +------+ +------+                              
   | CPU0 | | CPU1 | ...                          
   +------+ +------+                              
 */

typedef struct {
    uint32_t    cpu_num = 4;
    bool        has_dma = true;
    PhysAddrT   mem_base = 0UL;
    uint64_t    mem_sz = 0x10000000UL;
    uint32_t    l2_slice_num = 1;
    uint32_t    mem_ctrl_num = 1;
} MPL1L2Param;

class MultiCoreL1L2BusMapping : public BusPortMapping {
public:
    MultiCoreL1L2BusMapping(MPL1L2Param &param) : param(param) {
        assert(param.cpu_num);
        assert(param.mem_sz);
        assert(param.l2_slice_num);
        assert(param.mem_ctrl_num);
        BusPortT cur = 0;
        for(int i = 0; i < param.mem_ctrl_num; i++) mem_ports.push_back(cur++);
        for(int i = 0; i < param.l2_slice_num; i++) l2_ports.push_back(cur++);
        for(int i = 0; i < param.cpu_num * 2; i++) {
            cpu_index.insert(std::make_pair(cur, i));
            l1_ports.push_back(cur++);
        };
        if(param.has_dma) {
            cpu_index.insert(std::make_pair(cur, param.cpu_num * 2));
            l1_ports.push_back(cur++);
        }
        port_num = cur;
    };
    inline uint32_t get_port_num() { return port_num; }

    virtual bool get_homenode_port(LineIndexT line, BusPortT *out) {
        if((line >> CACHE_LINE_ADDR_OFFSET) >= (param.mem_base + param.mem_sz) || (line >> CACHE_LINE_ADDR_OFFSET) < param.mem_base) {
            return false;
        }
        if(out) *out = l2_ports[line % param.l2_slice_num];
        return true;
    }
    virtual bool get_subnode_port(LineIndexT line, BusPortT *out) {
        if((line >> CACHE_LINE_ADDR_OFFSET) >= (param.mem_base + param.mem_sz) || (line >> CACHE_LINE_ADDR_OFFSET) < param.mem_base) {
            return false;
        }
        if(out) *out = mem_ports[line % param.mem_ctrl_num];
        return true;
    }
    virtual bool get_reqnode_port(uint32_t index, BusPortT *out) {
        if(index >= l1_ports.size()) {
            return false;
        }
        if(out) *out = l1_ports[index];
        return true;
    }
    virtual bool get_reqnode_index(BusPortT port, uint32_t *out) {
        auto res = cpu_index.find(port);
        if(res == cpu_index.end()) {
            return false;
        }
        if(out) *out = res->second;
        return true;
    }
private:
    MPL1L2Param param;
    std::vector<BusPortT> l1_ports;
    std::vector<BusPortT> l2_ports;
    std::vector<BusPortT> mem_ports;
    std::unordered_map<BusPortT, uint32_t> cpu_index;
    uint32_t port_num = 0;
};

class MultiCoreL1L2AddrMap : public MemCtrlLineAddrMap {
public:
    MultiCoreL1L2AddrMap(MPL1L2Param &param, uint32_t myid) : param(param), id(myid) {
        assert(param.mem_base == 0 || (param.mem_base & (PAGE_LEN_BYTE - 1)) == 0);
        assert((param.mem_sz & (PAGE_LEN_BYTE - 1)) == 0);
        lindex_base = (param.mem_base >> CACHE_LINE_ADDR_OFFSET);
        lindex_max = ((param.mem_base + param.mem_sz) >> CACHE_LINE_ADDR_OFFSET);
    };
    virtual uint64_t get_local_mem_offset(LineIndexT lindex) {
        return (((lindex - lindex_base) / param.mem_ctrl_num) << CACHE_LINE_ADDR_OFFSET);
    };
    virtual bool is_responsible(LineIndexT lindex) {
        return (lindex < lindex_max && lindex >= lindex_base && (lindex % param.mem_ctrl_num) == id);
    };
private:
    MPL1L2Param param;
    uint32_t id = 0;
    LineIndexT lindex_base = 0;
    LineIndexT lindex_max = 0;
};


bool mp_moesi_l1l2(std::vector<string> &argv) {
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

    MPL1L2Param param;
    param.cpu_num = conf::get_int("multicore", "cpu_number", 4);
    param.has_dma = true;
    param.mem_base = 0UL;
    param.mem_ctrl_num = 1;
    param.mem_sz = ((uint64_t)(conf::get_int("multicore", "mem_size_mb", 256))) * 1024UL * 1024UL;
    param.l2_slice_num = 1;
    
    MultiCoreL1L2BusMapping busmap(param);

    vector<BusPortT> nodes;
    for(int i = 0; i < busmap.get_port_num(); i++) nodes.push_back(i);
    vector<uint32_t> cha_width;
    cha_width.assign(simcache::moesi::CHANNEL_CNT, 64);
    BusRouteTable route;
    simbus::genroute_double_ring(nodes, route);

    unique_ptr<SymmetricMultiChannelBus> bus = make_unique<SymmetricMultiChannelBus>(
        nodes, nodes, cha_width, route, "Bus"
    );
    simroot::add_sim_object(bus.get(), "Bus", 1);
    
    uint8_t *pmem = new uint8_t[param.mem_sz];
    unique_ptr<PhysPageAllocator> ppman = make_unique<PhysPageAllocator>(0UL, param.mem_sz, pmem);

    BusPortT busport;

    MultiCoreL1L2AddrMap memaddrmap(param, 0);
    assert(busmap.get_subnode_port(0, &busport));
    std::unique_ptr<MemoryNode> memory =  std::make_unique<MemoryNode>(pmem, &memaddrmap, bus.get(), busport, 32, nullptr);
    simroot::add_sim_object(memory.get(), "MemoryNode", 1);

    simcache::CacheParam cp;
    cp.set_offset = conf::get_int("llc", "blk_set_offset", 7);
    cp.way_cnt = conf::get_int("llc", "blk_way_count", 8);
    cp.dir_set_offset = conf::get_int("llc", "dir_set_offset", 7);
    cp.dir_way_cnt = conf::get_int("llc", "dir_way_count", 32);
    cp.mshr_num = conf::get_int("llc", "mshr_num", 8);
    cp.index_latency = conf::get_int("llc", "index_cycle", 4);
    cp.index_width = 1;

    assert(busmap.get_homenode_port(0, &busport));
    std::unique_ptr<LLCMoesiDirNoi> l2 =  std::make_unique<LLCMoesiDirNoi>(
        cp, bus.get(), busport, &busmap, "l2cache", nullptr
    );
    simroot::add_sim_object(l2.get(), "L2Cache", 1);

    std::unique_ptr<SimSystemMultiCore> simsys = std::make_unique<SimSystemMultiCore>();

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

    char namebuf[64];

    std::vector<std::unique_ptr<L1CacheMoesiDirNoiV2>> l1ds;
    std::vector<std::unique_ptr<L1CacheMoesiDirNoiV2>> l1is;
    for(uint32_t i = 0; i < param.cpu_num; i++) {
        assert(busmap.get_reqnode_port(i*2, &busport));
        sprintf(namebuf, "L1DCache%d", i);
        l1ds.emplace_back(std::move(std::make_unique<L1CacheMoesiDirNoiV2>(
            dcp, bus.get(), busport, &busmap, namebuf
        )));
        simroot::add_sim_object(l1ds.back().get(), namebuf, 0);
        assert(busmap.get_reqnode_port(i*2 + 1, &busport));
        sprintf(namebuf, "L1ICache%d", i);
        l1is.emplace_back(std::move(std::make_unique<L1CacheMoesiDirNoiV2>(
            icp, bus.get(), busport, &busmap, namebuf
        )));
        simroot::add_sim_object(l1is.back().get(), namebuf, 0);
    }

    std::vector<CPUInterface*> cpu_ptrs;
    cpu_ptrs.assign(param.cpu_num, nullptr);

    string cpu_type = conf::get_str("sys", "cpu_type", "pipeline5");

    if(cpu_type.compare("pipeline5") == 0) {
        for(uint32_t i = 0; i < param.cpu_num; i++) {
            cpu_ptrs[i] = new PipeLine5CPU(l1is[i].get(), l1ds[i].get(), simsys.get(), i);
        }
    }
    else if(cpu_type.compare("xiangshan") == 0) {
        for(uint32_t i = 0; i < param.cpu_num; i++) {
            cpu_ptrs[i] = new XiangShanCPU(l1is[i].get(), l1ds[i].get(), simsys.get(), i);
        }
    }
    else {
        LOG(ERROR) << "Unknown cpu_type : " << cpu_type;
        assert(0);
    }

    for(uint32_t i = 0; i < param.cpu_num; i++) {
        sprintf(namebuf, "CPU%d", i);
        cpu_ptrs[i]->halt();
        simroot::add_sim_object(cpu_ptrs[i], namebuf, 1);
    }


    assert(busmap.get_reqnode_port(param.cpu_num*2, &busport));
    std::unique_ptr<DMAL1MoesiDirNoi> simdma = std::make_unique<DMAL1MoesiDirNoi>(bus.get(), busport, &busmap);
    simdma->set_handler(simsys.get());
    simroot::add_sim_object(simdma.get(), "DMA", 1);

    simsys->init(workload, cpu_ptrs, ppman.get(), simdma.get());

    simroot::print_log_info("Start Simulator !!!");

    simroot::start_sim();

    delete[] pmem;

    return true;
}

}



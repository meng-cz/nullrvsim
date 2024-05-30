
#include "test_amo.h"

#include "common.h"

#include "cpu/pipeline5/pipeline5.h"

#include "cache/moesi/l1cache.h"
#include "cache/moesi/lastlevelcache.h"

#include "mem/memnode.h"

#include "bus/simplebus.h"

#include "simroot.h"
#include "configuration.h"

using simcpu::CPUSystemInterface;
using simcpu::cpup5::PipeLine5CPU;

using simcache::moesi::L1CacheMoesiDirNoi;
using simcache::moesi::LLCMoesiDirNoi;

using simmem::MemCtrlLineAddrMap;
using simmem::MemoryNode;

using simbus::BusPortT;
using simbus::BusPortMapping;
using simbus::SimpleBus;

namespace test {

// AMOAddTest
//  688:	6641                	lui	a2,0x10
//  68a:	4885                	li	a7,1
//  68c:	0116352f          	amoadd.d	a0,a7,(a2)
//  690:	00000073          	ecall
//  694:	9002                	ebreak

// SpinLockTest
//     0x0a13, // li	s4,100
//     0x0640, 
//     0x69c1, // lui	s3,0x10
//     0x4885, // li	a7,1
//     0xa031, // li	j	69c <__loop_start>
// // 0000000000000692 <__lock_loop_start>:
//     0x0001, // nop
//     0x0001, // nop
//     0x0001, // nop
//     0x0001, // nop
//     0x0001, // nop
// // 000000000000069c <__loop_start>:
//     0xa42f, // amoor.w	s0,a7,(s3)
//     0x4119,
//     0xf86d, // bnez	s0,692 <__lock_loop_start>
//     0xb503, // ld	a0,8(s3)
//     0x0089,
//     0x0505, // addi	a0,a0,1
//     0xb423, // sd	a0,8(s3)
//     0x00a9,
//     0x0073, // ecall
//     0x0000,
//     0xa023, // sw	zero,0(s3)
//     0x0009,
//     0x1a7d, // addi	s4,s4,-1
//     0x13e3, // bnez	s4,69c <__loop_start>
//     0xfe0a,
//     0x9002, // ebreak

// SpinLockLRSCTest
//  680:	06400a13          	li	s4,100
//  684:	69c1                	lui	s3,0x10
//  686:	4885                	li	a7,1
//  688:	a819                	j	69e <__loop_start>
// 000000000000068a <__lock_loop_start>:
//  68a:	0001                	nop
//  68c:	0001                	nop
//  68e:	0001                	nop
//  690:	0001                	nop
//  692:	0001                	nop
//  694:	0001                	nop
//  696:	0001                	nop
//  698:	0001                	nop
//  69a:	0001                	nop
//  69c:	0001                	nop
// 000000000000069e <__loop_start>:
//  69e:	1009a42f          	lr.w	s0,(s3)
//  6a2:	f465                	bnez	s0,68a <__lock_loop_start>
//  6a4:	1919a42f          	sc.w	s0,a7,(s3)
//  6a8:	f06d                	bnez	s0,68a <__lock_loop_start>
//  6aa:	0089b503          	ld	a0,8(s3) # 10008 <__global_pointer$+0xd808>
//  6ae:	0505                	addi	a0,a0,1
//  6b0:	00a9b423          	sd	a0,8(s3)
//  6b4:	00000073          	ecall
//  6b8:	0009a023          	sw	zero,0(s3)
//  6bc:	1a7d                	addi	s4,s4,-1 # ffffffffffff9fff <__global_pointer$+0xffffffffffff77ff>
//  6be:	fe0a10e3          	bnez	s4,69e <__loop_start>
//  6c2:	9002                	ebreak

const uint16_t test_amo_rom[] = {
    0x0a13, // li	s4,100
    0x0640, 
    0x69c1, // lui	s3,0x10
    0x4885, // li	a7,1
    0xa031, // j	69e <__loop_start>
// 0000000000000692 <__lock_loop_start>:
    0x0001, // nop
    0x0001, // nop
    0x0001, // nop
    0x0001, // nop
    0x0001, // nop
// 000000000000069c <__loop_start>:
    0xa42f, // lr.w	s0,(s3)
    0x1009,
    0xf86d, // bnez	s0,68a <__lock_loop_start>
    0xa42f, // sc.w	s0,a7,(s3)
    0x1919,
    0xf475, // bnez	s0,68a <__lock_loop_start>
    0xb503, // ld	a0,8(s3)
    0x0089,
    0x0505, // addi	a0,a0,1
    0xb423, // sd	a0,8(s3)
    0x00a9,
    0x0073, // ecall
    0x0000,
    0xa023, // sw	zero,0(s3)
    0x0009,
    0x1a7d, // addi	s4,s4,-1
    0x10e3, // bnez	s4,69e <__loop_start>
    0xfe0a,
    0x9002, // ebreak
};

class TestAMOInterface : public CPUSystemInterface, public BusPortMapping, public MemCtrlLineAddrMap {
public:

    uint32_t corenum = 4;
    uint64_t memsz = 0x1000000UL;
    uint8_t *prom = nullptr;

    SimpleBus *bus = nullptr;

    uint8_t *pmem = nullptr;
    MemoryNode *memory = nullptr;

    LLCMoesiDirNoi *l2 = nullptr;
    std::vector<L1CacheMoesiDirNoi*> l1ds;
    std::vector<L1CacheMoesiDirNoi*> l1is;
    std::vector<PipeLine5CPU*> cpus;

    std::vector<bool> is_halt;
    DefaultLock lock;
    char log_buf[128];

    TestAMOInterface() {
        prom = new uint8_t[memsz];
        memset(prom, 0, memsz);
        memcpy(prom, test_amo_rom, sizeof(test_amo_rom));

        bus = new SimpleBus(corenum * 2 + 2, 32);

        simroot::add_sim_object(bus, "Bus", 1);

        pmem = new uint8_t[memsz];
        memset(pmem, 0, memsz);
        memory = new MemoryNode(pmem, this, bus, 1, 32);

        simroot::add_sim_object(memory, "MemoryNode", 1);

        l2 = new LLCMoesiDirNoi(
            conf::get_int("llc", "blk_set_offset", 7),
            conf::get_int("llc", "blk_way_count", 8),
            conf::get_int("llc", "dir_set_offset", 7),
            conf::get_int("llc", "dir_way_count", 8),
            bus, 0, this, "l2cache"
        );

        simroot::add_sim_object(l2, "L2Cache", 1);

        std::vector<uint64_t> init_regs;
        init_regs.assign(64, 0);
        init_regs[0] = memsz; // pc
        for(uint32_t i = 0; i < corenum; i++) {
            l1ds.push_back(new L1CacheMoesiDirNoi(
                conf::get_int("l1cache", "dcache_set_offset", 5),
                conf::get_int("l1cache", "dcache_way_count", 8),
                conf::get_int("l1cache", "dcache_mshr_num", 6),
                bus, 2*i + 2, this, "l1dcache"
            ));
            l1is.push_back(new L1CacheMoesiDirNoi(
                conf::get_int("l1cache", "icache_set_offset", 4),
                conf::get_int("l1cache", "icache_way_count", 8),
                conf::get_int("l1cache", "icache_mshr_num", 4),
                bus, 2*i + 3, this, "l1icache"
            ));
            init_regs[isa::ireg_index_of("a0")] = i;
            cpus.push_back(new PipeLine5CPU(
                l1is.back(),
                l1ds.back(),
                this, i, init_regs
            ));
            sprintf(log_buf, "CPU%d", i);
            simroot::add_sim_object(cpus.back(), log_buf, 1);
        }
        is_halt.assign(corenum, false);
    };

    ~TestAMOInterface() {
        for(auto p : cpus) delete p;
        for(auto p : l1ds) delete p;
        for(auto p : l1is) delete p;
        delete l2;
        delete memory;
        delete[] pmem;
        delete bus;
        delete[] prom;
    }

    virtual SimError v_to_p(uint32_t cpu_id, VirtAddrT addr, PhysAddrT *out, PageFlagT flg) { if(out) *out = addr; return SimError::success; }
    virtual uint32_t is_dev_mem(uint32_t cpu_id, uint64_t addr) { return ((addr >= memsz) && (addr < memsz * 2)); }
    virtual bool dev_input(uint32_t cpu_id, VirtAddrT addr, uint32_t len, void *buf) {
        assert(is_dev_mem(cpu_id, addr));
        uint8_t *ptr = prom + (addr - memsz);
        memcpy(buf, ptr, len);
        return true;
    }
    virtual bool dev_output(uint32_t cpu_id, VirtAddrT addr, uint32_t len, void *buf) {
        assert(is_dev_mem(cpu_id, addr));
        uint8_t *ptr = prom + (addr - memsz);
        memcpy(ptr, buf, len);
        return true;
    }
    virtual uint64_t syscall(uint32_t cpu_id, uint64_t addr, RVRegArray &iregs) {
        uint64_t syscallid = iregs[isa::ireg_index_of("a7")];
        uint64_t arg = iregs[isa::ireg_index_of("a0")];
        lock.lock();
        sprintf(log_buf, "CPU %d: Raise a ecall %ld, arg: %ld", cpu_id, syscallid, arg);
        simroot::print_log_info(log_buf);
        lock.unlock();
        return addr + 4;
    }
    virtual uint64_t ebreak(uint32_t cpu_id, uint64_t addr, RVRegArray &iregs) {
        lock.lock();
        sprintf(log_buf, "CPU %d: Raise a ebreak", cpu_id);
        simroot::print_log_info(log_buf);
        cpus[cpu_id]->halt();
        is_halt[cpu_id] = true;
        bool finish = true;
        for(auto b : is_halt) if(!b) finish = false;
        lock.unlock();
        if(finish) {
            simroot::stop_sim_and_exit();
        }
        return 0;
    }

    virtual bool get_uplink_port(LineIndexT line, BusPortT *out) { if(out) *out = 0; return (line < (memsz >> CACHE_LINE_ADDR_OFFSET)); }
    virtual bool get_memory_port(LineIndexT line, BusPortT *out) { if(out) *out = 1; return (line < (memsz >> CACHE_LINE_ADDR_OFFSET)); }
    virtual bool get_downlink_port(uint32_t index, BusPortT *out) { if(out) *out = index + 2; return (index < corenum * 2); }
    virtual bool get_downlink_port_index(BusPortT port, uint32_t *out)  { if(out) *out = port - 2; return (port >= 2); }

    virtual uint64_t get_local_mem_offset(LineIndexT lindex) {return (lindex << CACHE_LINE_ADDR_OFFSET); }
    virtual bool is_responsible(LineIndexT lindex) {return (lindex < (memsz >> CACHE_LINE_ADDR_OFFSET)); }

};

bool test_amo() {
    
    TestAMOInterface testamo;
    simroot::start_sim();

    printf("Test AMO Finished!!!\n");
    return true;
}




}


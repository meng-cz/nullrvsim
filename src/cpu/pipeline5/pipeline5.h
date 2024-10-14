#ifndef RVSIM_CPU_PIPELINE5_H
#define RVSIM_CPU_PIPELINE5_H

#include "common.h"
#include "simroot.h"

#include "cpu/cpuinterface.h"
#include "cpu/isa.h"

#include "cache/cacheinterface.h"

namespace simcpu {

namespace cpup5 {

using isa::RV64InstDecoded;

using simcache::CacheInterface;

class PipeLine5CPU : public CPUInterface {
public:

    PipeLine5CPU(
        CacheInterface *icache_port,
        CacheInterface *dcache_port,
        CPUSystemInterface *sys_port,
        uint32_t id,
        VirtAddrT entry,
        VirtAddrT sp
    )
    :
    io_icache_port(icache_port),
    io_dcache_port(dcache_port),
    io_sys_port(sys_port),
    cpu_id(id) {
            
        pc = entry;
        iregs.setu(2, sp);

        init_all();
    };

    PipeLine5CPU(
        CacheInterface *icache_port,
        CacheInterface *dcache_port,
        CPUSystemInterface *sys_port,
        uint32_t id,
        std::vector<uint64_t> &init_regs
    )
    :
    io_icache_port(icache_port),
    io_dcache_port(dcache_port),
    io_sys_port(sys_port),
    cpu_id(id) {
        
        pc = init_regs[0];
        for(int i = 1; i < 32; i++) {
            iregs.setu(i, init_regs[i]);
        }

        init_all();
    }

    PipeLine5CPU(
        CacheInterface *icache_port,
        CacheInterface *dcache_port,
        CPUSystemInterface *sys_port,
        uint32_t id
    )
    :
    io_icache_port(icache_port),
    io_dcache_port(dcache_port),
    io_sys_port(sys_port),
    cpu_id(id) {
        init_all();
        is_halt = true;
    }

    ~PipeLine5CPU();

    virtual void halt() {
        apply_halt = true;
    }
    virtual void redirect(VirtAddrT addr, RVRegArray &regs) {
        apply_pc_redirect = true;
        pc_redirect = addr;
        apply_cpu_wakeup = true;
        if(regs.size() >= RV_REG_CNT_INT) {
            memcpy(apply_ireg_buf, regs.data(), sizeof(uint64_t) * RV_REG_CNT_INT);
            apply_iregs = true;
        }
        if(regs.size() >= (RV_REG_CNT_INT + RV_REG_CNT_FP)) {
            memcpy(apply_freg_buf, regs.data() + RV_REG_CNT_INT, sizeof(uint64_t) * RV_REG_CNT_FP);
            apply_fregs = true;
        }
    };

    void ifence() {
        simroot_assert(0);
    };

    virtual void on_current_tick();
    virtual void apply_next_tick();

    virtual void clear_statistic();
    virtual void print_statistic(std::ofstream &ofile);
    virtual void print_setup_info(std::ofstream &ofile);
    virtual void dump_core(std::ofstream &ofile);

    void debug_print_regs();
    VirtAddrT debug_get_pc() {return pc;}

protected:

    bool is_halt = false;

    uint32_t cpu_id;

    CacheInterface *io_icache_port;
    CacheInterface *io_dcache_port;
    CPUSystemInterface *io_sys_port;
    
    void init_all();

    typedef struct {
        VirtAddrT pc;
        RVInstT inst_raw;
    } InstRaw;

    typedef struct {
        RV64InstDecoded inst;
        RawDataT arg0;
        RawDataT arg1;
        RawDataT vaddr;
        SimError err = SimError::success;
        bool passp3 = false;
        bool passp4 = false;
        uint64_t mem_start_tick = 0;
        uint64_t mem_finish_tick = 0;
        bool cache_missed = false;
    } P5InstDecoded;

    typedef struct {
        uint32_t busy = false;
        IntDataT value;
    } Register64;

    typedef struct {
        uint32_t busy = false;
        FPDataT value;
    } FRegister64;

    class Pipeline5IntRegs {
    public:
        Register64 reg[32];
        inline bool blocked(uint32_t index) { if(index > 0) {return reg[index].busy;} else {return false;} }
        inline void block(uint32_t index) { if(index > 0) {reg[index].busy++;} }
        inline IntDataT get(uint32_t index) { if(index > 0) {return reg[index].value;} else {return 0;} }
        inline uint64_t getu(uint32_t index) { if(index > 0) {return reg[index].value;} else {return 0;} }
        inline int64_t gets(uint32_t index) { if(index > 0) {return RAW_DATA_AS(reg[index].value).i64;} else {return 0;} }
        inline void setu(uint32_t index, uint64_t v) { if(index > 0) {reg[index].value = v; if(reg[index].busy) reg[index].busy--;} }
        inline void sets(uint32_t index, int64_t v) { if(index > 0) {RAW_DATA_AS(reg[index].value).i64 = v; if(reg[index].busy) reg[index].busy--;} }
    };
    Pipeline5IntRegs iregs;

    class Pipeline5FpRegs {
    public:
        FRegister64 reg[32];
        inline bool blocked(uint32_t index) { return reg[index].busy; }
        inline void block(uint32_t index) { reg[index].busy++; }
        inline FPDataT get(uint32_t index) { return reg[index].value; }
        inline uint64_t getb(uint32_t index) { return reg[index].value; }
        inline double getd(uint32_t index) { return RAW_DATA_AS(reg[index].value).f64; }
        inline float getf(uint32_t index) { return RAW_DATA_AS(reg[index].value).f32; }
        inline void setb(uint32_t index, uint64_t v) { reg[index].value = v; if(reg[index].busy) reg[index].busy--; }
        inline FRegister64& at(uint32_t index) { return reg[index]; };
    };
    Pipeline5FpRegs fregs;

    uint64_t csr_fcsr = 0UL;

    VirtAddrT pc = 0;

    PhysAddrT satp = 0;
    AsidT asid = 0;

    // P1
    VirtAddrT p1_apply_next_pc = 0;
    std::pair<bool, InstRaw> p1_result;
    
    bool p1_wait_for_jump = false;

    // P2
    std::pair<bool, InstRaw> p2_workload;
    std::pair<bool, P5InstDecoded> p2_result;

    // P3
    // typedef struct {
    //     uint32_t reg_index = 0;
    //     bool    is_fp = false;
    //     uint64_t value = 0;
    // } P3WritebackBypass;

    std::pair<bool, P5InstDecoded> p3_workload;
    std::pair<bool, P5InstDecoded> p3_result;

    // P4
    std::pair<bool, P5InstDecoded> p4_workload;
    std::pair<bool, P5InstDecoded> p4_result;

    // P5
    std::pair<bool, P5InstDecoded> p5_workload;
    std::pair<bool, P5InstDecoded> p5_result;

    // Control
    bool current_tlb_occupy_dcache = false;

    bool apply_cpu_wakeup = false;
    bool apply_halt = false;

    bool apply_pc_redirect = false;
    VirtAddrT pc_redirect = 0;
    bool apply_iregs = false;
    uint64_t apply_ireg_buf[RV_REG_CNT_INT];
    bool apply_fregs = false;
    uint64_t apply_freg_buf[RV_REG_CNT_FP];

    bool apply_new_pagetable = false;
    PhysAddrT new_pgtable = 0;
    AsidT new_asid = 0;

    void process_pipeline_1_fetch();
    void process_pipeline_2_decode();
    void process_pipeline_3_execute();
    void process_pipeline_4_memory();
    void process_pipeline_5_commit();

    inline void clear_pipeline() {
        p1_result.first = false;
        p2_workload.first = false;
        p2_result.first = false;
        p3_workload.first = false;
        p3_result.first = false;
        p4_workload.first = false;
        p4_result.first = false;
        p5_workload.first = false;
        p5_result.first = false;
        for(auto &r : iregs.reg) r.busy = false;
        for(auto &r : fregs.reg) r.busy = false;
    }

    bool log_info = false;
    bool log_regs = false;
    bool log_fregs = false;
    char log_buf[256];
    std::ofstream *log_file_commited_inst = nullptr;
    std::ofstream *log_file_ldst = nullptr;

    inline void dcache_prefetch(VirtAddrT vaddr) {
        
    }

    // inline PhysAddrT mem_trans_check_error(VirtAddrT vaddr, PageFlagT flg, VirtAddrT pc) {
    //     PhysAddrT paddr = 0;
    //     SimError res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, flg);
    //     if(res == SimError::invalidaddr) {
    //         sprintf(log_buf, "CPU%d Unknown Memory Access @0x%lx, V-Address: 0x%lx", cpu_id, pc, vaddr);
    //         LOG(ERROR) << log_buf;
    //         simroot_assert(0);
    //     }
    //     else if(res == SimError::unaccessable) {
    //         sprintf(log_buf, "CPU%d Unauthorized Memory Access @0x%lx, V-Address: 0x%lx", cpu_id, pc, vaddr);
    //         LOG(ERROR) << log_buf;
    //         simroot_assert(0);
    //     }
    //     return paddr;
    // }
    inline void cache_operation_result_check_error(SimError res, VirtAddrT pc, VirtAddrT vaddr, PhysAddrT paddr, uint32_t len) {
        if(res == SimError::invalidaddr) {
            sprintf(log_buf, "Invalid Memory Address @0x%lx: 0x%lx -> 0x%lx", pc, vaddr, paddr);
            LOG(ERROR) << string(log_buf);
            simroot_assert(0);
        }
        else if(res == SimError::unaligned) {
            sprintf(log_buf, "Unaligned Memory Address @0x%lx: 0x%lx / %d", pc, vaddr, len);
            LOG(ERROR) << string(log_buf);
            simroot_assert(0);
        }
    }

    inline void error_unkown_fp_format(RV64InstDecoded &inst) {
        sprintf(log_buf, "Unkown FP Format @0x%lx", inst.pc);
        LOG(ERROR) << string(log_buf);
        simroot_assert(0);
    }

// ---- Statistic ----
struct {
    uint64_t total_tick_processed = 0;
    uint64_t halt_tick_count = 0;

    uint64_t finished_inst_count = 0;

    uint64_t pipeline_1_stalled_fetch_count = 0;
    uint64_t pipeline_1_stalled_busy_count = 0;

    uint64_t pipeline_2_stalled_nop_count = 0;
    uint64_t pipeline_2_stalled_busy_count = 0;
    uint64_t pipeline_2_stalled_flush_count = 0;

    uint64_t pipeline_3_stalled_nop_count = 0;
    uint64_t pipeline_3_stalled_busy_count = 0;
    uint64_t pipeline_3_stalled_waitreg_count = 0;
    

    uint64_t pipeline_4_stalled_nop_count = 0;
    uint64_t pipeline_4_stalled_amo_count = 0;
    uint64_t pipeline_4_stalled_fetch_count = 0;
    uint64_t pipeline_4_stalled_busbusy_count = 0;
    uint64_t pipeline_4_stalled_busy_count = 0;

    uint64_t ld_cache_miss_count = 0;
    uint64_t ld_cache_hit_count = 0;
    uint64_t ld_inst_cnt = 0;
    uint64_t ld_mem_tick_sum = 0;

    uint64_t st_cache_miss_count = 0;
    uint64_t st_cache_hit_count = 0;
    uint64_t st_inst_cnt = 0;
    uint64_t st_mem_tick_sum = 0;

} statistic;

};

}

}

#endif

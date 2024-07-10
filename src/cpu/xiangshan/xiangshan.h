#ifndef RVSIM_CPU_XS_H
#define RVSIM_CPU_XS_H

#include "common.h"
#include "tickqueue.h"

#include "cpu/cpuinterface.h"
#include "cpu/isa.h"

#include "cache/cacheinterface.h"

#include "xstypes.h"
#include "xsbpu.h"
#include "xslsu.h"

namespace simcpu {

namespace xs {

using simcache::CacheInterface;

class XiangShanCPU : public CPUInterface {

public:

    XiangShanCPU(
        CacheInterface *icache_port, CacheInterfaceV2 *dcache_port, CPUSystemInterface *sys_port,
        uint32_t id
    );

    virtual void halt();
    virtual void redirect(VirtAddrT addr, RVRegArray &regs);

    virtual void on_current_tick();
    virtual void apply_next_tick();

    virtual void clear_statistic();
    virtual void print_statistic(std::ofstream &ofile);
    virtual void print_setup_info(std::ofstream &ofile);
    virtual void dump_core(std::ofstream &ofile);

protected:
    XiangShanParam param;

    uint32_t cpu_id;

    CacheInterface *io_icache_port;
    CacheInterfaceV2 *io_dcache_port;
    CPUSystemInterface *io_sys_port;

    XSInstID current_inst_id = 0;

    struct {
        bool is_halt = true;
        bool apply_halt = false;
        ValidData<VirtAddrT>        global_redirect_pc; // 整个CPU的PC重定向
        ValidData<RVRegArray>       global_redirect_reg; // 重定向时是否需要修改寄存器组的值
        ValidData<XSInst*>          sys_redirect; // 由于ECALL或EBREAK造成的CPU重定向
        ValidData<XSInst*>          jmp_redirect; // 由于分支或跳转指令造成的CPU重定向
        ValidData<XSInst*>          reorder_redirect_pc; // 由于访存顺序违例造成的整个CPU重定向
        ValidData<FTQEntry*>        bpu_redirect; // FTB给出的该fetchpack信息错误，用取出该pack后的状态重置BPU和ftq的后续内容
    } control;
    
    void apl_global_redirect();
    void apl_sys_redirect();
    void apl_jmp_redirect();
    void apl_reorder_redirect();
    void apl_bpu_redirect();
    void _apl_general_cpu_redirect(XSInst *inst, VirtAddrT nextpc = 0);

    /**
     * 保存了取指-译码阶段可能由于分支预测导致的异常，当ROB为空时由提交逻辑响应该异常
     * 这些异常会在全局重定向时清空
    */
    list<SimErrorData> ifu_errors, dec_errors;
    inline void insert_error(list<SimErrorData> &l, SimError err, uint64_t a0, uint64_t a1, uint64_t a2) {
        SimErrorData e;
        e.type = err;
        e.args[0] = a0;
        e.args[1] = a1;
        e.args[2] = a2;
        l.emplace_back(e);
    }

// ------------- IFU ---------------
// BPU ->ftq-> fetch --fetchres-> predec --pdecres-> instbuf

    unique_ptr<BPU> bpu;
    list<FTQEntry*> ftq;
    uint32_t ftq_pos = 0;

    unique_ptr<SimpleTickQueue<FTQEntry*>> fetch_result_queue;
    unique_ptr<SimpleTickQueue<FTQEntry*>> pdec_result_queue;
    unique_ptr<SimpleTickQueue<XSInst*>> inst_buffer;

    struct {
        uint8_t         rawbuf[CACHE_LINE_LEN_BYTE];
        list<uint16_t>  buf; // 里面保存了从icache中获取的最多2个cache-line
        VirtAddrT       pc = 0; // 流里第一条指令对应的pc
        uint32_t        bytecnt = 0;
        uint32_t        brcnt = 0;
    } fetchbuf;

    /**
     * 读取ftq的当前条目，尝试从icache里fetch出完整的一个fetch-package写到fetch result
    */
    void _cur_fetch_one_pack();

    /**
     * 在fetch的时候做过了，直接pass
    */
    void _cur_predecode() { fetch_result_queue->pass_to(*pdec_result_queue); }

    /**
     * 从pdec result读指令写到inst buf，检查取出的指令的基本信息是否与BPU里FTB给的信息一致，否则用取出该pack后的状态刷新整个前端
    */
    void _cur_pred_check();

    void ifu_on_current_tick();


// ------------- DEC/DISP -----------
//                                                           --dqint->
// instbuf-> decode --dec2rnm-> rename --rnm2disp-> dispatch --dqls->
//                                                           --dqfp->

    unique_ptr<SimpleTickQueue<XSInst*>> dec_to_rnm;
    unique_ptr<SimpleTickQueue<XSInst*>> rnm_to_disp;
    unique_ptr<SimpleTickQueue<XSInst*>> dq_int;
    unique_ptr<SimpleTickQueue<XSInst*>> dq_ls;
    unique_ptr<SimpleTickQueue<XSInst*>> dq_fp;

    struct {
        vector<PhysReg>     table;
        list<PhysReg>       freelist;
        unordered_map<XSInst*, vector<PhysReg>> checkpoint; // 为每个分支指令保存一份映射表，用于分支预测错误的恢复
        vector<PhysReg>     commited_table;
        // 将所有没有被映射到的物理寄存器都放到freelist中
        inline void reset_freelist(PhysReg maxidx) {
            vector<bool> used;
            used.assign(maxidx, false);
            for(auto r : table) used[r] = true;
            freelist.clear();
            for(PhysReg i = 0; i < maxidx; i++) if(!used[i]) freelist.push_back(i);
        }
        inline void save(XSInst* inst) {
            checkpoint.emplace(inst, table);
        }
        inline void checkout(XSInst* inst) {
            auto res = checkpoint.find(inst);
            assert(res != checkpoint.end());
            table.swap(res->second);
            checkpoint.clear();
            commited_table = table;
        }
        inline void init_table(VirtReg vregcnt) {
            table.resize(vregcnt);
            for(int i = 0; i < vregcnt; i++) {
                table[i] = i;
            }
            commited_table = table;
        }
    } irnm, frnm;
    struct {
        vector<RawDataT>    regfile;
        vector<bool>        busy;
        unordered_map<PhysReg, RawDataT> bypass; // 用于模拟旁路网络，在执行模块出结果后指令提交之前暂存寄存器的最新值，指令提交时将旁路的值写到rd寄存器
        set<PhysReg>        wakeup; // 本周期在旁路网络上新增的可读寄存器，用于加速保留站的更新效率
        unordered_map<PhysReg, RawDataT> apl_wb; // commit时设置，下一周期这些物理寄存器会被实际写回,同时旁路网络上的值会被移除
        unordered_map<PhysReg, RawDataT> apl_bypass; // 由exu模块完成前同时设置，这些寄存器值下一周期会被送到旁路网络
        inline void apl_clear(PhysReg maxidx) {
            busy.assign(maxidx, false);
            bypass.clear();
            wakeup.clear();
            for(auto &entry: apl_wb) {
                regfile[entry.first] = entry.second;
            }
            apl_wb.clear();
            apl_bypass.clear();
        }
        inline void apply_next_tick() {
            wakeup.clear();
            for(auto &entry : apl_bypass) {
                assert(wakeup.insert(entry.first).second);
                bypass[entry.first] = entry.second;
            }
            apl_bypass.clear();
            for(auto &entry: apl_wb) {
                bypass.erase(entry.first);
                regfile[entry.first] = entry.second;
                busy[entry.first] = false;
            }
            apl_wb.clear();
        }
        inline void init(PhysReg maxidx) {
            regfile.assign(maxidx, 0);
            busy.assign(maxidx, false);
        }
    } ireg, freg;

    list<XSInst*> rob;
    list<XSInst*> apl_rob_push;

    bool unique_inst_in_pipeline = false;

    /**
     * 从instbuf中读指令，解码后放到dec2rnm队列中
    */
    void _cur_decode();

    /**
     * 从dec2rnm队列中读指令，重命名后放到rnm2disp队列中
    */
    void _cur_rename();

    /**
     * 读rnm2disp队列，为指令分配rob，将指令分派到不同的派遣队列中
    */
    void _cur_dispatch();

    void decdisp_on_current_tick();
    
    /**
     * 从rob开头提交若干个已完成的指令
    */
    void cur_commit();

// ---------- EXU ----------
    
    typedef struct {
        bool *wb_ready;
        RawDataT *wb_value;
        XSInst *inst;
    } WaitOPRand;
    unique_ptr<TickMultiMap<PhysReg, WaitOPRand>> ireg_waits;
    unique_ptr<TickMultiMap<PhysReg, WaitOPRand>> freg_waits;
    /**
     * 根据旁路网络本周期新增的寄存器唤醒等待队列中的指令
    */
    void _cur_wakeup_exu_bypass();
    /**
     * 在指令进入保留站时读取寄存器文件与旁路网络，初始化操作数状态，将被占用的操作数寄存器登记到等待队列。
     * 区分派遣队列是因为load、store指令的一二操作数对应的arg是反的
    */
    void _do_reg_read(XSInst *inst, DispType disp);
    bool _do_try_get_reg(PhysReg rx, RVRegType type, RawDataT *buf);

    /**
     * EXU的实际计算阶段模拟的时候就直接等若干周期即可
    */
    typedef struct {
        XSInst      *inst = nullptr;
        uint32_t    latency = 0;
        uint32_t    processed = 0;
    } OperatingInst;
    class IntFPEXU {
    public:
        IntFPEXU(uint32_t rsport, uint32_t rscnt, uint32_t unitcnt, string name) : rs(rsport, rscnt), name(name) {
            opunit.resize(unitcnt);
        }
        // LimitedTickList<XSInst*>   rs;
        ReserveStation rs;
        vector<OperatingInst> opunit;
        string name;
        inline void apply_next_tick() {
            rs.apply_next_tick();
        }
        inline void clear() {
            rs.clear();
            for(auto &u:opunit) u.inst = nullptr;
        }
    };

    unique_ptr<IntFPEXU> mdu, alu1, alu2, misc, fmac1, fmac2, fmisc;
    list<XSInst*> apl_finished_inst;

    /**
     * 从int/fp dispatch queue中取出指令送到对应的保留站中
    */
    void cur_int_disp2();
    void cur_fp_disp2();

    /**
     * 选择操作数就绪的指令发射到执行单元，写回执行单元中已完成的指令
    */
    void _cur_intfp_exu(IntFPEXU *exu);

    unique_ptr<LimitedTickList<XSInst*>> rs_ld;
    unique_ptr<LimitedTickList<XSInst*>> rs_sta;
    unique_ptr<LimitedTickList<XSInst*>> rs_std;
    unique_ptr<LimitedTickList<XSInst*>> rs_amo;
    unique_ptr<LimitedTickList<XSInst*>> rs_fence;
    LSUPort lsu_port;
    unique_ptr<LSU> lsu;
    void cur_mem_disp2();

    void cur_exu_lsu() {
        _cur_wakeup_exu_bypass();
        _cur_intfp_exu(mdu.get());
        _cur_intfp_exu(alu1.get());
        _cur_intfp_exu(alu2.get());
        _cur_intfp_exu(misc.get());
        _cur_intfp_exu(fmac1.get());
        _cur_intfp_exu(fmac2.get());
        _cur_intfp_exu(fmisc.get());
        lsu->on_current_tick();
    }

// --------- 杂项 ---------

    void _apl_clear_pipeline(); // 不包含BPU
    void _apl_forward_pipeline(); // 不包含LSU与Cache的每周期必须调用的部分
    void _cur_forward_pipeline(); // 不包含LSU与Cache的每周期必须调用的部分

    struct {
        RawDataT    fcsr;
    } csrs;
    void _do_system_inst(XSInst *inst);
    void _do_csr_inst(XSInst *inst);
    void _do_op_inst(XSInst *inst);

    uint32_t _get_exu_latency(XSInst *inst);


// ---------- LOG -----------
    uint64_t dead_loop_warn_tick = -1;
    uint64_t dead_loop_detact_tick = 0;

    bool debug_bpu = false;
    bool debug_lsu = false;
    bool debug_pipeline = false;
    bool log_inst_to_file = false;
    bool log_inst_to_stdout = false;
    bool log_regs = false;
    bool log_fregs = false;
    simroot::LogFileT debug_bpu_ofile = nullptr;
    simroot::LogFileT debug_lsu_ofile = nullptr;
    simroot::LogFileT debug_pipeline_ofile = nullptr;
    simroot::LogFileT log_inst_ofile = nullptr;
    char log_buf[512];

    struct {
        uint64_t total_tick_cnt = 0;
        uint64_t active_tick_cnt = 0;

        uint64_t finished_inst_cnt = 0;
        uint64_t ld_inst_cnt = 0;
        uint64_t st_inst_cnt = 0;
        uint64_t amo_inst_cnt = 0;
        uint64_t sys_inst_cnt = 0;
        uint64_t mem_inst_cnt = 0;

        uint64_t br_inst_cnt = 0;
        uint64_t br_pred_hit_cnt = 0;
        uint64_t jalr_inst_cnt = 0;
        uint64_t jalr_pred_hit_cnt = 0;
        uint64_t call_inst_cnt = 0;
        uint64_t ret_inst_cnt = 0;
        uint64_t ret_pred_hit_cnt = 0;

        uint64_t fetch_pack_cnt = 0;
        uint64_t fetch_pack_hit_cnt = 0;
    } statistic;
};


}}

#endif

#ifndef RVSIM_CPU_XS_LSU_H
#define RVSIM_CPU_XS_LSU_H

#include "common.h"
#include "tickqueue.h"

#include "cpu/cpuinterface.h"
#include "cpu/isa.h"

#include "cache/cacheinterface.h"

#include "xstypes.h"

namespace simcpu {

namespace xs {

using simcache::CacheInterface;

/**
 * LSU不负责更新RS，只负责每周期从RS中取出已就绪的指令并发射
 * RS中指令的就绪状态与操作数获取由外部的流水线控制逻辑处理
*/
typedef struct {
    TickList<XSInst*> *ld;
    TickList<XSInst*> *sta;
    TickList<XSInst*> *std;
    TickList<XSInst*> *amo; // 只有在amo处于commit头部时才会实际执行以保证amo正确，当前amo完成前不能接收另一个amo
    TickList<XSInst*> *fence;
    unordered_map<PhysReg, RawDataT> *apl_int_bypass;
    unordered_map<PhysReg, RawDataT> *apl_fp_bypass;
} LSUPort;

typedef struct {
    uint8_t linebuf[CACHE_LINE_LEN_BYTE];
    bool    valid[CACHE_LINE_LEN_BYTE];
} StoreBufEntry;

enum class AMOState {
    free = 0,               // AMO状态机空闲
    tlb,                    // 原子指令发出 TLB 查询请求
    pm,                     // 完成 TLB 查询和地址例外检查
    flush_sbuffer_req,      // 发出 sbuffer/sq flush 请求
    flush_sbuffer_resp,     // 等待 sbuffer/sq flush 请求完成
    cache_req,              // 向 dcache 发起原子指令操作
    cache_resp,             // 等待 dcache 返回原子指令结果
    finish,                 // 结果写回
};

class LSU {

public:

    LSU(XiangShanParam *param, uint32_t cpu_id, CPUSystemInterface *io_sys, CacheInterface *io_dcache, LSUPort *port);

    /**
     * 该函数必须每周期被调用一次，即使CPU当前为halt状态。
     * 用于将commited_store_buf写回到dcache
    */
    void always_on_current_tick() { _cur_write_commited_store(); };
    /**
     * 该函数必须每周期被调用一次，即使CPU当前为halt状态。
     * 用于将commited_store_buf写回到dcache
    */
    void always_apply_next_tick();

    void on_current_tick();
    void apply_next_tick();

    void cur_commit_store(XSInst *inst) { _cur_commit_store(inst); };
    void cur_commit_load(XSInst *inst) { _cur_commit_load(inst); };

    /**
     * 尝试提交AMO指令，表示该AMO指令位于ROB头部，如果AMO指令还没启动则启动AMO状态机，否则返回AMO指令是否完成
    */
    bool cur_commit_amo(XSInst *inst);

    void apl_clear_pipeline(); // 里面所有的XSInst*都在ROB里有表项，这里不需要释放

    simroot::LogFileT debug_ofile = nullptr;
    char log_buf[256];

protected:

    XiangShanParam *param;
    uint32_t cpu_id;
    CPUSystemInterface *io_sys_port;
    CacheInterface *io_dcache_port;
    LSUPort *port;

    AMOState amo_state = AMOState::free;

    unique_ptr<SimpleTickQueue<XSInst*>> ld_addr_trans_queue;
    unique_ptr<SimpleTickQueue<XSInst*>> st_addr_trans_queue;

    list<XSInst*> success_load_queue; // 已经访存成功等待写回的
    unordered_map<LineIndexT, list<XSInst*>> load_queue_waiting_line; // 已经访存过一次但miss的
    list<XSInst*> load_queue_arrival; // 刚到还没有访存过的
    list<XSInst*> apl_push_load_queue; // 刚翻译完地址下一周期准备要进load_queue_arrival的
    set<XSInst*> finished_load;
    uint32_t load_queue_total_cnt = 0;

    set<XSInst*> store_queue;
    list<XSInst*> apl_push_store_queue;

    list<XSInst*> inst_finished;
    list<XSInst*> apl_inst_finished;

    unordered_map<LineIndexT, StoreBufEntry> commited_store_buf;
    list<LineIndexT> commited_store_buf_lru;
    unordered_map<LineIndexT, StoreBufEntry> apl_commited_store_buf;

    /**
     * 从LD保留站中获取一个就绪的指令发射到ld_addr_trans_queue
    */
    void _cur_get_ld_from_rs();

    /**
     * 从ld_addr_trans_queue取出一个指令并进行地址翻译，翻译成功则进入apl_push_load_queue，否则设置错误后从output输出。
     * 该阶段完成load-load违例检查，检查finished_load中是否存在比自己晚的同地址的指令，存在则将查到的指令标为llreorder错误。
    */
    void _cur_ld_addr_trans();

    /**
     * 获取cache当前周期新到达的cacheline，检查load_queue中是否有可完成的
    */
    void _cur_load_queue();

    SimError _do_load(XSInst *inst);

    /**
     * 从STA保留站中获取一个指令操作数就绪的指令发射到st_addr_trans_queue
    */
    void _cur_get_sta_from_rs();

    /**
     * 从STD保留站中获取一个双操作数均就绪的指令发射到apl_push_store_queue
     * 该阶段完成store-load违例检查，检查finished_load中是否存在比自己晚的同地址的指令，存在则将查到的指令标为slreorder错误。
    */
    void _cur_get_std_from_rs();

    /**
     * 从st_addr_trans_queue取出一个指令并进行地址翻译，翻译失败则设置错误后从output输出同时删除STD保留站对应项。
     * 翻译成功则直接丢弃等STD发射。
    */
    void _cur_st_addr_trans();

    SimError _do_amo(XSInst *inst);

// ---------------------------------------------------

    /**
     * 从finished_load中删除对应项
    */
    void _cur_commit_load(XSInst *inst);

    /**
     * 从store_queue中找出对应项，将store内容加入commited_store_buf
    */
    void _cur_commit_store(XSInst *inst);

    /**
     * 从committed store buffer中选择最旧的一行写回到dcache
    */
    void _cur_write_commited_store();

};



}}

#endif


#include "xiangshan.h"

#include "cpu/operation.h"

#include "simroot.h"
#include "configuration.h"

namespace simcpu {

namespace xs {

XiangShanCPU::XiangShanCPU(
    CacheInterface *icache_port, CacheInterfaceV2 *dcache_port, CPUSystemInterface *sys_port,
    uint32_t id
) : cpu_id(id), io_icache_port(icache_port), io_dcache_port(dcache_port), io_sys_port(sys_port)
{
    control.is_halt = true;

    #define XSREADCONF(name) param.name = conf::get_int("xiangshan", #name, param.name)
    XSREADCONF(ftb_ways);
    XSREADCONF(ubtb_ways);
    XSREADCONF(ftb_set_bits);
    XSREADCONF(fetch_width_bytes);
    XSREADCONF(ftq_size);
    XSREADCONF(inst_buffer_size);
    XSREADCONF(decode_width);
    XSREADCONF(rob_size);
    XSREADCONF(branch_inst_cnt);
    XSREADCONF(phys_ireg_cnt);
    XSREADCONF(phys_freg_cnt);
    XSREADCONF(int_rs_size);
    XSREADCONF(fp_rs_size);
    XSREADCONF(mem_rs_size);
    XSREADCONF(load_queue_size);
    XSREADCONF(store_queue_size);
    XSREADCONF(commited_store_buffer_size);
    #undef XSREADCONF

    dead_loop_warn_tick = conf::get_int("xiangshandebug", "dead_loop_warn_tick", -1);

    if(conf::get_int("xiangshandebug", "debug_all", 0)) {
        debug_bpu = debug_lsu = debug_pipeline = true;
    }
    else {
        debug_bpu = conf::get_int("xiangshandebug", "debug_bpu", 0);
        debug_lsu = conf::get_int("xiangshandebug", "debug_lsu", 0);
        debug_pipeline = conf::get_int("xiangshandebug", "debug_pipeline", 0);
    }
    log_inst_to_file = conf::get_int("xiangshandebug", "log_inst_to_file", 0);
    log_inst_to_stdout = conf::get_int("xiangshandebug", "log_inst_to_stdout", 0);
    log_regs = conf::get_int("xiangshandebug", "log_regs", 0);
    log_fregs = conf::get_int("xiangshandebug", "log_fregs", 0);

    int32_t log_linecnt = conf::get_int("xiangshandebug", "debug_file_line", 0);
    if(debug_bpu || debug_lsu || debug_pipeline || log_inst_to_file) {
        std::filesystem::path logdir(conf::get_str("xiangshandebug", "logdir", "xiangshandebug"));
        std::filesystem::create_directory(logdir);
        #define OPENLOGFILE(cond, name, ofsptr) if(cond) { \
            sprintf(log_buf, #name "_%d.log", cpu_id); \
            std::filesystem::path _fpath = logdir / std::filesystem::path(string(log_buf)); \
            ofsptr = simroot::create_log_file(_fpath.string(), log_linecnt); \
            };
        OPENLOGFILE(debug_bpu, bpu, debug_bpu_ofile);
        OPENLOGFILE(debug_lsu, lsu, debug_lsu_ofile);
        OPENLOGFILE(debug_pipeline, pipeline, debug_pipeline_ofile);
        OPENLOGFILE(log_inst_to_file, inst, log_inst_ofile);
        #undef OPENLOGFILE
    }

    memset(&statistic, 0, sizeof(statistic));

    uint32_t cpu_width = param.decode_width;

    fetch_result_queue = make_unique<SimpleTickQueue<FTQEntry*>>(1, 1, 0);
    pdec_result_queue = make_unique<SimpleTickQueue<FTQEntry*>>(1, 1, 0);
    inst_buffer = make_unique<SimpleTickQueue<XSInst*>>(1 + param.fetch_width_bytes / 2, cpu_width, param.inst_buffer_size);
    
    bpu = make_unique<BPU>(&param, &ftq, 0);
    bpu->debug_ofile = debug_bpu_ofile;

    dec_to_rnm = make_unique<SimpleTickQueue<XSInst*>>(cpu_width, cpu_width, 0);
    rnm_to_disp = make_unique<SimpleTickQueue<XSInst*>>(cpu_width, cpu_width, 0);
    dq_int = make_unique<SimpleTickQueue<XSInst*>>(cpu_width, cpu_width, 0);
    dq_ls = make_unique<SimpleTickQueue<XSInst*>>(cpu_width, cpu_width, 0);
    dq_fp = make_unique<SimpleTickQueue<XSInst*>>(cpu_width, cpu_width, 0);

    irnm.init_table(RV_REG_CNT_INT);
    frnm.init_table(RV_REG_CNT_FP);
    irnm.reset_freelist(param.phys_ireg_cnt);
    frnm.reset_freelist(param.phys_freg_cnt);
    ireg.init(param.phys_ireg_cnt);
    freg.init(param.phys_freg_cnt);

    ireg_waits = make_unique<TickMultiMap<PhysReg, WaitOPRand>>();
    freg_waits = make_unique<TickMultiMap<PhysReg, WaitOPRand>>();

    mdu = make_unique<IntFPEXU>(2, param.int_rs_size * 2, 2, "MDU");
    alu1 = make_unique<IntFPEXU>(2, param.int_rs_size * 2, 2, "ALU1");
    alu2 = make_unique<IntFPEXU>(2, param.int_rs_size * 2, 2, "ALU2");
    misc = make_unique<IntFPEXU>(1, param.int_rs_size * 1, 1, "MISC");

    fmac1 = make_unique<IntFPEXU>(2, param.fp_rs_size * 2, 2, "FMAC1");
    fmac2 = make_unique<IntFPEXU>(2, param.fp_rs_size * 2, 2, "FMAC2");
    fmisc = make_unique<IntFPEXU>(2, param.fp_rs_size * 2, 2, "FMISC");

    rs_ld = make_unique<LimitedTickList<XSInst*>>(2, param.mem_rs_size * 2);
    rs_sta = make_unique<LimitedTickList<XSInst*>>(2, param.mem_rs_size * 2);
    rs_std = make_unique<LimitedTickList<XSInst*>>(2, param.mem_rs_size * 2);
    rs_amo = make_unique<LimitedTickList<XSInst*>>(1, 1);
    rs_fence = make_unique<LimitedTickList<XSInst*>>(1, 1);

    lsu_port.ld = rs_ld.get();
    lsu_port.sta = rs_sta.get();
    lsu_port.std = rs_std.get();
    lsu_port.amo = rs_amo.get();
    lsu_port.fence = rs_fence.get();
    lsu_port.apl_int_bypass = &ireg.apl_bypass;
    lsu_port.apl_fp_bypass = &freg.apl_bypass;
    lsu = make_unique<LSU>(&param, cpu_id, io_sys_port, io_dcache_port, &lsu_port);
    lsu->debug_ofile = debug_lsu_ofile;


}

void XiangShanCPU::halt() {
    control.apply_halt = true;
}

void XiangShanCPU::redirect(VirtAddrT addr, RVRegArray &regs) {
    control.global_redirect_pc.valid = true;
    control.global_redirect_pc.data = addr;
    control.global_redirect_reg.valid = true;
    control.global_redirect_reg.data = regs;
}

void XiangShanCPU::on_current_tick() {
    io_icache_port->on_current_tick();
    io_dcache_port->on_current_tick();
    lsu->always_on_current_tick();

    statistic.total_tick_cnt++;

    if(!control.is_halt) {
        _cur_forward_pipeline();
        statistic.active_tick_cnt++;
    }
}

void XiangShanCPU::apply_next_tick() {
    io_icache_port->apply_next_tick();
    io_dcache_port->apply_next_tick();
    lsu->always_apply_next_tick();

    if(control.apply_halt) [[unlikely]] {
        control.is_halt = true;
        control.apply_halt = false;
    }
    apl_global_redirect();
    if(control.is_halt) return;

    apl_sys_redirect();
    apl_jmp_redirect();
    apl_reorder_redirect();
    apl_bpu_redirect();

    _apl_forward_pipeline();
}

void XiangShanCPU::clear_statistic() {
    memset(&statistic, 0, sizeof(statistic));
}

#define LOGTOFILE(fmt, ...) do{sprintf(log_buf, fmt, ##__VA_ARGS__);ofile << log_buf;}while(0)

void XiangShanCPU::print_statistic(std::ofstream &ofile) {
    #define STATU64(name) do{sprintf(log_buf, #name ": \t\t%ld\n", statistic.name);ofile << log_buf;}while(0)
    LOGTOFILE("XSCPU:%d\n", cpu_id);
    STATU64(total_tick_cnt);
    STATU64(active_tick_cnt);
    LOGTOFILE("\n");
    STATU64(finished_inst_cnt);
    STATU64(ld_inst_cnt);
    STATU64(st_inst_cnt);
    STATU64(amo_inst_cnt);
    STATU64(sys_inst_cnt);
    STATU64(mem_inst_cnt);
    LOGTOFILE("\n");
    STATU64(br_inst_cnt);
    STATU64(br_pred_hit_cnt);
    STATU64(jalr_inst_cnt);
    STATU64(jalr_pred_hit_cnt);
    STATU64(ret_inst_cnt);
    STATU64(ret_pred_hit_cnt);
    LOGTOFILE("\n");
    STATU64(fetch_pack_cnt);
    STATU64(fetch_pack_hit_cnt);
    #undef STATU64
}

void XiangShanCPU::print_setup_info(std::ofstream &ofile) {

}

void XiangShanCPU::dump_core(std::ofstream &ofile) {
    LOGTOFILE("XSCPU:%d\n", cpu_id);
    LOGTOFILE("\n");
    {
        LOGTOFILE("#FTQ: %ld items, pos ptr %d:\n", ftq.size(), ftq_pos);
        int i = 0;
        for(auto &p : ftq) {
            LOGTOFILE("%d:0x%lx-0x%lx %ldinsts %ldbr %djmp 0x%lx | ", i, p->startpc, p->endpc, p->insts.size(), p->branchs.size(), (int)(p->jmpinfo), p->jmptarget);
            i++;
            if(i % 4 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 4 == 0) LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    {
        vector<XSInst*> tmp;
        inst_buffer->dbg_get_all(&tmp);
        LOGTOFILE("#INST_BUF: %ld items:\n", tmp.size());
        int i = 0;
        for(auto &p : tmp) {
            if(isa::isRVC(p->inst)) LOGTOFILE("%d:0x%lx:0x%04x | ", i, p->pc, p->inst);
            else LOGTOFILE("%d:0x%lx:0x%08x | ", i, p->pc, p->inst);
            i++;
            if(i % 4 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 4 == 0) LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#ROB: %ld items:\n", rob.size());
        int i = 0;
        for(auto &p : rob) {
            if(isa::isRVC(p->inst)) {
                LOGTOFILE("%d:0x%lx:0x%04x %s (%d,%d,%d->%d) | ", i, p->pc, p->inst, p->dbgname.c_str(), p->prs[0], p->prs[1], p->prs[2], p->prd);
            }
            else {
                LOGTOFILE("%d:0x%lx:0x%08x %s (%d,%d,%d->%d) | ", i, p->pc, p->inst, p->dbgname.c_str(), p->prs[0], p->prs[1], p->prs[2], p->prd);
            }
            i++;
            if(i % 2 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 2 == 0) LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#V-REGS:\n");
        for(int i = 0; i < 32; i+=2) {
            PhysReg pr1 = irnm.commited_table[i], pr2 = irnm.commited_table[i+1];
            PhysReg pr3 = frnm.commited_table[i], pr4 = frnm.commited_table[i+1];
            LOGTOFILE("%s(%02d)-%d: 0x%016lx, %s(%02d)-%d: 0x%016lx,    %s(%02d)-%d: 0x%016lx, %s(%02d)-%d: 0x%016lx,\n",
                isa::ireg_names()[i], pr1, ireg.busy[pr1]?1:0, ireg.regfile[pr1],
                isa::ireg_names()[i+1], pr2, ireg.busy[pr2]?1:0, ireg.regfile[pr2],
                isa::freg_names()[i], pr3, freg.busy[pr3]?1:0, freg.regfile[pr3],
                isa::freg_names()[i+1], pr4, freg.busy[pr4]?1:0, freg.regfile[pr4]
            );
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#IREGS:\n");
        for(int i = 0; i < param.phys_ireg_cnt; i+=4) {
            LOGTOFILE("%02d-%d: 0x%016lx, %02d-%d: 0x%016lx, %02d-%d: 0x%016lx, %02d-%d: 0x%016lx,\n",
                i, ireg.busy[i]?1:0, ireg.regfile[i], 
                i+1, ireg.busy[i+1]?1:0, ireg.regfile[i+1], 
                i+2, ireg.busy[i+2]?1:0, ireg.regfile[i+2], 
                i+3, ireg.busy[i+3]?1:0, ireg.regfile[i+3]
            );
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#IBYPASS:\n");
        int i = 0;
        for(auto &e : ireg.bypass) {
            LOGTOFILE("%d:0x%016lx | ", e.first, e.second);
            i++;
            if(i % 4 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 4 == 0) LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#FREGS:\n");
        for(int i = 0; i < param.phys_freg_cnt; i+=4) {
            LOGTOFILE("%02d-%d: 0x%016lx, %02d-%d: 0x%016lx, %02d-%d: 0x%016lx, %02d-%d: 0x%016lx,\n",
                i, freg.busy[i]?1:0, freg.regfile[i], 
                i+1, freg.busy[i+1]?1:0, freg.regfile[i+1], 
                i+2, freg.busy[i+2]?1:0, freg.regfile[i+2], 
                i+3, freg.busy[i+3]?1:0, freg.regfile[i+3]
            );
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#FBYPASS:\n");
        int i = 0;
        for(auto &e : freg.bypass) {
            LOGTOFILE("%d:0x%016lx | ", e.first, e.second);
            i++;
            if(i % 4 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 4 == 0) LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    auto log_exu = [&](IntFPEXU *exu, string name) -> void {
        LOGTOFILE("#%s-RS:\n", name.c_str());
        int i = 0;
        for(auto &p : exu->rs.get()) {
            if(isa::isRVC(p->inst)) {
                LOGTOFILE("%d:0x%lx:0x%04x %s (%d,%d,%d) | ", i, p->pc, p->inst, p->dbgname.c_str(), p->rsready[0], p->rsready[1], p->rsready[2]);
            }
            else {
                LOGTOFILE("%d:0x%lx:0x%08x %s (%d,%d,%d) | ", i, p->pc, p->inst, p->dbgname.c_str(), p->rsready[0], p->rsready[1], p->rsready[2]);
            }
            i++;
            if(i % 2 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 2 == 0) LOGTOFILE("\n");
        LOGTOFILE("#%s-EX:\n", name.c_str());
        for(int j = 0; j < exu->opunit.size(); j++) {
            auto p = exu->opunit[j].inst;
            if(p == nullptr) LOGTOFILE("%d:Free\n", j);
            LOGTOFILE("%d:0x%lx:0x%04x %s, processing (%d/%d)\n", j, p->pc, p->inst, p->dbgname.c_str(), exu->opunit[j].processed, exu->opunit[j].latency);
        }
    };
    log_exu(alu1.get(), "ALU1");
    LOGTOFILE("\n");
    log_exu(alu2.get(), "ALU2");
    LOGTOFILE("\n");
    log_exu(mdu.get(), "MDU");
    LOGTOFILE("\n");
    log_exu(misc.get(), "MISC");
    LOGTOFILE("\n");
    log_exu(fmac1.get(), "FMAC1");
    LOGTOFILE("\n");
    log_exu(fmac2.get(), "FMAC2");
    LOGTOFILE("\n");
    log_exu(fmisc.get(), "FMISC");
    LOGTOFILE("\n");

    auto log_mrs = [&](LimitedTickList<XSInst*> *rs, string name) -> void {
        LOGTOFILE("#%s-RS:\n", name.c_str());
        int i = 0;
        for(auto &p : rs->get()) {
            if(isa::isRVC(p->inst)) {
                LOGTOFILE("%d:0x%lx:0x%04x %s (%d,%d,%d) | ", i, p->pc, p->inst, p->dbgname.c_str(), p->rsready[0], p->rsready[1], p->rsready[2]);
            }
            else {
                LOGTOFILE("%d:0x%lx:0x%08x %s (%d,%d,%d) | ", i, p->pc, p->inst, p->dbgname.c_str(), p->rsready[0], p->rsready[1], p->rsready[2]);
            }
            i++;
            if(i % 2 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 2 == 0) LOGTOFILE("\n");
    };
    log_mrs(rs_ld.get(), "LD");
    LOGTOFILE("\n");
    log_mrs(rs_sta.get(), "STA");
    LOGTOFILE("\n");
    log_mrs(rs_std.get(), "STD");
    LOGTOFILE("\n");
    log_mrs(rs_amo.get(), "AMO");
    LOGTOFILE("\n");
    log_mrs(rs_fence.get(), "FENCE");
    LOGTOFILE("\n");

    lsu->dump_core(ofile);
    io_dcache_port->dump_core(ofile);

    bpu->dump_core(ofile);

    io_icache_port->dump_core(ofile);
}

#undef LOGTOFILE




/**
 * 读取ftq的当前条目，尝试从icache里fetch出完整的一个fetch-package写到fetch result
*/
void XiangShanCPU::_cur_fetch_one_pack() {
    if(ftq_pos >= ftq.size()) return;
    if(fetch_result_queue->can_push() == 0) return;

    auto ftq_iter = ftq.begin();
    std::advance(ftq_iter, ftq_pos);
    FTQEntry * fetch = *ftq_iter;

    bool fetch_finished = false;
    if(fetchbuf.pc != fetch->startpc) {
        fetch->insts.clear();
        fetchbuf.buf.clear();
        fetchbuf.pc = fetch->startpc;
        fetchbuf.bytecnt = fetchbuf.brcnt = 0;
    }

    if(fetchbuf.pc & 1) [[unlikely]] {
        insert_error(ifu_errors, SimError::invalidpc, fetchbuf.pc, 0, 0);
        return;
    }

    io_icache_port->arrival_line(nullptr); // 用不到，清空icache的到达记录

    bool finished = true;
    while(fetchbuf.bytecnt < param.fetch_width_bytes) {
        bool need_fetch = (fetchbuf.buf.empty() || (fetchbuf.buf.size() == 1 && !isa::isRVC(fetchbuf.buf.front())));
        bool has_err = false;
        SimError res = SimError::success;
        if(fetchbuf.buf.empty() || (fetchbuf.buf.size() == 1 && !isa::isRVC(fetchbuf.buf.front()))) {
            uint64_t fetch_start = fetchbuf.pc + (fetchbuf.buf.size() * 2);
            uint64_t fetch_end = ALIGN(fetch_start + 1, CACHE_LINE_LEN_BYTE);
            uint64_t fetch_length = fetch_end - fetch_start;
            simroot_assert(fetch_length <= CACHE_LINE_LEN_BYTE);
            simroot_assert((fetch_length & 1) == 0);
            // 从fetch start开始的一行
            if(io_sys_port->is_dev_mem(cpu_id, fetch_start)) {
                simroot_assert(io_sys_port->dev_input(cpu_id, fetch_start, fetch_length, fetchbuf.rawbuf));
            }
            else {
                PhysAddrT paddr = 0;
                res = io_sys_port->v_to_p(cpu_id, fetch_start, &paddr, PGFLAG_X);
                if(res == SimError::success) [[likely]] {
                    res = io_icache_port->load(paddr, fetch_length, fetchbuf.rawbuf, false);
                }
            }
            if(res == SimError::invalidaddr) [[unlikely]] res = SimError::invalidpc;
            else if(res == SimError::unaccessable) [[unlikely]] res = SimError::unexecutable;
            if(res == SimError::invalidpc || res == SimError::unexecutable || res == SimError::unaligned) [[unlikely]] {
                insert_error(ifu_errors, res, fetchbuf.pc, fetch_length, 0);
                has_err = true;
            }
            else if(res == SimError::success) {
                // need_fetch = false;
                for(uint64_t i = 0; i < fetch_length; i+=2) {
                    fetchbuf.buf.push_back(*((uint16_t*)(fetchbuf.rawbuf + i)));
                }
            }
        }
        if(fetchbuf.buf.empty() || (fetchbuf.buf.size() == 1 && !isa::isRVC(fetchbuf.buf.front()))) {
            finished = has_err; // 如果出错就只保留取到的指令，否则等下周期再来看icache是否有响应
            break;
        }
        RVInstT inst = fetchbuf.buf.front();
        if(!isa::isRVC(inst)) {
            auto iter = fetchbuf.buf.begin();
            std::advance(iter, 1);
            inst |= (((uint32_t)(*iter)) << 16);
        }
        if(isa::pdec_isB(inst)) {
            if(fetchbuf.brcnt == XSIFU_BRANCH_CNT) {
                // 不要这个指令
                break;
            }
            fetchbuf.brcnt ++;
        }
        fetch->insts.push_back(inst);
        fetchbuf.buf.pop_front();
        fetchbuf.pc += 2;
        fetchbuf.bytecnt += 2;
        if(!isa::isRVC(inst)) {
            fetchbuf.buf.pop_front();
            fetchbuf.pc += 2;
            fetchbuf.bytecnt += 2;
        }
        if(isa::pdec_isJ(inst) || fetchbuf.bytecnt >= param.fetch_width_bytes) [[unlikely]] {
            break;
        }
    }

    if(finished) {
        if(!fetch->insts.empty()) [[likely]] {
            if(debug_pipeline_ofile) {
                sprintf(log_buf, "%ld:IFU: Fetch(%d/%ld) @0x%lx, len %d, ftb len %ld, err %d:",
                    simroot::get_current_tick(), ftq_pos + 1, ftq.size(),
                    fetch->startpc, fetchbuf.bytecnt, fetch->endpc - fetch->startpc, !ifu_errors.empty()
                );
                string str(log_buf);
                for(auto raw : fetch->insts) {
                    if(isa::isRVC(raw)) sprintf(log_buf, " %04x", raw & 0xffff);
                    else sprintf(log_buf, " %08x", raw);
                    str += log_buf;
                }
                simroot::log_line(debug_pipeline_ofile, str);
            }
            fetch_result_queue->push(fetch);
            ftq_pos++;
        }
        else {
            ftq.erase(ftq_iter);
            if(debug_pipeline_ofile) {
                sprintf(log_buf, "%ld:IFU: Fetch EMPTY @0x%lx", simroot::get_current_tick(), fetch->startpc);
                simroot::log_line(debug_pipeline_ofile, log_buf);
            }
        }
        fetchbuf.bytecnt = fetchbuf.brcnt = 0;
    }
}

/**
 * 从fetch result读指令写到inst buf，检查取出的指令的基本信息是否与BPU里FTB给的信息一致，否则用取出该pack后的状态刷新整个前端
*/
void XiangShanCPU::_cur_pred_check() {
    if(pdec_result_queue->can_pop() == 0) return;
    FTQEntry *fetch = pdec_result_queue->top();
    if(inst_buffer->can_push() < fetch->insts.size()) return;

    auto &insts = fetch->insts;
    vector<XSInst*> pdec_insts;
    pdec_insts.assign(insts.size(), nullptr);

    // 用于跟ftb给的信息进行对比
    vector<int32_t> brjmpoffset;
    vector<uint16_t> brinstoffset;
    FetchFlagT jmpinfo = 0;
    VirtAddrT jmptarget;
    uint16_t len = 0;

    for(int i = 0; i < insts.size(); i++) {
        XSInst * xsinst = new XSInst();
        pdec_insts[i] = xsinst;

        xsinst->ftq = fetch;
        xsinst->pc = fetch->startpc + len;
        xsinst->inst = insts[i];

        if(isa::pdec_isB(insts[i])) {
            int32_t target = 0;
            simroot_assert(isa::pdec_get_B_target(insts[i], &target));
            brinstoffset.push_back(len);
            brjmpoffset.push_back(target);
        }
        else if(isa::pdec_isJI(insts[i])) {
            simroot_assert(i + 1 == insts.size());
            jmpinfo |= FETCH_FLAG_JAL;
            int32_t target = 0;
            simroot_assert(isa::pdec_get_J_target(insts[i], &target));
            jmptarget = fetch->startpc + len + (int64_t)target;
            if(isa::pdec_isCALLI(insts[i])) {
                jmpinfo |= FETCH_FLAG_CALL;
            }
        }
        else if(isa::pdec_isJR(insts[i])) {
            simroot_assert(i + 1 == insts.size());
            jmpinfo |= FETCH_FLAG_JALR;
            if(isa::pdec_isCALLR(insts[i])) {
                jmpinfo |= FETCH_FLAG_CALL;
            }
            else if(isa::pdec_isRET(insts[i])) {
                jmpinfo |= FETCH_FLAG_RET;
            }
        }
        len += (isa::isRVC(insts[i])?2:4);
    }

    simroot_assert(brjmpoffset.size() <= XSIFU_BRANCH_CNT);

    bool lencheck = (fetch->startpc + len == fetch->endpc);
    bool jmpcheck = (jmpinfo == fetch->jmpinfo && ((jmpinfo & FETCH_FLAG_JAL) == 0 || jmptarget == fetch->jmptarget));
    bool brcheck = (brjmpoffset.size() == fetch->branchs.size());
    if(brcheck) for(int i = 0; i < brjmpoffset.size(); i++) {
        if(brjmpoffset[i] != fetch->branchs[i].jmp_offset || brinstoffset[i] != fetch->branchs[i].inst_offset) {
            brcheck = false;
            break;
        }
    }
    if(!lencheck || !jmpcheck || !brcheck) {
        fetch->endpc = fetch->startpc + len;
        fetch->jmpinfo = jmpinfo;
        if(jmpinfo & FETCH_FLAG_JAL) fetch->jmptarget = jmptarget;
        vector<FTQBranch> newbr;
        newbr.resize(brjmpoffset.size());
        for(int i = 0; i < brjmpoffset.size(); i++) {
            newbr[i].jmp_offset = brjmpoffset[i];
            newbr[i].inst_offset = brinstoffset[i];
            newbr[i].pred_taken = ((i < fetch->branchs.size())?(fetch->branchs[i].pred_taken):1); // always taken
        }
        fetch->branchs.swap(newbr);
        // FTB给出的该fetchpack信息错误，用取出该pack后的状态重置BPU和ftq的后续内容
        control.bpu_redirect.valid = true;
        control.bpu_redirect.data = fetch;

        bpu->cur_update_fetch_result(fetch);
    }
    else {
        statistic.fetch_pack_hit_cnt++;
    }
    statistic.fetch_pack_cnt++;

    if(debug_pipeline_ofile) {
        sprintf(log_buf, "%ld:PDEC: @0x%lx, %ld insts, check:%d", simroot::get_current_tick(), fetch->startpc, fetch->insts.size(), (lencheck && jmpcheck && brcheck)?1:0);
        simroot::log_line(debug_pipeline_ofile, log_buf);
    }
    
    uint32_t bridx = 0;
    uint32_t instcnt = 0;
    bool brout = false;
    for(auto p : pdec_insts) {
        if(brout) {
            delete p;
            continue;
        }
        simroot_assert(inst_buffer->push(p));
        instcnt ++;
        if(isa::pdec_isB(p->inst)) {
            if(fetch->branchs[bridx].pred_taken >= 0) {
                // 已经跳走了，后面的就不用往后端发了
                brout = true;
            }
            else {
                bridx++;
            }
        }
    }

    fetch->insts.resize(instcnt);
    fetch->commit_cnt = 0;

    pdec_result_queue->pop();

}

void XiangShanCPU::apl_bpu_redirect() {
    if(!control.bpu_redirect.valid) return;
    control.bpu_redirect.valid = false;

    FTQEntry * fetch = control.bpu_redirect.data; // 这一组取出来的指令是正确的，已经下发，所以从这一条之后开始删

    VirtAddrT nextpc = fetch->endpc;
    if(fetch->jmptarget != 0) {
        // jalr的分支预测只与pc有关，可以保留
        nextpc = fetch->jmptarget;
    }
    BrHist hist = fetch->bhr;
    bool brout = false;
    // 更新分支历史到预测这个pack之后的状态
    for(auto &br : fetch->branchs) { 
        hist.push(br.pred_taken >= 0);
        if(br.pred_taken >= 0) {
            nextpc = fetch->startpc + br.inst_offset + br.jmp_offset;
            brout = true;
            break;
        }
    }

    if(!nextpc) [[unlikely]] {
        // 完全未知的jalr指令，暂时阻塞等待这个jalr的重定向
        insert_error(ifu_errors, SimError::invalidpc, fetch->startpc, fetch->endpc, 1);
        return;
    }

    // 更新RAS
    RASSnapShot ras = fetch->ras;
    if(!brout && (fetch->jmpinfo & FETCH_FLAG_CALL)) {
        ras.push(fetch->endpc);
    }
    else if(!brout && (fetch->jmpinfo & FETCH_FLAG_RET)) {
        ras.pop();
    }

    bpu->apl_redirect(nextpc, hist, ras);

    fetch_result_queue->clear();
    pdec_result_queue->clear(); // 这两个队列里的指针是从ftq中取的，都在ftq里有，所以不用额外释放
    auto iter = ftq.begin();
    ftq_pos = 0;
    // 回收ftq里该fetch之后的所有项
    while(*iter != fetch) {
        std::advance(iter, 1);
        ftq_pos ++;
    }
    ftq_pos ++;
    std::advance(iter, 1);
    while(iter != ftq.end()) {
        delete (*iter);
        iter = ftq.erase(iter);
    }

    ifu_errors.clear();
    
    if(debug_pipeline_ofile) {
        sprintf(log_buf, "%ld:PDEC: Redirect to 0x%lx", simroot::get_current_tick(), nextpc);
        simroot::log_line(debug_pipeline_ofile, log_buf);
    }
}

void XiangShanCPU::ifu_on_current_tick() {
    if(ifu_errors.empty() && dec_errors.empty()) {
        bpu->on_current_tick();
        _cur_fetch_one_pack();
    }
    if(dec_errors.empty()) {
        _cur_predecode();
        _cur_pred_check();
    }
}

void XiangShanCPU::_cur_decode() {
    while(dec_to_rnm->can_push() && inst_buffer->can_pop() && !unique_inst_in_pipeline) {
        RV64InstDecoded dec;
        XSInst *inst = inst_buffer->top();
        bool res = isa::decode_rv64(inst->inst, &dec);
        if(!res) [[unlikely]] {
            insert_error(dec_errors, SimError::illegalinst, inst->pc, inst->inst, 0);
            break;
        }

        inst->id = current_inst_id++;
        inst->opcode = dec.opcode;
        inst->param = dec.param;
        inst->flag = dec.flag;
        inst->imm = dec.imm;
        inst->vrd = dec.rd;
        inst->vrs[0] = dec.rs1;
        inst->vrs[1] = dec.rs2;
        inst->vrs[2] = dec.rs3;
        inst->prd = inst->prs[0] = inst->prs[1] = inst->prs[2] = inst->prstale = 0;
        inst->err = SimError::success;
        inst->finished = false;

        if(debug_pipeline || log_inst_to_file || log_inst_to_stdout) {
            isa::init_rv64_inst_name_str(&dec);
            inst->dbgname.swap(dec.debug_name_str);
        }

        if(debug_pipeline_ofile) {
            sprintf(log_buf, "%ld:DEC: (%ld) @0x%lx, %ld, %s",
                simroot::get_current_tick(), inst_buffer->cur_size(), inst->pc, inst->id, inst->dbgname.c_str()
            );
            simroot::log_line(debug_pipeline_ofile, log_buf);
        }

        inst_buffer->pass_to(*dec_to_rnm);

        if(inst->flag & RVINSTFLAG_UNIQUE) [[unlikely]] unique_inst_in_pipeline = true;

    }
}

void XiangShanCPU::_cur_rename() {
    while(dec_to_rnm->can_pop() && rnm_to_disp->can_push()) {
        XSInst *inst = dec_to_rnm->top();
        if((inst->opcode == RV64OPCode::branch || inst->opcode == RV64OPCode::jalr) && irnm.checkpoint.size() >= param.branch_inst_cnt) {
            // 没有多余的重命名表备份空间，pass
            return;
        }
        if((inst->flag & RVINSTFLAG_RDINT) && irnm.freelist.empty()) return;
        if((inst->flag & RVINSTFLAG_RDFP) && frnm.freelist.empty()) return;

        if(inst->flag & RVINSTFLAG_S1INT) inst->prs[0] = irnm.table[inst->vrs[0]];
        else if(inst->flag & RVINSTFLAG_S1FP) inst->prs[0] = frnm.table[inst->vrs[0]];
        else inst->prs[0] = 0;
        if(inst->flag & RVINSTFLAG_S2INT) inst->prs[1] = irnm.table[inst->vrs[1]];
        else if(inst->flag & RVINSTFLAG_S2FP) inst->prs[1] = frnm.table[inst->vrs[1]];
        else inst->prs[1] = 0;
        if(inst->flag & RVINSTFLAG_S3FP) [[unlikely]] inst->prs[2] = frnm.table[inst->vrs[2]];
        else inst->prs[2] = 0;
        if((inst->flag & RVINSTFLAG_RDINT) && inst->vrd) {
            inst->prd = irnm.freelist.front();
            irnm.freelist.pop_front();
            inst->prstale = irnm.table[inst->vrd];
            irnm.table[inst->vrd] = inst->prd;
        }
        else if(inst->flag & RVINSTFLAG_RDFP) {
            inst->prd = frnm.freelist.front();
            frnm.freelist.pop_front();
            inst->prstale = frnm.table[inst->vrd];
            frnm.table[inst->vrd] = inst->prd;
        }
        else inst->prd = inst->prstale = 0;

        if(inst->opcode == RV64OPCode::branch || inst->opcode == RV64OPCode::jalr) {
            // 保存一份rename表的备份，用于分支预测错误时的重定向
            irnm.save(inst);
            frnm.save(inst);
        }

        dec_to_rnm->pass_to(*rnm_to_disp);

        if(debug_pipeline_ofile) {
            sprintf(log_buf, "%ld:RNM: @0x%lx, %ld, %s", simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str());
            string str(log_buf);
            if(inst->flag & (RVINSTFLAG_RDINT | RVINSTFLAG_RDFP)) {
                sprintf(log_buf, ", RD:%d->%d / %d", inst->vrd, inst->prd, inst->prstale);
                str += log_buf;
            }
            if(inst->flag & (RVINSTFLAG_S1INT | RVINSTFLAG_S1FP)) {
                sprintf(log_buf, ", RS1:%d->%d", inst->vrs[0], inst->prs[0]);
                str += log_buf;
            }
            if(inst->flag & (RVINSTFLAG_S2INT | RVINSTFLAG_S2FP)) {
                sprintf(log_buf, ", RS2:%d->%d", inst->vrs[1], inst->prs[1]);
                str += log_buf;
            }
            if(inst->flag & (RVINSTFLAG_S3INT | RVINSTFLAG_S3FP)) {
                sprintf(log_buf, ", RS3:%d->%d", inst->vrs[2], inst->prs[2]);
                str += log_buf;
            }
            simroot::log_line(debug_pipeline_ofile, str);
        }
    }
}

void XiangShanCPU::_cur_dispatch() {
    while(rnm_to_disp->can_pop() && rob.size() + apl_rob_push.size() < param.rob_size && dq_int->can_push() && dq_ls->can_push() && dq_fp->can_push()) {
        XSInst *inst = rnm_to_disp->top();

        if(inst->opcode == RV64OPCode::system) [[unlikely]] {
            // CSR，ECALL，EBREAK指令直接等提交阶段完成，不加入dispath队列
            inst->finished = true;
            rnm_to_disp->pop();
            if(debug_pipeline_ofile) {
                sprintf(log_buf, "%ld:DISP-SYS: @0x%lx, %ld, %s", simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str());
                simroot::log_line(debug_pipeline_ofile, log_buf);
            }
        }
        else {
            inst->finished = false;
            DispType disp = disp_type(inst);
            switch (disp)
            {
            case DispType::alu:
                rnm_to_disp->pass_to(*dq_int);
                if(debug_pipeline_ofile) {
                    sprintf(log_buf, "%ld:DISP-ALU: @0x%lx, %ld, %s", simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str());
                    simroot::log_line(debug_pipeline_ofile, log_buf);
                }
                break;
            case DispType::mem:
                rnm_to_disp->pass_to(*dq_ls);
                if(debug_pipeline_ofile) {
                    sprintf(log_buf, "%ld:DISP-MEM: @0x%lx, %ld, %s", simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str());
                    simroot::log_line(debug_pipeline_ofile, log_buf);
                }
                break;
            case DispType::fp:
                rnm_to_disp->pass_to(*dq_fp);
                if(debug_pipeline_ofile) {
                    sprintf(log_buf, "%ld:DISP-FP: @0x%lx, %ld, %s", simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str());
                    simroot::log_line(debug_pipeline_ofile, log_buf);
                }
                break;
            default:
                simroot_assert(0);
            }
        }

        if((inst->flag & RVINSTFLAG_RDINT) && inst->vrd) {
            ireg.busy[inst->prd] = true;
        }
        else if(inst->flag & RVINSTFLAG_RDFP) {
            freg.busy[inst->prd] = true;
        }
        inst->arg0 = inst->arg1 = 0;
        inst->rsready[0] = inst->rsready[1] = inst->rsready[2] = false;
        apl_rob_push.push_back(inst);
    }
}

void XiangShanCPU::decdisp_on_current_tick() {
    if(dec_errors.empty()) {
        _cur_decode();
    }
    _cur_rename();
    _cur_dispatch();
}

void XiangShanCPU::cur_commit() {
    dead_loop_detact_tick++;
    if(dead_loop_detact_tick == dead_loop_warn_tick) [[unlikely]] {
        sprintf(log_buf, "CPU%d: %ld ticks without inst commited", cpu_id, dead_loop_warn_tick);
        LOG(WARNING) << log_buf;
    }
    for(int __n = 0; !rob.empty() && __n < param.decode_width; __n++) {
        XSInst *inst = rob.front();
        if(!inst->finished) return;

        if(debug_pipeline_ofile) {
            sprintf(log_buf, "%ld:COMMIT: (%ld) @0x%lx, %ld, %s, (0x%lx, 0x%lx, 0x%lx)",
                simroot::get_current_tick(), rob.size(), inst->pc, inst->id, inst->dbgname.c_str(),
                inst->arg0, inst->arg1, inst->arg2
            );
            simroot::log_line(debug_pipeline_ofile, log_buf);
        }
        dead_loop_detact_tick = 0;

        bool commit_finished = false;
        bool dealloc_inst = true;
        bool update_rename = true;
        FTQEntry *fetch = inst->ftq;
        // 正在提交的FetchPackage必须是FTQ程序顺序的第一项
        simroot_assert(fetch == ftq.front());

        if(inst->opcode == RV64OPCode::branch) {
            statistic.br_inst_cnt ++;
            if(inst->vrs[0] == 1) statistic.ret_inst_cnt ++;
            bool istaken = (inst->arg0 != 0);
            uint16_t instoff = inst->pc - fetch->startpc;
            uint16_t bridx = 0;
            for(;bridx < fetch->branchs.size(); bridx++) if(fetch->branchs[bridx].inst_offset == instoff) break;
            simroot_assert(bridx < fetch->branchs.size());
            simroot_assert(RAW_DATA_AS(inst->imm).i32 == fetch->branchs[bridx].jmp_offset);
            bool predtaken = (fetch->branchs[bridx].pred_taken >= 0);
            if(bridx + 1 == fetch->branchs.size() || !BOEQ(istaken, predtaken)) {
                // 这是这组Fetch Package运行的最后一条分支指令，更新对这个fetch的分支预测
                vector<bool> istakens;
                istakens.assign(bridx + 1, false); // 前面的肯定都没跳转才会执行到这一句
                istakens[bridx] = istaken;
                bpu->cur_commit_branch(fetch, istakens);
            }
            if(!BOEQ(istaken, predtaken)) {
                control.jmp_redirect.valid = true;
                control.jmp_redirect.data = inst;
                dealloc_inst = false;
                // 指令算提交，但指针的释放要留到apply阶段，因为apply的时候还需要设置bhr与ras以及恢复寄存器映射表
                commit_finished = true;
            }
            else {
                // 分支指令预测成功，可以释放备份映射表
                irnm.checkpoint.erase(inst);
                frnm.checkpoint.erase(inst);
                statistic.br_pred_hit_cnt++;
                if(inst->vrs[0] == 1) statistic.ret_pred_hit_cnt ++;
            }
        }
        else if(inst->opcode == RV64OPCode::jalr) {
            statistic.jalr_inst_cnt++;
            VirtAddrT predtarget = fetch->jmptarget;
            VirtAddrT jmptarget = inst->arg1;
            if(predtarget != jmptarget) {
                control.jmp_redirect.valid = true;
                control.jmp_redirect.data = inst;
                dealloc_inst = false;
                // 指令算提交，但指针的释放要留到apply阶段，因为apply的时候还需要设置bhr与ras以及恢复寄存器映射表
                commit_finished = true;
            }
            else {
                // 分支指令预测成功，可以释放备份映射表
                irnm.checkpoint.erase(inst);
                frnm.checkpoint.erase(inst);
                statistic.jalr_pred_hit_cnt++;
            }
            simroot_assert(fetch->commit_cnt + 1 == fetch->insts.size());
            fetch->jmptarget = jmptarget;
            bpu->cur_commit_jalr_target(fetch);
            commit_finished = true;
        }
        else if(inst->opcode == RV64OPCode::load || inst->opcode == RV64OPCode::loadfp) {
            lsu->cur_commit_load(inst);
            statistic.ld_inst_cnt++;
        }
        else if(inst->opcode == RV64OPCode::store || inst->opcode == RV64OPCode::storefp) {
            lsu->cur_commit_store(inst);
            statistic.st_inst_cnt++;
        }
        else if(inst->opcode == RV64OPCode::amo) {
            if(!lsu->cur_commit_amo(inst)) return;
            commit_finished = true;
            statistic.amo_inst_cnt++;
        }
        else if(inst->opcode == RV64OPCode::system) {
            if(__n != 0) {
                // 等待前面所有指令都被完全提交再执行system指令
                return;
            }
            _do_system_inst(inst);
            commit_finished = true;
            if(control.sys_redirect.valid) {
                dealloc_inst = false;
            }
            statistic.sys_inst_cnt++;
        }
        else if(inst->opcode == RV64OPCode::miscmem) {
            statistic.mem_inst_cnt++;
        }

        // 指令异常的处理
        #define PERR(fmt, ...) sprintf(log_buf, fmt, ##__VA_ARGS__ ); LOG(ERROR)<<(log_buf); simroot_assert(0);
        switch (inst->err)
        {
        case SimError::illegalinst : PERR("Illegal Inst @0x%lx: 0x%x", inst->pc, inst->inst);
        case SimError::devidebyzero : PERR("Devide by Zero @0x%lx: 0x%x", inst->pc, inst->inst);
        case SimError::invalidaddr : PERR("Invalid Memroy Access @0x%lx: 0x%x, vaddr:0x%lx", inst->pc, inst->inst, inst->arg1);
        case SimError::unaligned : PERR("Unaligned Memroy Address @0x%lx: 0x%x, vaddr:0x%lx", inst->pc, inst->inst, inst->arg1);
        case SimError::unaccessable : PERR("Non-permission Memroy Access @0x%lx: 0x%x, vaddr:0x%lx", inst->pc, inst->inst, inst->arg1);
        case SimError::slreorder : case SimError::llreorder : // 从取指开始重新执行该指令
            control.reorder_redirect_pc.valid = true;
            control.reorder_redirect_pc.data = inst;
            commit_finished = true;
            dealloc_inst = false;
            update_rename = false;
            break;
        }
        #undef PERR

        if(update_rename) [[likely]] {
            if((inst->flag & RVINSTFLAG_RDINT) && inst->vrd) {
                ireg.apl_wb[inst->prd] = inst->arg0;
                irnm.freelist.push_back(inst->prstale);
                irnm.commited_table[inst->vrd] = inst->prd;
            }
            else if(inst->flag & RVINSTFLAG_RDFP) {
                freg.apl_wb[inst->prd] = inst->arg0;
                frnm.freelist.push_back(inst->prstale);
                frnm.commited_table[inst->vrd] = inst->prd;
            }
            statistic.finished_inst_cnt++;
        }

        if(inst->flag & RVINSTFLAG_UNIQUE) [[unlikely]] {
            unique_inst_in_pipeline = false;
            commit_finished = true;
        }

        if((log_inst_ofile || log_inst_to_stdout) && update_rename) {
            sprintf(log_buf, "%ld: 0x%lx, %ld, %s, (0x%lx, 0x%lx, 0x%lx)", 
                simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(),
                inst->arg0, inst->arg1, inst->arg2
            );
            string str(log_buf);
            if(log_regs) {
                for(int i = 0; i < 32; i+=2) {
                    PhysReg pr1 = irnm.commited_table[i], pr2 = irnm.commited_table[i+1];
                    PhysReg pr3 = frnm.commited_table[i], pr4 = frnm.commited_table[i+1];
                    if(log_fregs) {
                        sprintf(log_buf, "\n    %s(%02d)-%d: 0x%16lx, %s(%02d)-%d: 0x%16lx,    %s(%02d)-%d: 0x%16lx, %s(%02d)-%d: 0x%16lx,",
                            isa::ireg_names()[i], pr1, ireg.busy[pr1]?1:0, ireg.regfile[pr1],
                            isa::ireg_names()[i+1], pr2, ireg.busy[pr2]?1:0, ireg.regfile[pr2],
                            isa::freg_names()[i], pr3, freg.busy[pr3]?1:0, freg.regfile[pr3],
                            isa::freg_names()[i+1], pr4, freg.busy[pr4]?1:0, freg.regfile[pr4]
                        );
                    }
                    else {
                        sprintf(log_buf, "\n    %s(%02d)-%d: 0x%16lx, %s(%02d)-%d: 0x%16lx,",
                            isa::ireg_names()[i], pr1, ireg.busy[pr1]?1:0, ireg.regfile[pr1],
                            isa::ireg_names()[i+1], pr2, ireg.busy[pr2]?1:0, ireg.regfile[pr2]
                        );
                    }
                    str += (string(log_buf));
                }
            }
            if(log_inst_ofile) simroot::log_line(log_inst_ofile, str);
            if(log_inst_to_stdout) {
                printf("CPU%d:%s\n", cpu_id, str.c_str());
            }
        }

        if(dealloc_inst) [[likely]] {
            delete inst;

            fetch->commit_cnt++;
            if(fetch->commit_cnt >= fetch->insts.size()) {
                // 整个Fetch全部被Commit
                delete fetch;
                ftq.pop_front();
                simroot_assert(ftq_pos);
                ftq_pos --;
                commit_finished = true;
            }
        }
        rob.pop_front();

        if(commit_finished) {
            return;
        }
    }

    // 处理前端错误，检查流水线里还有没有指令，即ROB、DEC2RNM、RNM2DISP队列是否为空
    #define PERR(fmt, ...) sprintf(log_buf, fmt, ##__VA_ARGS__ ); LOG(ERROR)<<(log_buf); simroot_assert(0);
    if(!ifu_errors.empty() || !dec_errors.empty()) {
        bool backend_empty = (rob.empty() && apl_rob_push.empty() && dec_to_rnm->empty() && rnm_to_disp->empty());
        bool frontend_empty = (inst_buffer->empty() && pdec_result_queue->empty() && fetch_result_queue->empty());
        bool dec_err = (!dec_errors.empty() && backend_empty);
        bool ifu_err = (!ifu_errors.empty() && backend_empty && frontend_empty);
        if(!control.jmp_redirect.valid && (dec_err || ifu_err)) {
            SimErrorData &s = (dec_errors.empty()?(ifu_errors.front()):(dec_errors.front()));
            switch (s.type)
            {
            case SimError::illegalinst : PERR("Illegal Inst @0x%lx: 0x%lx", s.args[0], s.args[1]);
            case SimError::unaligned : PERR("Unaligned ICache Access @0x%lx, length 0x%lx", s.args[0], s.args[1]);
            case SimError::invalidpc : PERR("Invalid ICache Access @0x%lx, length 0x%lx", s.args[0], s.args[1]);
            case SimError::unexecutable : PERR("Unexecutable ICache Access @0x%lx, length 0x%lx", s.args[0], s.args[1]);
            }
        }
    }
    #undef PERR

}

void XiangShanCPU::cur_int_disp2() {
    while(dq_int->can_pop()) {
        XSInst *inst = dq_int->top();
        RV64OPCode opcode = inst->opcode;

        IntFPEXU* disp_target = nullptr;
        if(opcode == RV64OPCode::branch || opcode == RV64OPCode::opfp) {
            disp_target = (misc.get());
        }
        else {
            auto intop = inst->param.intop;
            switch (intop)
            {
            case isa::RV64IntOP73::MUL :
            case isa::RV64IntOP73::MULH :
            case isa::RV64IntOP73::MULHSU :
            case isa::RV64IntOP73::MULHU :
            case isa::RV64IntOP73::DIV :
            case isa::RV64IntOP73::DIVU :
            case isa::RV64IntOP73::REM :
            case isa::RV64IntOP73::REMU :
                disp_target = (mdu.get());
                break;
            default:
                disp_target = ((alu1->rs.size() < alu2->rs.size())?(alu1.get()):(alu2.get()));
                break;
            }
        }

        if(disp_target->rs.can_push() == 0) break;

        _do_reg_read(inst, DispType::alu);
        simroot_assert(disp_target->rs.push_next_tick(inst));

        dq_int->pop();

        if(debug_pipeline_ofile) {
            sprintf(log_buf, "%ld:RS-I: @0x%lx, %ld, %s, Ready:(%d,%d,%d)",
                simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(),
                inst->rsready[0], inst->rsready[1], inst->rsready[2]
            );
            simroot::log_line(debug_pipeline_ofile, log_buf);
        }
    }
}

void XiangShanCPU::cur_fp_disp2() {
    while(dq_fp->can_pop()) {
        XSInst *inst = dq_fp->top();
        RV64OPCode opcode = inst->opcode;

        IntFPEXU* disp_target = nullptr;
        if(opcode == RV64OPCode::opfp) {
            auto fpop = inst->param.fp.op;
            switch (fpop)
            {
            case isa::RV64FPOP5::MVF2I :
            case isa::RV64FPOP5::CVTF2I :
            case isa::RV64FPOP5::MVF2F :
            case isa::RV64FPOP5::CMP :
                disp_target = (fmisc.get());
                break;
            }
        }
        if(disp_target == nullptr) {
            disp_target = ((fmac1->rs.size() < fmac2->rs.size())?(fmac1.get()):(fmac2.get()));
        }

        if(disp_target->rs.can_push() == 0) break;

        _do_reg_read(inst, DispType::fp);
        simroot_assert(disp_target->rs.push_next_tick(inst));

        dq_fp->pop();

        if(debug_pipeline_ofile) {
            sprintf(log_buf, "%ld:RS-F: @0x%lx, %ld, %s, Ready:(%d,%d,%d)",
                simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(),
                inst->rsready[0], inst->rsready[1], inst->rsready[2]
            );
            simroot::log_line(debug_pipeline_ofile, log_buf);
        }
    }
}


/**
 * 在指令进入保留站时读取寄存器文件与旁路网络，初始化操作数状态
*/
void XiangShanCPU::_do_reg_read(XSInst *inst, DispType disp) {
    auto flg = inst->flag;
    memset(inst->rsready, 0, sizeof(inst->rsready));

    bool s1_int = (flg & RVINSTFLAG_S1INT);
    bool s1_fp = (flg & RVINSTFLAG_S1FP);
    bool s2_int = (flg & RVINSTFLAG_S2INT);
    bool s2_fp = (flg & RVINSTFLAG_S2FP);
    bool s3_fp = (flg & RVINSTFLAG_S3FP);

    bool *pready1 = &(inst->rsready[0]);
    bool *pready2 = &(inst->rsready[1]);
    bool *pready3 = &(inst->rsready[2]);

    RawDataT *ps1 = &(inst->arg0);
    RawDataT *ps2 = &(inst->arg1);
    RawDataT *ps3 = &(inst->arg2);

    PhysReg rs1 = inst->prs[0];
    PhysReg rs2 = inst->prs[1];
    PhysReg rs3 = inst->prs[2];

    if(disp == DispType::mem) {
        std::swap(ps1, ps2);
    }

    if(s1_int) {
        if(rs1) *pready1 = _do_try_get_reg(rs1, RVRegType::i, ps1);
        else {
            *pready1 = true;
            *ps1 = 0;
        }
        if(!(*pready1)) {
            WaitOPRand tmp{.wb_ready = pready1, .wb_value = ps1};
            ireg_waits->push_next_tick(rs1, tmp);
        }
    }
    else if(s1_fp) {
        if(!(*pready1 = _do_try_get_reg(rs1, RVRegType::f, ps1))) {
            WaitOPRand tmp{.wb_ready = pready1, .wb_value = ps1};
            freg_waits->push_next_tick(rs1, tmp);
        }
    }

    if(s2_int) {
        if(rs2) *pready2 = _do_try_get_reg(rs2, RVRegType::i, ps2);
        else {
            *pready2 = true;
            *ps2 = 0;
        }
        if(!(*pready2)) {
            WaitOPRand tmp{.wb_ready = pready2, .wb_value = ps2};
            ireg_waits->push_next_tick(rs2, tmp);
        }
    }
    else if(s2_fp) {
        if(!(*pready2 = _do_try_get_reg(rs2, RVRegType::f, ps2))) {
            WaitOPRand tmp{.wb_ready = pready2, .wb_value = ps2};
            freg_waits->push_next_tick(rs2, tmp);
        }
    }

    if(s3_fp) [[unlikely]] {
        if(!(*pready3 = _do_try_get_reg(rs3, RVRegType::f, ps3))) {
            WaitOPRand tmp{.wb_ready = pready3, .wb_value = ps3};
            freg_waits->push_next_tick(rs3, tmp);
        }
    }

}

bool XiangShanCPU::_do_try_get_reg(PhysReg rx, RVRegType type, RawDataT *buf) {
    auto regs = &ireg;
    if(type == RVRegType::f) regs = &freg;

    if(!regs->busy[rx]) {
        *buf = regs->regfile[rx];
        return true;
    }
    auto res = regs->bypass.find(rx);
    if(res != regs->bypass.end()) {
        *buf = res->second;
        return true;
    }
    return false;
}

void XiangShanCPU::_cur_wakeup_exu_bypass() {
    if(debug_pipeline_ofile && ((!ireg.wakeup.empty()) || (!freg.wakeup.empty()))) {
        sprintf(log_buf, "%ld:WAKEUP: I(", simroot::get_current_tick());
        string str(log_buf);
        for(auto newreg : ireg.wakeup) {
            sprintf(log_buf, "%d ", newreg);
            str += log_buf;
        }
        str += "), F(";
        for(auto newreg : freg.wakeup) {
            sprintf(log_buf, "%d ", newreg);
            str += log_buf;
        }
        str += ")";
        simroot::log_line(debug_pipeline_ofile, str);
    }

    {
        auto &wait = ireg_waits->get();
        for(auto newreg : ireg.wakeup) {
            auto res = wait.find(newreg);
            if(res == wait.end()) continue;
            uint64_t cnt = wait.count(newreg);
            RawDataT value = 0;
            simroot_assert(_do_try_get_reg(newreg, RVRegType::i, &value));
            for(uint64_t i = 0; i < cnt; i++, res++) {
                WaitOPRand &entry = res->second;
                *(entry.wb_ready) = true;
                *(entry.wb_value) = value;
            }
            wait.erase(newreg);
        }
    }
    {
        auto &wait = freg_waits->get();
        for(auto newreg : freg.wakeup) {
            auto res = wait.find(newreg);
            if(res == wait.end()) continue;
            uint64_t cnt = wait.count(newreg);
            RawDataT value = 0;
            simroot_assert(_do_try_get_reg(newreg, RVRegType::f, &value));
            for(uint64_t i = 0; i < cnt; i++, res++) {
                WaitOPRand &entry = res->second;
                *(entry.wb_ready) = true;
                *(entry.wb_value) = value;
            }
            wait.erase(newreg);
        }
    }
}

/**
 * 选择操作数就绪的指令发射到执行单元，写回执行单元中已完成的指令
*/
void XiangShanCPU::_cur_intfp_exu(IntFPEXU *exu) {

    vector<OperatingInst*> free_units;
    for(auto &unit : exu->opunit) {
        if(unit.inst) {
            unit.processed ++;
            if(unit.processed >= unit.latency) {
                if(debug_pipeline_ofile) {
                    sprintf(log_buf, "%ld:%s-FINISH: @0x%lx, %ld, %s",
                        simroot::get_current_tick(), exu->name.c_str(), unit.inst->pc, unit.inst->id, unit.inst->dbgname.c_str()
                    );
                    simroot::log_line(debug_pipeline_ofile, log_buf);
                }
                apl_finished_inst.push_back(unit.inst);
                if((unit.inst->flag & RVINSTFLAG_RDINT) && unit.inst->vrd) {
                    simroot_assert(ireg.apl_bypass.emplace(unit.inst->prd, unit.inst->arg0).second);
                }
                else if(unit.inst->flag & RVINSTFLAG_RDFP) {
                    simroot_assert(freg.apl_bypass.emplace(unit.inst->prd, unit.inst->arg0).second);
                }
                unit.inst = nullptr;
            }
        }
        if(!unit.inst) {
            free_units.push_back(&unit);
        }
    }

    auto &rs = exu->rs.get();
    auto iter = rs.begin();
    for(auto unit : free_units) {
        for(; iter != rs.end(); iter++) {
            if(inst_ready(*iter)) break;
        }
        if(iter == rs.end()) break;
        auto inst = *iter;
        unit->inst = inst;
        unit->processed = 0;
        unit->latency = _get_exu_latency(inst);
        _do_op_inst(inst);
        iter = rs.erase(iter);

        if(debug_pipeline_ofile) {
            sprintf(log_buf, "%ld:%s-START: @0x%lx, %ld, %s, Latency %d",
                simroot::get_current_tick(), exu->name.c_str(), inst->pc, inst->id, inst->dbgname.c_str(), unit->latency
            );
            simroot::log_line(debug_pipeline_ofile, log_buf);
        }
    }

}

void XiangShanCPU::cur_mem_disp2() {
    while(dq_ls->can_pop()) {
        XSInst *inst = dq_ls->top();
        RV64OPCode opcode = inst->opcode;

        if(!rs_amo->empty() || !rs_fence->empty()) [[unlikely]] {
            return;
        }

        if(opcode == RV64OPCode::load || opcode == RV64OPCode::loadfp) {
            if(rs_ld->can_push() == 0) return;
            simroot_assert(rs_ld->push_next_tick(inst));
        }
        else if(opcode == RV64OPCode::store || opcode == RV64OPCode::storefp) {
            if(rs_sta->can_push() == 0 || rs_std->can_push() == 0) return;
            simroot_assert(rs_sta->push_next_tick(inst));
            simroot_assert(rs_std->push_next_tick(inst));
        }
        else if(opcode == RV64OPCode::amo) [[unlikely]] {
            simroot_assert(rs_amo->push_next_tick(inst));
        }
        else if(opcode == RV64OPCode::miscmem) [[unlikely]] {
            simroot_assert(rs_fence->push_next_tick(inst));
        }
        else [[unlikely]] {
            simroot_assert(0);
        }

        _do_reg_read(inst, DispType::mem);

        dq_ls->pop();

        if(debug_pipeline_ofile) {
            sprintf(log_buf, "%ld:RS-M: @0x%lx, %ld, %s, Ready:(%d,%d,%d)",
                simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(),
                inst->rsready[0], inst->rsready[1], inst->rsready[2]
            );
            simroot::log_line(debug_pipeline_ofile, log_buf);
        }
    }
}






void XiangShanCPU::apl_global_redirect() {
    if(control.global_redirect_pc.valid == false) return;
    VirtAddrT nextpc = control.global_redirect_pc.data;
    control.global_redirect_pc.valid = false;
    
    if(control.global_redirect_reg.valid) {
        auto &regs = control.global_redirect_reg.data;
        irnm.table = irnm.commited_table;
        frnm.table = frnm.commited_table;
        for(int i = 1; i < RV_REG_CNT_INT; i++) {
            ireg.regfile[irnm.commited_table[i]] = regs[i];
        }
        for(int i = 0; i < RV_REG_CNT_FP; i++) {
            freg.regfile[frnm.commited_table[i]] = regs[RV_REG_CNT_INT + i];
        }
    }
    control.global_redirect_reg.valid = false;

    bpu->apl_redirect(nextpc);
    _apl_clear_pipeline();

    if(debug_pipeline_ofile) {
        sprintf(log_buf, "%ld:FORCE_REDIRECT: @0x%lx", simroot::get_current_tick(), nextpc);
        simroot::log_line(debug_pipeline_ofile, log_buf);
    }

    control.is_halt = false;
}

void XiangShanCPU::_apl_general_cpu_redirect(XSInst *inst, VirtAddrT nextpc) {

    // 跳转位置
    if(nextpc == 0) {
        nextpc = inst->pc + ((inst->flag & RVINSTFLAG_RVC)?2:4);
        if((inst->opcode == RV64OPCode::branch && (inst->arg0 != 0)) ||
            (inst->opcode == RV64OPCode::jalr) ||
            (inst->opcode == RV64OPCode::jal) ||
            (inst->opcode == RV64OPCode::system)
        ) {
            nextpc = inst->arg1;
        }
    }

    // 恢复分支预测现场
    FTQEntry * fetch = inst->ftq;
    BrHist bhr = fetch->bhr;
    RASSnapShot ras = fetch->ras;
    uint16_t instoff = inst->pc - fetch->startpc;
    for(auto &br : fetch->branchs) {
        if(br.inst_offset < instoff) bhr.push(false);
    }
    if(inst->opcode == RV64OPCode::branch) {
        bhr.push(inst->arg0 != 0);
    }
    else if((inst->opcode == RV64OPCode::jalr || inst->opcode == RV64OPCode::jal) && inst->vrd == 1) {
        ras.push((inst->flag & RVINSTFLAG_RVC)?(inst->pc+2):(inst->pc+4));
    }
    else if(inst->opcode == RV64OPCode::jalr && inst->vrs[0] == 1) {
        ras.pop();
    }
    
    bpu->apl_redirect(nextpc, bhr, ras);

    if(debug_pipeline_ofile) {
        sprintf(log_buf, "%ld:REDIRECT: FROM @0x%lx, %ld, %s, TO 0x%lx",
            simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(), nextpc
        );
        simroot::log_line(debug_pipeline_ofile, log_buf);
    }

    delete inst;

    _apl_clear_pipeline();
}

void XiangShanCPU::apl_jmp_redirect() {
    if(control.jmp_redirect.valid == false) return;
    XSInst *inst = control.jmp_redirect.data;
    control.jmp_redirect.valid = false;
    
    // 恢复重命名表
    irnm.checkout(inst);
    frnm.checkout(inst);

    _apl_general_cpu_redirect(inst);
}

void XiangShanCPU::apl_sys_redirect() {
    if(control.sys_redirect.valid == false) return;
    XSInst *inst = control.sys_redirect.data;
    control.sys_redirect.valid = false;
    
    _apl_general_cpu_redirect(inst);
}

void XiangShanCPU::apl_reorder_redirect() {
    if(control.reorder_redirect_pc.valid == false) return;
    XSInst *inst = control.reorder_redirect_pc.data;
    control.reorder_redirect_pc.valid = false;
    
    _apl_general_cpu_redirect(inst, inst->pc);
}


void XiangShanCPU::_apl_clear_pipeline() {
    
    control.global_redirect_pc.valid = false;
    control.global_redirect_reg.valid = false;
    control.reorder_redirect_pc.valid = false;
    control.jmp_redirect.valid = false;
    control.bpu_redirect.valid = false;
    control.sys_redirect.valid = false;

    ifu_errors.clear();
    dec_errors.clear();

    list<XSInst*> to_free_inst;
    list<FTQEntry*> to_free_ftq;

    to_free_ftq.splice(to_free_ftq.end(), ftq);
    ftq_pos = 0;

    fetchbuf.buf.clear();
    fetchbuf.bytecnt = fetchbuf.brcnt = 0;

    fetch_result_queue->clear();
    pdec_result_queue->clear();

    inst_buffer->clear(&to_free_inst);
    dec_to_rnm->clear(&to_free_inst);
    rnm_to_disp->clear(&to_free_inst);

    dq_int->clear();
    dq_ls->clear();
    dq_fp->clear();

    irnm.table = irnm.commited_table;
    irnm.reset_freelist(param.phys_ireg_cnt);
    irnm.checkpoint.clear();
    frnm.table = frnm.commited_table;
    frnm.reset_freelist(param.phys_freg_cnt);
    frnm.checkpoint.clear();
    
    ireg.apl_clear(param.phys_ireg_cnt);
    freg.apl_clear(param.phys_freg_cnt);

    to_free_inst.splice(to_free_inst.end(), rob);
    to_free_inst.splice(to_free_inst.end(), apl_rob_push);
    rob.clear();
    apl_rob_push.clear();

    unique_inst_in_pipeline = false;

    ireg_waits->clear();
    freg_waits->clear();

    mdu->clear();
    alu1->clear();
    alu2->clear();
    misc->clear();
    fmac1->clear();
    fmac2->clear();
    fmisc->clear();

    apl_finished_inst.clear();

    rs_ld->clear();
    rs_sta->clear();
    rs_std->clear();
    rs_amo->clear();
    rs_fence->clear();

    lsu->apl_clear_pipeline();

    for(auto p : to_free_inst) {
        delete p;
    }
    for(auto p : to_free_ftq) {
        delete p;
    }
}

void XiangShanCPU::_apl_forward_pipeline() {

    bpu->apply_next_tick();
    
    fetch_result_queue->apply_next_tick();
    pdec_result_queue->apply_next_tick();
    inst_buffer->apply_next_tick();

    dec_to_rnm->apply_next_tick();
    rnm_to_disp->apply_next_tick();
    dq_int->apply_next_tick();
    dq_ls->apply_next_tick();
    dq_fp->apply_next_tick();

    ireg.apply_next_tick();
    freg.apply_next_tick();

    rob.splice(rob.end(), apl_rob_push);

    ireg_waits->apply_next_tick();
    freg_waits->apply_next_tick();

    mdu->apply_next_tick();
    alu1->apply_next_tick();
    alu2->apply_next_tick();
    misc->apply_next_tick();
    fmac1->apply_next_tick();
    fmac2->apply_next_tick();
    fmisc->apply_next_tick();

    for(auto inst: apl_finished_inst) {
        inst->finished = true;
    }
    apl_finished_inst.clear();

    rs_ld->apply_next_tick();
    rs_sta->apply_next_tick();
    rs_std->apply_next_tick();
    rs_amo->apply_next_tick();
    rs_fence->apply_next_tick();

    lsu->apply_next_tick();
}

void XiangShanCPU::_cur_forward_pipeline() {

    ifu_on_current_tick();

    decdisp_on_current_tick();
    cur_commit();

    cur_int_disp2();
    cur_fp_disp2();
    cur_mem_disp2();

    cur_exu_lsu();
}



void XiangShanCPU::_do_csr_inst(XSInst *inst) {
    if(inst->param.csr.index == RVCSR_FFLAGS || inst->param.csr.index == RVCSR_FRM || inst->param.csr.index == RVCSR_FCSR) {
        uint64_t mask = 0, offset = 0;
        switch (inst->param.csr.index)
        {
        case RVCSR_FFLAGS: mask = RV_FFLASG_MASK; break;
        case RVCSR_FRM: mask = RV_FRM_MASK; offset = 5; break;
        case RVCSR_FCSR: mask = (RV_FFLASG_MASK | RV_FRM_MASK); break;
        default: simroot_assert(0);
        }
        switch (inst->param.csr.op)
        {
        case isa::RV64CSROP3::CSRRW:
            if(inst->prd) {
                ireg.regfile[inst->prd] = ((csrs.fcsr & mask) >> offset);
                csrs.fcsr = ((ireg.regfile[inst->prs[0]] << offset) & mask);
            }
            break;
        case isa::RV64CSROP3::CSRRS:
            if(inst->prd) {
                ireg.regfile[inst->prd] = ((csrs.fcsr & mask) >> offset);
                csrs.fcsr |= ((ireg.regfile[inst->prs[0]] << offset) & mask);
            }
            break;
        case isa::RV64CSROP3::CSRRC:
            if(inst->prd) {
                ireg.regfile[inst->prd] = ((csrs.fcsr & mask) >> offset);
                csrs.fcsr &= (~((ireg.regfile[inst->prs[0]] << offset) & mask));
            }
            break;
        default:
            sprintf(log_buf, "%lx: Unknown CSR op @0x%lx: %s\n", simroot::get_current_tick(), inst->pc, inst->dbgname.c_str());
            LOG(ERROR) << log_buf;
            exit(0);
        }
    }
    else if(inst->param.csr.index == RVCSR_FRM) {
        switch (inst->param.csr.op)
        {
        case isa::RV64CSROP3::CSRRW:
            if(inst->prd) {
                ireg.regfile[inst->prd] = (csrs.fcsr & RV_FRM_MASK);
                csrs.fcsr = (ireg.regfile[inst->prs[0]] & RV_FRM_MASK);
            }
            break;
        case isa::RV64CSROP3::CSRRS:
            if(inst->prd) {
                ireg.regfile[inst->prd] = (csrs.fcsr & RV_FRM_MASK);
                csrs.fcsr |= (ireg.regfile[inst->prs[0]] & RV_FRM_MASK);
            }
            break;
        case isa::RV64CSROP3::CSRRC:
            if(inst->prd) {
                ireg.regfile[inst->prd] = (csrs.fcsr & RV_FRM_MASK);
                csrs.fcsr &= (~(ireg.regfile[inst->prs[0]] & RV_FRM_MASK));
            }
            break;
        default:
            sprintf(log_buf, "%lx: Unknown CSR op @0x%lx: %s\n", simroot::get_current_tick(), inst->pc, inst->dbgname.c_str());
            LOG(ERROR) << log_buf;
            exit(0);
        }
    }
    else {
        sprintf(log_buf, "%lx: Unknown CSR index @0x%lx: %d\n", simroot::get_current_tick(), inst->pc, (int)(inst->param.csr.index));
        LOG(ERROR) << log_buf;
        exit(0);
    }
}

void XiangShanCPU::_do_system_inst(XSInst *inst) {
    if(inst->flag & (RVINSTFLAG_ECALL | RVINSTFLAG_EBREAK)) {
        RVRegArray regs;
        regs[0] = 0;
        for(int i = 1; i < RV_REG_CNT_INT; i++) {
            regs[i] = ireg.regfile[irnm.commited_table[i]];
        }
        for(int i = 0; i < RV_REG_CNT_FP; i++) {
            regs[RV_REG_CNT_INT + i] = freg.regfile[frnm.commited_table[i]];
        }
        VirtAddrT nextpc = inst->pc + ((inst->flag & RVINSTFLAG_RVC)?2:4);
        inst->arg1 = ((inst->flag & RVINSTFLAG_ECALL)?(io_sys_port->syscall(cpu_id, inst->pc, regs)):(io_sys_port->ebreak(cpu_id, inst->pc, regs)));
        if(inst->arg1 != nextpc) {
            control.sys_redirect.valid = true;
            control.sys_redirect.data = inst;
        }
        for(int i = 1; i < RV_REG_CNT_INT; i++) {
            ireg.regfile[irnm.commited_table[i]] = regs[i];
        }
        for(int i = 0; i < RV_REG_CNT_FP; i++) {
            freg.regfile[frnm.commited_table[i]] = regs[RV_REG_CNT_INT + i];
        }
    }
    // CSR
    else _do_csr_inst(inst);
}


void XiangShanCPU::_do_op_inst(XSInst *inst) {
    auto opcode = inst->opcode;
    RawDataT s1 = inst->arg0;
    RawDataT s2 = inst->arg1;
    RawDataT s3 = inst->arg2;
    RawDataT &arg0_data = inst->arg0;
    RawDataT &arg1_addr = inst->arg1;

    switch (opcode)
    {
    case RV64OPCode::auipc:
        RAW_DATA_AS(arg0_data).i64 = inst->pc + RAW_DATA_AS(inst->imm).i64;
        break;
    case RV64OPCode::lui:
        arg0_data = inst->imm;
        break;
    case RV64OPCode::jal:
        arg1_addr = inst->pc + RAW_DATA_AS(inst->imm).i64;
        arg0_data = inst->pc + ((inst->flag & RVINSTFLAG_RVC)?2:4);
        break;
    case RV64OPCode::jalr:
        arg1_addr = s1 + RAW_DATA_AS(inst->imm).i64;
        arg0_data = inst->pc + ((inst->flag & RVINSTFLAG_RVC)?2:4);
        break;
    case RV64OPCode::branch:
        arg1_addr = inst->pc + RAW_DATA_AS(inst->imm).i64;
        inst->err = isa::perform_branch_op_64(inst->param.branch, &arg0_data, s1, s2);
        break;
    case RV64OPCode::opimm:
        inst->err = isa::perform_int_op_64(inst->param.intop, &arg0_data, s1, inst->imm);
        break;
    case RV64OPCode::opimm32:
        inst->err = isa::perform_int_op_32(inst->param.intop, &arg0_data, s1, inst->imm);
        break;
    case RV64OPCode::op:
        inst->err = isa::perform_int_op_64(inst->param.intop, &arg0_data, s1, s2);
        break;
    case RV64OPCode::op32:
        inst->err = isa::perform_int_op_32(inst->param.intop, &arg0_data, s1, s2);
        break;
    case RV64OPCode::madd: case RV64OPCode::msub: case RV64OPCode::nmsub: case RV64OPCode::nmadd:
        inst->err = isa::perform_fmadd_op(inst->opcode, inst->param.fp.fwid, &arg0_data, s1, s2, s3, &arg1_addr);
        break;
    case RV64OPCode::opfp:
        inst->err = isa::perform_fp_op(inst->param.fp, &arg0_data, s1, s2, &arg1_addr);
        break;
    default:
        LOG(ERROR) << "Unknown OPCODE in Generic EXU: " << (int)opcode;
        simroot_assert(0);
    }
}

uint32_t XiangShanCPU::_get_exu_latency(XSInst *inst) {
    auto opcode = inst->opcode;
    switch (opcode)
    {
    case RV64OPCode::auipc:
    case RV64OPCode::lui:
    case RV64OPCode::jal:
    case RV64OPCode::jalr:
    case RV64OPCode::branch:
        return 1;
    case RV64OPCode::opimm:
    case RV64OPCode::op:
        switch (inst->param.intop)
        {
        case isa::RV64IntOP73::MUL :
        case isa::RV64IntOP73::MULH :
        case isa::RV64IntOP73::MULHSU :
        case isa::RV64IntOP73::MULHU :
            return 3;
        case isa::RV64IntOP73::DIV :
        case isa::RV64IntOP73::DIVU :
        case isa::RV64IntOP73::REM :
        case isa::RV64IntOP73::REMU :
            return 18;
        default:
            return 1;
        }
    case RV64OPCode::opimm32:
    case RV64OPCode::op32:
        switch (inst->param.intop)
        {
        case isa::RV64IntOP73::MUL :
        case isa::RV64IntOP73::MULH :
        case isa::RV64IntOP73::MULHSU :
        case isa::RV64IntOP73::MULHU :
            return 3;
        case isa::RV64IntOP73::DIV :
        case isa::RV64IntOP73::DIVU :
        case isa::RV64IntOP73::REM :
        case isa::RV64IntOP73::REMU :
            return 10;
        default:
            return 1;
        }
    case RV64OPCode::madd: case RV64OPCode::msub: case RV64OPCode::nmsub: case RV64OPCode::nmadd:
        return 5;
    case RV64OPCode::opfp:
        switch (inst->param.fp.op)
        {
        case isa::RV64FPOP5::DIV :
            switch (inst->param.fp.fwid)
            {
            case isa::RV64FPWidth2::fharf : return 6;
            case isa::RV64FPWidth2::fword : return 10;
            case isa::RV64FPWidth2::fdword : return 18;
            case isa::RV64FPWidth2::fqword : return 34;
            default: simroot_assert(0);
            }
        case isa::RV64FPOP5::SQRT :
            switch (inst->param.fp.fwid)
            {
            case isa::RV64FPWidth2::fharf : return 10;
            case isa::RV64FPWidth2::fword : return 18;
            case isa::RV64FPWidth2::fdword : return 34;
            case isa::RV64FPWidth2::fqword : return 66;
            default: simroot_assert(0);
            }
        default:
            return 3;
        }
    default:
        LOG(ERROR) << "Unknown OPCODE in Generic EXU: " << (int)opcode;
        simroot_assert(0);
    }
}





}}

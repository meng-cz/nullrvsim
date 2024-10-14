

#include "simroot.h"

#include "cpu/operation.h"

#include "xslsu.h"

namespace simcpu {

namespace xs {

void LSU::dump_core(std::ofstream &ofile) {
    #define LOGTOFILE(fmt, ...) do{sprintf(log_buf, fmt, ##__VA_ARGS__); ofile << log_buf;}while(0)
    {
        LOGTOFILE("#LSU-LD-INDEXING: ");
        for(auto l : ld_indexing) LOGTOFILE("0x%lx, ", l);
        LOGTOFILE("\n");
        LOGTOFILE("#LSU-LD-WAITING: ");
        for(auto l : ld_waiting.get()) LOGTOFILE("0x%lx, ", l);
        LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#LSU-LDQ: %ld items\n", ldq_total_size);
        int i = 0;
        for(auto &e : load_queue.get()) {
            LDQEntry* l = e.second;
            LOGTOFILE("%d:0x%lx,%ld,%s,v", i, l->inst->pc, l->inst->id, l->inst->dbgname.c_str());
            for(uint64_t j = l->valid.size(); j > 0; j--) {
                if(l->valid[j-1]) LOGTOFILE("1");
                else LOGTOFILE("0");
            }
            LOGTOFILE(",d");
            for(uint64_t j = l->data.size(); j > 0; j--) {
                LOGTOFILE("%02x", l->data[j-1]);
            }
            LOGTOFILE(" | ");
            i++;
            if(i % 2 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 2 != 0) {
            LOGTOFILE("\n");
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#LSU-LDQ-WB: %ld items\n", wait_writeback_load.size());
        int i = 0;
        for(auto &e : wait_writeback_load) {
            LOGTOFILE("%d:0x%lx,%ld,%s | ", i, e->pc, e->id, e->dbgname.c_str());
            i++;
            if(i % 2 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 2 != 0) {
            LOGTOFILE("\n");
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#LSU-LDQ-FINISHED: %ld items\n", finished_load.size());
        int i = 0;
        for(auto &e : finished_load) {
            XSInst *p = e.second;
            LOGTOFILE("%d:0x%lx,%ld,%s | ", i, p->pc, p->id, p->dbgname.c_str());
            i++;
            if(i % 2 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 2 != 0) {
            LOGTOFILE("\n");
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#LSU-STQ: %ld items\n", store_queue.size());
        int i = 0;
        for(auto &e : store_queue.get()) {
            LOGTOFILE("%d:0x%lx,%ld,%s | ", i, e->pc, e->id, e->dbgname.c_str());
            i++;
            if(i % 2 == 0) {
                LOGTOFILE("\n");
            }
        }
        if(i % 2 != 0) {
            LOGTOFILE("\n");
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#LSU-COMMITED-STB: %d items\n", commited_store_buf_indexing_cnt);
        for(auto &e : commited_store_buf) {
            LOGTOFILE("0x%lx, id %ld: ", e.first, e.second.last_inst_id);
            for(int i = CACHE_LINE_LEN_BYTE - 1; i >= 0; i--) {
                if(e.second.valid[i]) LOGTOFILE("%02x", e.second.linebuf[i]);
                else LOGTOFILE("XX");
            }
            LOGTOFILE("\n");
        }
    }
    LOGTOFILE("\n");
    #undef LOGTOFILE
}

LSU::LSU(XiangShanParam *param, uint32_t cpu_id, CPUSystemInterface *io_sys, CacheInterfaceV2 *io_dcache, LSUPort *port)
: param(param), cpu_id(cpu_id), io_sys_port(io_sys), io_dcache_port(io_dcache), port(port)
{
    ld_addr_trans_queue = make_unique<SimpleTickQueue<XSInst*>>(2, 2, 0);
    st_addr_trans_queue = make_unique<SimpleTickQueue<XSInst*>>(2, 2, 0);
}

void LSU::on_current_tick() {
    if(port->fence->cur_size()) {
        clear_commited_store_buf = true;
        if(pipeline_empty()) {
            XSInst *inst = port->fence->front();
            port->fence->pop_front();
            inst->finished = true;
            if(debug_ofile) {
                sprintf(log_buf, "%ld:FENCE @0x%lx, %ld", simroot::get_current_tick(), inst->pc, inst->id);
                simroot::log_line(debug_ofile, log_buf);
            }
            return;
            clear_commited_store_buf = false;
        }
    }

    if(port->amo->cur_size() && !(port->amo->front()->finished)) {
        auto inst = port->amo->front();
        bool addr_ready = (inst->rsready[0]);
        bool data_ready = (!(inst->flag & RVINSTFLAG_S2INT) || inst->rsready[1]);
        if(addr_ready && data_ready) {
            inst->finished = true;
            amo_state = AMOState::tlb;
            if(debug_ofile) {
                sprintf(log_buf, "%ld:AMO @0x%lx, %ld", simroot::get_current_tick(), inst->pc, inst->id);
                simroot::log_line(debug_ofile, log_buf);
            }
        }
    }

    _cur_get_ld_from_rs();
    _cur_ld_addr_trans();
    _cur_load_queue();
    _cur_get_sta_from_rs();
    _cur_get_std_from_rs();
    _cur_st_addr_trans();
}

void LSU::always_apply_next_tick() {
    if(apl_commited_store_buf.empty()) return;
    for(auto &entry : apl_commited_store_buf) {
        LineIndexT lindex = entry.first;
        auto &stb = entry.second;
        auto res = commited_store_buf.find(lindex);
        if(res == commited_store_buf.end()) {
            commited_store_buf.emplace(lindex, stb);
        }
        else {
            auto &stb2 = res->second;
            for(int i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
                if(stb.valid[i]) {
                    stb2.valid[i] = true;
                    stb2.linebuf[i] = stb.linebuf[i];
                }
            }
            stb2.last_inst_id = stb.last_inst_id;
        }
        commited_store_buf_lru.remove(lindex);
        commited_store_buf_lru.push_back(lindex);
    }
    apl_commited_store_buf.clear();
}

void LSU::apply_next_tick() {
    ld_addr_trans_queue->apply_next_tick();
    st_addr_trans_queue->apply_next_tick();

    for(auto &entry : apl_st_bypass) {
        auto res = st_bypass.find(entry.first);
        if(res == st_bypass.end()) {
            res = st_bypass.emplace(entry.first, list<STByPass>()).first;
        }
        auto pos = res->second.rbegin();
        while(pos != res->second.rend()) {
            if(inst_later_than(entry.second.inst_id, pos->inst_id)) break;
            pos++;
        }
        res->second.insert(pos.base(), entry.second);
    }
    apl_st_bypass.clear();

    ld_waiting.apply_next_tick();
    load_queue.apply_next_tick();
    store_queue.apply_next_tick();

    for(auto inst : apl_st_addr_ready) {
        inst->rsready[2] = true;
    }
    apl_st_addr_ready.clear();

    for(auto inst : apl_ll_reorder_check) {
        _ld_reorder_check(inst->id, inst->arg2, isa::rv64_ls_width_to_length(inst->param.loadstore), SimError::llreorder);
    }
    for(auto inst : apl_sl_reorder_check) {
        _ld_reorder_check(inst->id, inst->arg2, isa::rv64_ls_width_to_length(inst->param.loadstore), SimError::slreorder);
    }
    apl_ll_reorder_check.clear();
    apl_sl_reorder_check.clear();

    for(auto inst : apl_ld_finish) {
        LineIndexT lindex = (inst->arg2 >> CACHE_LINE_ADDR_OFFSET);
        wait_writeback_load.emplace_back(inst);
        finished_load.emplace(lindex, inst);
    }
    apl_ld_finish.clear();

    // LSU每周期可以提交4条指令到ROB，一般不会占满，所以不考虑等待了
    for(auto inst : apl_inst_finished) {
        inst->finished = true;
    }
    apl_inst_finished.clear();

}

void LSU::apl_clear_pipeline() {
    amo_state = AMOState::free;
    ld_addr_trans_queue->clear();
    st_addr_trans_queue->clear();
    ld_indexing.clear();
    ld_waiting.clear();
    st_bypass.clear();
    apl_st_bypass.clear();
    ld_refire_queue.clear();
    ldq_total_size = 0;
    list<std::pair<LineIndexT, LDQEntry*>> tofree_ldq;
    load_queue.clear(&tofree_ldq);
    for(auto &entry : tofree_ldq) {
        delete entry.second;
    }
    apl_ld_finish.clear();
    wait_writeback_load.clear();
    finished_load.clear();
    store_queue.clear();
    apl_st_addr_ready.clear();
    apl_inst_finished.clear();
    apl_ll_reorder_check.clear();
    apl_sl_reorder_check.clear();
    std::list<CacheOP*> tofree;
    io_dcache_port->clear_ld(&tofree);
    io_dcache_port->clear_amo(&tofree);
    io_dcache_port->clear_misc(&tofree);
    for(auto p : tofree) {
        delete p;
    }
}


/**
 * 尝试提交AMO指令，表示该AMO指令位于ROB头部，如果AMO指令还没启动则启动AMO状态机，否则返回AMO指令的完成状态
*/
bool LSU::cur_commit_amo(XSInst *inst) {
    simroot_assert(port->amo->size());
    simroot_assert(inst == port->amo->front());
    bool amo_finished = false;
    if(amo_state == AMOState::tlb) {
        VirtAddrT vaddr = inst->arg1;
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            LOG(ERROR) << "Cannot perform AMO on device memory";
            simroot_assert(0);
        }
        else {
            PhysAddrT paddr = 0;
            if((inst->err = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_R)) != SimError::success) {
                amo_finished = true;
            }
            else {
                inst->arg2 = paddr;
            }
        }
        amo_state = AMOState::pm;
    }
    else if(amo_state == AMOState::pm) {
        _ld_reorder_check(inst->id, inst->arg2, isa::rv64_ls_width_to_length(inst->param.amo.wid), SimError::slreorder);
        amo_state = AMOState::flush_sbuffer_req;
    }
    else if(amo_state == AMOState::flush_sbuffer_req) {
        clear_commited_store_buf = true;
        amo_state = AMOState::flush_sbuffer_resp;
    }
    else if(amo_state == AMOState::flush_sbuffer_resp) {
        if(commited_store_buf.empty() && apl_commited_store_buf.empty() && commited_store_buf_indexing_cnt == 0) {
            amo_state = AMOState::cache_req;
            clear_commited_store_buf = false;
        }
    }
    else if(amo_state == AMOState::cache_req) {
        CacheOP *cop = new CacheOP();
        cop->opcode = CacheOPCode::amo;
        cop->addr = inst->arg2;
        cop->len = isa::rv64_ls_width_to_length(inst->param.amo.wid);
        cop->data.resize(cop->len);
        memcpy(cop->data.data(), &(inst->arg0), cop->len);
        cop->amo = inst->param.amo.op;
        cop->param = inst;
        simroot_assert(io_dcache_port->amo_input->push(cop));
        amo_state = AMOState::cache_resp;
    }
    else if(amo_state == AMOState::cache_resp && io_dcache_port->amo_output->can_pop()) {
        CacheOP *cop = io_dcache_port->amo_output->top();
        io_dcache_port->amo_output->pop();
        SimError res = cop->err;
        if(res == SimError::success) {
            amo_state = AMOState::finish;
            if(inst->param.amo.op == RV64AMOOP5::SC) {
                inst->arg0 = 0;
            }
            else if(cop->len == 4) {
                RAW_DATA_AS(inst->arg0).i64 = *((int32_t*)cop->data.data());
            }
            else {
                inst->arg0 = *((int64_t*)cop->data.data());
            }
            if((inst->flag & RVINSTFLAG_RDINT) && (inst->vrd)) {
                simroot_assert(port->apl_int_bypass->emplace(inst->prd, inst->arg0).second);
            }
        }
        else if(res == SimError::unconditional) {
            inst->err = SimError::success;
            amo_state = AMOState::finish;
            inst->arg0 = 1;
            if((inst->flag & RVINSTFLAG_RDINT) && (inst->vrd)) {
                simroot_assert(port->apl_int_bypass->emplace(inst->prd, inst->arg0).second);
            }
        }
        else if(res == SimError::busy || res == SimError::coherence || res == SimError::miss) {
            amo_state = AMOState::cache_req;
        }
        else {
            inst->err = res;
            amo_finished = true;
        }
        delete cop;
    }
    else if(amo_state == AMOState::finish) {
        inst->err = SimError::success;
        amo_finished = true;
    }

    if(amo_finished) {
        amo_state = AMOState::free;
        port->amo->pop_front();
    }
    return amo_finished;
}


/**
 * 从LD保留站中获取一个就绪的指令发射到ld_addr_trans_queue
*/
void LSU::_cur_get_ld_from_rs() {
    auto &rs = port->ld->get();
    while(ld_addr_trans_queue->can_push() && !ld_refire_queue.empty()) {
        simroot_assert(ld_addr_trans_queue->push(ld_refire_queue.front()));
        ld_refire_queue.pop_front();
    }
    while(ld_addr_trans_queue->can_push() && !rs.empty()) {
        auto iter = rs.begin();
        for( ; iter != rs.end(); iter++) {
            if((*iter)->rsready[0]) break;
        }
        if(iter == rs.end()) break;
        simroot_assert(ld_addr_trans_queue->push(*iter));
        iter = rs.erase(iter);
    }
}

inline RawDataT _signed_extension(uint8_t *buf, isa::RV64LSWidth lswid) {
    RawDataT ret = 0;
    switch (lswid)
    {
    case isa::RV64LSWidth::byte : RAW_DATA_AS(ret).i64 = *((int8_t*)(buf)); break;
    case isa::RV64LSWidth::harf : RAW_DATA_AS(ret).i64 = *((int16_t*)(buf)); break;
    case isa::RV64LSWidth::word : RAW_DATA_AS(ret).i64 = *((int32_t*)(buf)); break;
    case isa::RV64LSWidth::dword : RAW_DATA_AS(ret).i64 = *((int64_t*)(buf)); break;
    case isa::RV64LSWidth::ubyte: ret = *((uint8_t*)(buf)); break;
    case isa::RV64LSWidth::uharf: ret = *((uint16_t*)(buf)); break;
    case isa::RV64LSWidth::uword: ret = *((uint32_t*)(buf)); break;
    }
    return ret;
}

/**
 * 从ld_addr_trans_queue取出一个指令并进行地址翻译，翻译成功则同时进入L1D-LDInput与load_queue，否则设置错误后从output输出。
 * 该阶段完成load-load违例检查，检查finished_load中是否存在比自己晚的同地址的指令，存在则将查到的指令标为llreorder错误。
*/
void LSU::_cur_ld_addr_trans() {
    while(ld_addr_trans_queue->can_pop() &&
        ldq_total_size < param->load_queue_size &&
        io_dcache_port->ld_input->can_push()
    ) {
        XSInst* inst = ld_addr_trans_queue->top();
        ld_addr_trans_queue->pop();
        VirtAddrT vaddr = inst->arg1 + RAW_DATA_AS(inst->imm).i64;
        uint32_t len = isa::rv64_ls_width_to_length(inst->param.loadstore);
        bool succ = false;
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            inst->arg2 = vaddr;
            uint64_t buf = 0;
            io_sys_port->dev_input(cpu_id, vaddr, len, &(buf));
            vector<bool> valid;
            valid.resize(len);
            _do_store_bypass(inst->id, vaddr >> CACHE_LINE_ADDR_OFFSET, vaddr & ((1<<CACHE_LINE_ADDR_OFFSET)-1), len, (uint8_t*)(&buf), &valid);
            inst->arg0 = _signed_extension((uint8_t*)(&buf), inst->param.loadstore);
            inst->err = SimError::success;

            // load-load违例检查
            apl_ll_reorder_check.push_back(inst);

            apl_ld_finish.push_back(inst);
            ldq_total_size++;
        }
        else {
            PhysAddrT paddr = 0;
            SimError res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_R);
            if(res == SimError::success) {
                inst->arg2 = paddr;
                LineIndexT lindex = (paddr >> CACHE_LINE_ADDR_OFFSET);

                LDQEntry *tmp = new LDQEntry();
                tmp->inst = inst;
                tmp->offset = (paddr & ((1<<CACHE_LINE_ADDR_OFFSET) - 1));
                tmp->len = len;
                tmp->data.resize(len);
                tmp->valid.assign(len, false);
                tmp->fired = true;
                load_queue.push_next_tick(lindex, tmp);
                ld_waiting.push_next_tick(lindex);

                ldq_total_size++;
            }
            else {
                inst->err = res;
                apl_inst_finished.push_back(inst);
                continue;
            }
            // load-load违例检查
            apl_ll_reorder_check.push_back(inst);
        }

        if(debug_ofile) {
            sprintf(log_buf, "%ld:LD-ADDR: @0x%lx, %ld, %s, 0x%lx->0x%lx", 
                simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(), vaddr, inst->arg2
            );
            simroot::log_line(debug_ofile, log_buf);
        }
    }
}

void LSU::_do_store_bypass(XSInstID inst_id, LineIndexT lindex, uint32_t offset, uint32_t len, uint8_t *buf, vector<bool> *setvalid) { 
    auto res = st_bypass.find(lindex);
    auto res2 = commited_st_bypass.find(lindex);
    if(res == st_bypass.end() && res2 == commited_st_bypass.end()) return;
    uint64_t _dbg_previous = 0;
    if(debug_ofile) {
        memcpy(&_dbg_previous, buf, len);
    }
    if(res2 != commited_st_bypass.end()) {
        list<STByPass> &bps = res2->second;
        for(auto &bp : bps) {
            if(inst_later_than(bp.inst_id, inst_id)) break;
            int32_t dst_off = offset, dst_len = len, src_off = bp.offset, src_len = bp.len;
            if(dst_off + dst_len <= src_off || src_off + src_len <= dst_off) continue;
            for(int i = 0; i < dst_len; i++) {
                int32_t idx = i + dst_off - src_off;
                if(idx >= 0 && idx < src_len) {
                    buf[i] = bp.data[idx];
                    (*setvalid)[i] = true;
                }
            }
        }
    }
    if(res != st_bypass.end()) {
        list<STByPass> &bps = res->second;
        for(auto &bp : bps) {
            if(inst_later_than(bp.inst_id, inst_id)) break;
            int32_t dst_off = offset, dst_len = len, src_off = bp.offset, src_len = bp.len;
            if(dst_off + dst_len <= src_off || src_off + src_len <= dst_off) continue;
            for(int i = 0; i < dst_len; i++) {
                int32_t idx = i + dst_off - src_off;
                if(idx >= 0 && idx < src_len) {
                    buf[i] = bp.data[idx];
                    (*setvalid)[i] = true;
                }
            }
        }
    }
    if(debug_ofile) {
        switch (len)
        {
            case 1: sprintf(log_buf, "%ld:BYPASS: 0x%lx, 0x%02x->0x%02x",
                simroot::get_current_tick(), (lindex<<CACHE_LINE_ADDR_OFFSET)+offset, 
                *((uint8_t*)(&_dbg_previous)), *((uint8_t*)buf)
                ); break;
            case 2: sprintf(log_buf, "%ld:BYPASS: 0x%lx, 0x%04x->0x%04x",
                simroot::get_current_tick(), (lindex<<CACHE_LINE_ADDR_OFFSET)+offset, 
                *((uint16_t*)(&_dbg_previous)), *((uint16_t*)buf)
                ); break;
            case 4: sprintf(log_buf, "%ld:BYPASS: 0x%lx, 0x%08x->0x%08x",
                simroot::get_current_tick(), (lindex<<CACHE_LINE_ADDR_OFFSET)+offset, 
                *((uint32_t*)(&_dbg_previous)), *((uint32_t*)buf)
                ); break;
            case 8: sprintf(log_buf, "%ld:BYPASS: 0x%lx, 0x%016lx->0x%016lx",
                simroot::get_current_tick(), (lindex<<CACHE_LINE_ADDR_OFFSET)+offset, 
                *((uint64_t*)(&_dbg_previous)), *((uint64_t*)buf)
                ); break;
            default: sprintf(log_buf, "%ld:BYPASS: Unknown length", simroot::get_current_tick());
        }
        simroot::log_line(debug_ofile, log_buf);
    }
}

/**
 * 获取cache当前周期新到达的cacheline，回填到LDQ中
 * 获取L1D-LDOutput的结果和STByPass查询结果，回填到LDQ中
 * 检查load_queue中是否有已完成的load
*/
void LSU::_cur_load_queue() {
    
    auto &ldq = load_queue.get();

    auto &arrivals = io_dcache_port->arrivals;
    vector<XSInst*> wakeup_insts;
    for(auto &l : arrivals) {
        LineIndexT lindex = l.lindex;
        uint8_t* linebuf = l.data.data();
        uint64_t cnt = ldq.count(lindex);
        if(cnt == 0) continue;
        auto res = ldq.find(lindex);
        auto iter = res;
        for(uint64_t i = 0; i < cnt; i++) {
            auto &e = *(iter->second);
            for(int i = 0; i < e.len; i++) {
                if(!e.valid[i]) e.data[i] = linebuf[i + e.offset];
            }
            e.valid.assign(e.len, true);
            iter++;
        }
        for(uint64_t i = 0; i < cnt; i++) {
            auto &e = *(res->second);
            if(e.fired) {
                res++;
            }
            else {
                e.inst->arg0 = _signed_extension(e.data.data(), e.inst->param.loadstore);
                e.inst->err = SimError::success;
                apl_ld_finish.emplace_back(e.inst);
                wakeup_insts.emplace_back(e.inst);
                delete res->second;
                res = ldq.erase(res);
            }
        }
    }

    if(debug_ofile && !arrivals.empty()) {
        sprintf(log_buf, "%ld:LD-NEWLINE: ", simroot::get_current_tick());
        string str(log_buf);
        for(auto &l : arrivals) {
            sprintf(log_buf, "0x%lx ", l.lindex);
            str += log_buf;
        }
        str += ", WAKEUP: ";
        for(auto &p : wakeup_insts) {
            sprintf(log_buf, "@0x%lx,%ld ", p->pc, p->id);
            str += log_buf;
        }
        simroot::log_line(debug_ofile, str);
    }

    while(io_dcache_port->ld_output->can_pop()) {
        CacheOP *cop = io_dcache_port->ld_output->top();
        io_dcache_port->ld_output->pop();
        
        LineIndexT lindex = (cop->addr >> CACHE_LINE_ADDR_OFFSET);
        uint8_t *linebuf = cop->data.data();
        bool has_error = (cop->err == SimError::invalidaddr || cop->err == SimError::unaligned || cop->err == SimError::unaccessable);
        uint64_t cnt = ldq.count(lindex);
        if(cnt == 0) {
            delete cop;
            continue;
        }
        auto res = ldq.find(lindex);
        for(int i = 0; i < cnt; i++) {
            auto &e = *(res->second);
            e.fired = false;
            if(cop->err == SimError::success) {
                for(int i = 0; i < e.len; i++) {
                    e.data[i] = linebuf[i + e.offset];
                    e.valid[i] = true;
                }
            }
            _do_store_bypass(e.inst->id, lindex, e.offset, e.len, e.data.data(), &(e.valid));
            if(has_error || std::count(e.valid.begin(), e.valid.end(), true) == e.len) {
                e.inst->err = (has_error?(cop->err):(SimError::success));
                e.inst->arg0 = _signed_extension(e.data.data(), e.inst->param.loadstore);
                apl_ld_finish.emplace_back(e.inst);
                delete res->second;
                res = ldq.erase(res);
            }
            else {
                res++;
            }
        }

        if(ldq.count(lindex) != 0) {
            if(cop->err == SimError::busy || cop->err == SimError::coherence) {
                // dcache暂时没法响应这个请求，扔回队尾等重发
                ld_waiting.get().push_front(lindex);
            }
        }
        ld_indexing.erase(lindex);

        delete cop;
    }

    while(!ld_waiting.get().empty() && io_dcache_port->ld_input->can_push()) {
        LineIndexT lindex = ld_waiting.get().back();
        CacheOP *cop = new CacheOP();
        cop->opcode = CacheOPCode::load;
        cop->addr = (lindex << CACHE_LINE_ADDR_OFFSET);
        cop->data.resize(CACHE_LINE_LEN_BYTE);
        cop->len = CACHE_LINE_LEN_BYTE;
        io_dcache_port->ld_input->push(cop);
        ld_indexing.insert(lindex);
        ld_waiting.get().remove(lindex);
        if(debug_ofile) {
            sprintf(log_buf, "%ld:INDEXING: LINE 0x%lx", simroot::get_current_tick(), lindex);
            simroot::log_line(debug_ofile, log_buf);
        }
    }

    // LSU只有两个写回端口，每周期只能写回两条指令到旁路网络
    uint32_t int_wb = 0, fp_wb = 0;
    const uint32_t lsu_int_wb_width = 2, lsu_fp_wb_width = 2;
    auto iter = wait_writeback_load.begin();
    for(;iter != wait_writeback_load.end() && (int_wb < lsu_int_wb_width || fp_wb < lsu_fp_wb_width);) {
        auto inst = *iter;
        bool rdint = ((inst->flag & RVINSTFLAG_RDINT) && (inst->vrd));
        bool rdfp = (inst->flag & RVINSTFLAG_RDFP);
        bool wbbusy = ((rdint && (int_wb >= lsu_int_wb_width)) || (rdfp && (fp_wb >= lsu_fp_wb_width)));

        if(wbbusy) {
            iter ++;
            continue;
        }
        iter = wait_writeback_load.erase(iter);

        apl_inst_finished.push_back(inst);
        if(rdint) {
            simroot_assert(port->apl_int_bypass->emplace(inst->prd, inst->arg0).second);
            int_wb++;
        }
        else if(rdfp) {
            simroot_assert(port->apl_fp_bypass->emplace(inst->prd, inst->arg0).second);
            fp_wb++;
        }

        if(debug_ofile) {
            sprintf(log_buf, "%ld:LD-WB: @0x%lx, %ld, %s, A:0x%lx->0x%lx D:0x%lx R:%d->%d",
                simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(),
                RAW_DATA_AS(inst->arg1).i64 + RAW_DATA_AS(inst->imm).i64, inst->arg2, inst->arg0, inst->vrd, inst->prd
            );
            simroot::log_line(debug_ofile, log_buf);
        }
    }
}

void LSU::_ld_reorder_check(XSInstID inst_id, PhysAddrT addr, uint32_t len, SimError errcode) {
    LineIndexT lindex = (addr >> CACHE_LINE_ADDR_OFFSET);
    uint32_t cnt = finished_load.count(lindex);
    if(cnt != 0) {
        auto iter = finished_load.find(lindex);
        for(int i = 0; i < cnt; i++, iter++) {
            auto &p2 = iter->second;
            if(!inst_later_than(p2->id, inst_id)) [[likely]] continue;
            uint32_t len2 = isa::rv64_ls_width_to_length(p2->param.loadstore);
            if(addr + len <= p2->arg2 || p2->arg2 + len2 <= addr) continue;
            p2->err = errcode;
        }
    }
    {
        auto iter = apl_ld_finish.begin();
        while(iter != apl_ld_finish.end()) {
            auto &p2 = *iter;
            if(lindex != (p2->arg2 >> CACHE_LINE_ADDR_OFFSET)) [[likely]] {iter++; continue;}
            if(!inst_later_than(p2->id, inst_id)) [[likely]] {iter++; continue;}
            uint32_t len2 = isa::rv64_ls_width_to_length(p2->param.loadstore);
            if(addr + len <= p2->arg2 || p2->arg2 + len2 <= addr) {iter++; continue;}
            // 结果还没有进入流水线，不需要设置err，扔回RS重发
            p2->rsready[2] = false;
            ld_refire_queue.push_back(p2);
            iter = apl_ld_finish.erase(iter);
            simroot_assert(ldq_total_size);
            ldq_total_size--;
        }
    }
    
}

/**
 * 从STA保留站中获取一个指令操作数就绪的指令发射到st_addr_trans_queue
*/
void LSU::_cur_get_sta_from_rs() {
    auto &rs = port->sta->get();
    while(st_addr_trans_queue->can_push() && !rs.empty()) {
        auto iter = rs.begin();
        for( ; iter != rs.end(); iter++) {
            if((*iter)->rsready[0]) break;
        }
        if(iter == rs.end()) break;
        simroot_assert(st_addr_trans_queue->push(*iter));
        iter = rs.erase(iter);
    }
}

/**
 * 从STD保留站中获取一个双操作数均就绪的指令发射到store_queue与STByPass
 * 该阶段完成store-load违例检查，检查finished_load中是否存在比自己晚的同地址的指令，存在则将查到的指令标为slreorder错误。
*/
void LSU::_cur_get_std_from_rs() {
    auto &rs = port->std->get();
    for(int __n = 0; __n < 2; __n++) {
        if(rs.empty() || store_queue.size() >= param->store_queue_size) return;
        auto iter = rs.begin();
        for( ; iter != rs.end(); iter++) {
            if((*iter)->rsready[1] && (*iter)->rsready[2]) break;
        }
        if(iter == rs.end()) return;
        auto inst = *iter;
        iter = rs.erase(iter);
        store_queue.push_next_tick(inst);
        apl_inst_finished.push_back(inst);
        STByPass tmp;
        tmp.inst_id = inst->id;
        tmp.offset = (inst->arg2 & ((1 << CACHE_LINE_ADDR_OFFSET) - 1));
        tmp.len = isa::rv64_ls_width_to_length(inst->param.loadstore);
        tmp.data.resize(tmp.len);
        memcpy(tmp.data.data(), &(inst->arg0), tmp.len);
        apl_st_bypass.emplace_back(std::make_pair(inst->arg2 >> CACHE_LINE_ADDR_OFFSET, tmp));
        // store-load违例检查
        apl_sl_reorder_check.push_back(inst);
        if(debug_ofile) {
            sprintf(log_buf, "%ld:ST-READY: @0x%lx, %ld, %s", simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str());
            simroot::log_line(debug_ofile, log_buf);
        }
    }
}

/**
 * 从st_addr_trans_queue取出一个指令并进行地址翻译，翻译失败则设置错误后从output输出同时删除STD保留站对应项。
 * 翻译成功则直接丢弃等STD发射。
*/
void LSU::_cur_st_addr_trans() {
    while(st_addr_trans_queue->can_pop()) {
        XSInst* inst = st_addr_trans_queue->top();
        st_addr_trans_queue->pop();
        VirtAddrT vaddr = inst->arg1 + RAW_DATA_AS(inst->imm).i64;
        SimError res = SimError::success;
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            inst->arg2 = vaddr;
        }
        else {
            PhysAddrT paddr = 0;
            res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_R);
            inst->arg2 = paddr;
        }
        inst->err = res;
        if(res == SimError::success) {
            apl_st_addr_ready.push_back(inst);
        }
        else {
            apl_inst_finished.push_back(inst);
            port->std->get().remove(inst);
        }

        if(debug_ofile) {
            sprintf(log_buf, "%ld:ST-ADDR: @0x%lx, %ld, %s, 0x%lx->0x%lx", 
                simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(), vaddr, inst->arg2
            );
            simroot::log_line(debug_ofile, log_buf);
        }
    }
}


/**
 * 从finished_load中删除对应项
*/
void LSU::_cur_commit_load(XSInst *inst) {
    LineIndexT lindex = inst->arg2 >> CACHE_LINE_ADDR_OFFSET;
    uint32_t cnt = 0;
    if((cnt = finished_load.count(lindex)) > 0) {
        auto res = finished_load.find(lindex);
        for(int i = 0; i < cnt; i++) {
            if(res->second == inst) {
                finished_load.erase(res);
                simroot_assert(ldq_total_size > 0);
                ldq_total_size -= 1;
                return;
            }
            res ++;
        }
    }
}

/**
 * 从store_queue中找出对应项，将store内容加入commited_store_buf
*/
void LSU::_cur_commit_store(XSInst *inst) {
    if(store_queue.get().erase(inst) == 0) return;

    PhysAddrT paddr = inst->arg2;
    RawDataT data = inst->arg0;
    uint32_t len = isa::rv64_ls_width_to_length(inst->param.loadstore);
    LineIndexT lindex = (paddr >> CACHE_LINE_ADDR_OFFSET);
    
    if(debug_ofile) {
        sprintf(log_buf, "%ld:ST: @0x%lx, %ld, %s, A:0x%lx->0x%lx D:0x%lx",
            simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(),
            RAW_DATA_AS(inst->arg1).i64 + RAW_DATA_AS(inst->imm).i64, inst->arg2, inst->arg0
        );
        simroot::log_line(debug_ofile, log_buf);
    }

    auto res_bp = st_bypass.find(lindex);
    simroot_assert(res_bp != st_bypass.end());
    auto &l1 = res_bp->second;
    simroot_assert(!l1.empty() && l1.front().inst_id == inst->id);

    if(io_sys_port->is_dev_mem(cpu_id, paddr)) {
        l1.pop_front();
        if(l1.empty()) st_bypass.erase(res_bp);
        io_sys_port->dev_output(cpu_id, paddr, len, &data);
        return;
    }

    auto res_cbp = commited_st_bypass.find(lindex);
    if(res_cbp == commited_st_bypass.end()) {
        res_cbp = commited_st_bypass.emplace(lindex, list<STByPass>()).first;
    }
    res_cbp->second.emplace_back(l1.front());
    l1.pop_front();
    if(l1.empty()) st_bypass.erase(res_bp);

    auto res_cmt = apl_commited_store_buf.find(lindex);
    if(res_cmt == apl_commited_store_buf.end()) {
        res_cmt = apl_commited_store_buf.emplace(lindex, StoreBufEntry()).first;
        memset(&(res_cmt->second), 0, sizeof(res_cmt->second));
        res_cmt->second.last_inst_id = inst->id;
    }
    StoreBufEntry &stbuf = res_cmt->second;
    uint64_t offset = (paddr & ((1UL << CACHE_LINE_ADDR_OFFSET) - 1));
    for(int i = 0; i < len && offset + i < CACHE_LINE_LEN_BYTE; i++) {
        stbuf.valid[offset + i] = true;
    }
    memcpy(stbuf.linebuf + offset, &data, len);
    if(inst_later_than(inst->id, stbuf.last_inst_id)) [[likely]] {
        stbuf.last_inst_id = inst->id;
    }
}

/**
 * 从committed store buffer中选择最旧的一行写回到dcache
*/
void LSU::_cur_write_commited_store() {

    while(io_dcache_port->st_output->can_pop()) {
        CacheOP *cop = io_dcache_port->st_output->top();
        io_dcache_port->st_output->pop();
        simroot_assert(commited_store_buf_indexing_cnt > 0);
        commited_store_buf_indexing_cnt--;
        LineIndexT lindex = (cop->addr >> CACHE_LINE_ADDR_OFFSET);
        simroot_assert(cop->len == CACHE_LINE_LEN_BYTE);
        SimError res = cop->err;
        XSInstID last_inst_id = (XSInstID)(cop->param);
        if(res == SimError::busy || res == SimError::coherence || res == SimError::miss) {
            // 重新写回
            auto iter = commited_store_buf.find(lindex);
            if(iter == commited_store_buf.end()) {
                iter = commited_store_buf.emplace(lindex, StoreBufEntry()).first;
                memset(iter->second.valid, 0, sizeof(iter->second.valid));
                iter->second.last_inst_id = last_inst_id;
            }
            auto &stbuf = iter->second;
            for(int i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
                if(cop->valid[i] && !stbuf.valid[i]) {
                    stbuf.linebuf[i] = cop->data[i];
                    stbuf.valid[i] = true;
                }
            }
            commited_store_buf_lru.remove(lindex);
            if(res == SimError::miss) {
                commited_store_buf_lru.push_front(lindex);
            }
            else {
                commited_store_buf_lru.push_back(lindex);
            }
        }
        else if(res == SimError::success) {
            // 可以删除commited_st_bypass里该cacheline所有比这个指令ID早的项
            auto res_stbp = commited_st_bypass.find(lindex);
            if(res_stbp != commited_st_bypass.end()) [[likely]] {
                auto &l = res_stbp->second;
                while(!l.empty()) {
                    if(inst_later_than(l.front().inst_id, last_inst_id)) break;
                    l.pop_front();
                }
                if(l.empty()) commited_st_bypass.erase(res_stbp);
            }
            if(debug_ofile) {
                sprintf(log_buf, "%ld:ST-WB: Line @0x%lx, %ld Remain", simroot::get_current_tick(), lindex, commited_store_buf.size() + commited_store_buf_indexing_cnt);
                simroot::log_line(debug_ofile, log_buf);
            }
        }
        else [[unlikely]] {
            simroot_assert(0);
        }
        delete cop;
    }

    if(commited_store_buf.empty()) return;
    if(!clear_commited_store_buf && (commited_store_buf.size() + commited_store_buf_indexing_cnt) * 2 < param->commited_store_buffer_size) return;
    if(!io_dcache_port->st_input->can_push()) return;

    LineIndexT lindex = commited_store_buf_lru.front();
    auto iter = commited_store_buf.find(lindex);
    simroot_assert(iter != commited_store_buf.end());
    auto &stbuf = iter->second;
    CacheOP *cop = new CacheOP();
    cop->addr = (lindex << CACHE_LINE_ADDR_OFFSET);
    cop->len = CACHE_LINE_LEN_BYTE;
    cop->opcode = CacheOPCode::store;
    cop->data.resize(CACHE_LINE_LEN_BYTE);
    cache_line_copy(cop->data.data(), stbuf.linebuf);
    cop->valid.resize(CACHE_LINE_LEN_BYTE);
    for(int i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
        cop->valid[i] = stbuf.valid[i];
    }
    cop->param = (void*)stbuf.last_inst_id;
    io_dcache_port->st_input->push(cop);
    commited_store_buf.erase(iter);
    commited_store_buf_lru.pop_front();
    commited_store_buf_indexing_cnt++;
}

}}

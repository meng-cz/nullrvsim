

#include "simroot.h"

#include "cpu/operation.h"

#include "xslsu.h"

namespace simcpu {

namespace xs {

LSU::LSU(XiangShanParam *param, uint32_t cpu_id, CPUSystemInterface *io_sys, CacheInterface *io_dcache, LSUPort *port)
: param(param), cpu_id(cpu_id), io_sys_port(io_sys), io_dcache_port(io_dcache), port(port)
{
    ld_addr_trans_queue = make_unique<SimpleTickQueue<XSInst*>>(2, 2, 0);
    st_addr_trans_queue = make_unique<SimpleTickQueue<XSInst*>>(2, 2, 0);
}


void LSU::on_current_tick() {
    if(port->fence->cur_size()) {
        if(
            port->ld->empty() &&
            port->sta->empty() &&
            port->std->empty() &&
            ld_addr_trans_queue->empty() &&
            st_addr_trans_queue->empty() &&
            load_queue_total_cnt == 0 &&
            store_queue.empty() &&
            apl_push_store_queue.empty() &&
            commited_store_buf.empty() &&
            apl_commited_store_buf.empty()
        ) {
            XSInst *inst = port->fence->front();
            port->fence->pop_front();
            inst->finished = true;
            if(debug_ofile) {
                sprintf(log_buf, "%ld:FENCE @0x%lx, %ld", simroot::get_current_tick(), inst->pc, inst->id);
                simroot::log_line(debug_ofile, log_buf);
            }
            return;
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
        }
        commited_store_buf_lru.remove(lindex);
        commited_store_buf_lru.push_back(lindex);
    }
    apl_commited_store_buf.clear();
}

void LSU::apply_next_tick() {
    ld_addr_trans_queue->apply_next_tick();
    st_addr_trans_queue->apply_next_tick();

    load_queue_arrival.splice(load_queue_arrival.end(), apl_push_load_queue);
    apl_push_load_queue.clear();

    for(auto inst : apl_push_store_queue) {
        store_queue.insert(inst);
    }
    apl_push_store_queue.clear();

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
    load_queue_waiting_line.clear();
    load_queue_arrival.clear();
    apl_push_load_queue.clear();
    finished_load.clear();
    load_queue_total_cnt = 0;
    store_queue.clear();
    apl_push_store_queue.clear();
    apl_inst_finished.clear();
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
            inst->arg2 = vaddr;
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
        for(auto inst2 : finished_load) {
            if((inst2->arg2) != (inst->arg2)) continue;
            if(!inst_later_than(inst2->id, inst->id)) continue;
            inst2->err = SimError::slreorder;
        }
        amo_state = AMOState::flush_sbuffer_req;
    }
    else if(amo_state == AMOState::flush_sbuffer_req) {
        amo_state = AMOState::flush_sbuffer_resp;
    }
    else if(amo_state == AMOState::flush_sbuffer_resp) {
        if(commited_store_buf.empty() && apl_commited_store_buf.empty()) {
            amo_state = AMOState::cache_req;
        }
    }
    else if(amo_state == AMOState::cache_req) {
        SimError res = _do_amo(inst);
        if(res == SimError::success) {
            amo_state = AMOState::cache_resp;
        }
        else if(res == SimError::busy || res == SimError::coherence || res == SimError::miss) {

        }
        else {
            inst->err = res;
            amo_finished = true;
        }
    }
    else if(amo_state == AMOState::cache_resp) {
        amo_state = AMOState::finish;
        if((inst->flag & RVINSTFLAG_RDINT) && (inst->vrd)) {
            simroot_assert(port->apl_int_bypass->emplace(inst->prd, inst->arg0).second);
        }
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

/**
 * 从ld_addr_trans_queue取出一个指令并进行地址翻译，翻译成功则进入apl_push_load_queue，否则设置错误后结束指令。
 * 该阶段完成load-load违例检查，检查finished_load中是否存在比自己晚的同地址的指令，存在则将查到的指令标为llreorder错误。
*/
void LSU::_cur_ld_addr_trans() {
    while(ld_addr_trans_queue->can_pop() && load_queue_total_cnt < param->load_queue_size) {
        XSInst* inst = ld_addr_trans_queue->top();
        ld_addr_trans_queue->pop();
        VirtAddrT vaddr = inst->arg1 + RAW_DATA_AS(inst->imm).i64;
        bool succ = false;
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            inst->arg2 = vaddr;
            succ = true;
        }
        else {
            PhysAddrT paddr = 0;
            SimError res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_R);
            if(!(succ = (res == SimError::success))) {
                inst->err = res;
            }
            else {
                inst->arg2 = paddr;
            }
        }
        if(succ) {
            apl_push_load_queue.push_back(inst);
            load_queue_total_cnt++;
        }
        else {
            apl_inst_finished.push_back(inst);
            continue;
        }
        // load-load违例检查
        for(auto inst2 : finished_load) {
            if((inst2->arg2) != (inst->arg2)) continue;
            if(!inst_later_than(inst2->id, inst->id)) continue;
            inst2->err = SimError::llreorder;
        }

        if(debug_ofile) {
            sprintf(log_buf, "%ld:LD-ADDR: @0x%lx, %ld, %s, 0x%lx->0x%lx", 
                simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(), vaddr, inst->arg2
            );
            simroot::log_line(debug_ofile, log_buf);
        }
    }
}

/**
 * 获取cache当前周期新到达的cacheline，检查load_queue中是否有可完成的
*/
void LSU::_cur_load_queue() {
    // 将新到达的load指令合并到等待队列
    for(auto iter = load_queue_arrival.begin(); iter != load_queue_arrival.end(); ) {
        LineIndexT lindex = ((*iter)->arg2 >> CACHE_LINE_ADDR_OFFSET);
        auto res = load_queue_waiting_line.find(lindex);
        if(res != load_queue_waiting_line.end()) {
            res->second.push_back(*iter);
            iter = load_queue_arrival.erase(iter);
        }
        else {
            iter++;
        }
    }

    vector<simcache::ArrivalLine> newlines;
    list<XSInst*> wakeup_insts;
    io_dcache_port->arrival_line(&newlines);
    for(auto &l : newlines) {
        auto res = load_queue_waiting_line.find(l.lindex);
        if(res == load_queue_waiting_line.end()) continue;
        bool err = false;
        for(auto inst : res->second) {
            if(!err) err = (_do_load(inst) != SimError::success);
            if(err) {
                // 情报有误，这一行已经被invalid了，清空等待信息回到主队列等下一次访存
                load_queue_arrival.push_back(inst);
            }
            else {
                success_load_queue.push_back(inst);
                wakeup_insts.push_back(inst);
            }
        }
        load_queue_waiting_line.erase(res);
    }
    if(debug_ofile && !newlines.empty()) {
        sprintf(log_buf, "%ld:LD-NEWLINE: ", simroot::get_current_tick());
        string str(log_buf);
        for(auto &l : newlines) {
            sprintf(log_buf, "0x%lx ", l.lindex);
            str += log_buf;
        }
        str += ", WAKEUP: ";
        for(auto &p : wakeup_insts) {
            sprintf(log_buf, "@0x%lx,%ld ", p->pc, p->id);
            str += log_buf;
        }
        simroot::log_line(debug_ofile, log_buf);
    }
    
    if(newlines.empty() && !load_queue_arrival.empty()) {
        // dcache主流水线这周期没有收到refill，是空闲的，可以进行一个访存
        auto inst = load_queue_arrival.front();
        load_queue_arrival.pop_front();
        SimError res = _do_load(inst);
        if(res == SimError::success) {
            success_load_queue.push_back(inst);
        }
        else if(res == SimError::invalidaddr || res == SimError::unaligned) {
            apl_inst_finished.push_back(inst);
            finished_load.insert(inst);
            inst->err = res;
        }
        else if(res == SimError::busy || res == SimError::coherence) {
            // dcache暂时没法响应这个请求，扔回队尾等会再说
            load_queue_arrival.push_back(inst);
        }
        else {
            // dcache miss，并且已经在处理了，挂到等待区
            LineIndexT lindex = (inst->arg2 >> CACHE_LINE_ADDR_OFFSET);
            auto res = load_queue_waiting_line.find(lindex);
            if(res == load_queue_waiting_line.end()) {
                res = load_queue_waiting_line.emplace(lindex, list<XSInst*>()).first;
            }
            res->second.push_back(inst);
        }
    }

    // LSU只有一个INT一个FP两个写回端口，每周期只能写回两条指令到旁路网络
    uint32_t int_wb = 0, fp_wb = 0;
    const uint32_t lsu_int_wb_width = 1, lsu_fp_wb_width = 1;
    auto iter = success_load_queue.begin();
    for(;iter != success_load_queue.end() && (int_wb < lsu_int_wb_width || fp_wb < lsu_fp_wb_width);) {
        auto inst = *iter;
        bool rdint = ((inst->flag & RVINSTFLAG_RDINT) && (inst->vrd));
        bool rdfp = (inst->flag & RVINSTFLAG_RDFP);
        bool wbbusy = ((rdint && (int_wb >= lsu_int_wb_width)) || (rdfp && (fp_wb >= lsu_fp_wb_width)));

        if(wbbusy) {
            iter ++;
            continue;
        }
        iter = success_load_queue.erase(iter);

        apl_inst_finished.push_back(inst);
        finished_load.insert(inst);
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

SimError LSU::_do_load(XSInst *inst) {
    VirtAddrT paddr = inst->arg2;
    uint32_t len = isa::rv64_ls_width_to_length(inst->param.loadstore);
    uint64_t buf = 0;
    SimError ret = SimError::success;
    if(io_sys_port->is_dev_mem(cpu_id, paddr)) {
        io_sys_port->dev_input(cpu_id, paddr, len, &(buf));
    }
    else {
        ret = io_dcache_port->load(paddr, len, &buf, true);
        vector<bool> bypassed;
        bypassed.assign(len, false);
        uint8_t *byte_data = (uint8_t*)(&buf);
        if(ret != SimError::unaligned) {
            uint64_t offset = (paddr & ((1UL << CACHE_LINE_ADDR_OFFSET) - 1));
            auto res = commited_store_buf.find(paddr >> CACHE_LINE_ADDR_OFFSET);
            if(res != commited_store_buf.end()) {
                auto &stbuf = res->second;
                for(int i = 0; i < len; i++) {
                    if(stbuf.valid[offset + i]) {
                        byte_data[i] = res->second.linebuf[offset + i];
                        bypassed[i] = true;
                    }
                }
            }
            res = apl_commited_store_buf.find(paddr >> CACHE_LINE_ADDR_OFFSET);
            if(res != apl_commited_store_buf.end()) {
                auto &stbuf = res->second;
                for(int i = 0; i < len; i++) {
                    if(stbuf.valid[offset + i]) {
                        byte_data[i] = res->second.linebuf[offset + i];
                        bypassed[i] = true;
                    }
                }
            }
            for(auto inst2 : store_queue) {
                if(inst_later_than(inst2->id, inst->id)) continue;
                if((inst2->arg2 >> CACHE_LINE_ADDR_OFFSET) != (inst->arg2 >> CACHE_LINE_ADDR_OFFSET)) continue;
                uint32_t len2 = isa::rv64_ls_width_to_length(inst2->param.loadstore);
                uint64_t offset2 = ((inst2->arg2) & ((1UL << CACHE_LINE_ADDR_OFFSET) - 1));
                uint8_t *byte_data2 = (uint8_t*)(&(inst2->arg0));
                for(int i = 0; i < len2; i++) {
                    if(offset2 + i >= offset && offset2 + i < offset + len) {
                        byte_data[offset2 + i - offset] = byte_data2[i];
                        bypassed[offset2 + i - offset] = true;
                    }
                }
            }
        }
        if(std::count(bypassed.begin(), bypassed.end(), true) == len) {
            ret = SimError::success;
        }
    }
    if(ret == SimError::success) {
        switch (inst->param.loadstore)
        {
        case isa::RV64LSWidth::byte : RAW_DATA_AS(inst->arg0).i64 = *((int8_t*)(&buf)); break;
        case isa::RV64LSWidth::harf : RAW_DATA_AS(inst->arg0).i64 = *((int16_t*)(&buf)); break;
        case isa::RV64LSWidth::word : RAW_DATA_AS(inst->arg0).i64 = *((int32_t*)(&buf)); break;
        case isa::RV64LSWidth::dword : RAW_DATA_AS(inst->arg0).i64 = *((int64_t*)(&buf)); break;
        case isa::RV64LSWidth::ubyte: inst->arg0 = *((uint8_t*)(&buf)); break;
        case isa::RV64LSWidth::uharf: inst->arg0 = *((uint16_t*)(&buf)); break;
        case isa::RV64LSWidth::uword: inst->arg0 = *((uint32_t*)(&buf)); break;
        default: simroot_assert(0);
        }
    }
    return ret;
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
 * 从STD保留站中获取一个双操作数均就绪的指令发射到apl_push_store_queue
*/
void LSU::_cur_get_std_from_rs() {
    auto &rs = port->std->get();
    for(int __n = 0; __n < 2; __n++) {
        if(rs.empty() || store_queue.size() + apl_push_store_queue.size() >= param->store_queue_size) return;
        auto iter = rs.begin();
        for( ; iter != rs.end(); iter++) {
            if((*iter)->rsready[1] && (*iter)->rsready[2]) break;
        }
        if(iter == rs.end()) return;
        auto inst = *iter;
        iter = rs.erase(iter);
        apl_push_store_queue.push_back(inst);
        apl_inst_finished.push_back(inst);
        // store-load违例检查
        for(auto inst2 : finished_load) {
            if((inst2->arg2) != (inst->arg2)) continue;
            if(!inst_later_than(inst2->id, inst->id)) continue;
            inst2->err = SimError::slreorder;
        }
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
            inst->rsready[2] = true;
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
    if(finished_load.erase(inst)) {
        simroot_assert(load_queue_total_cnt > 0);
        load_queue_total_cnt -= 1;
    }
}

/**
 * 从store_queue中找出对应项，将store内容加入commited_store_buf
*/
void LSU::_cur_commit_store(XSInst *inst) {
    if(store_queue.erase(inst) == 0) return;

    PhysAddrT paddr = inst->arg2;
    RawDataT data = inst->arg0;
    uint32_t len = isa::rv64_ls_width_to_length(inst->param.loadstore);
    
    if(debug_ofile) {
        sprintf(log_buf, "%ld:ST: @0x%lx, %ld, %s, A:0x%lx->0x%lx D:0x%lx",
            simroot::get_current_tick(), inst->pc, inst->id, inst->dbgname.c_str(),
            RAW_DATA_AS(inst->arg1).i64 + RAW_DATA_AS(inst->imm).i64, inst->arg2, inst->arg0
        );
        simroot::log_line(debug_ofile, log_buf);
    }

    if(io_sys_port->is_dev_mem(cpu_id, paddr)) {
        io_sys_port->dev_output(cpu_id, paddr, len, &data);
        return;
    }

    LineIndexT lindex = (paddr >> CACHE_LINE_ADDR_OFFSET);
    auto res_cmt = apl_commited_store_buf.find(lindex);
    if(res_cmt == apl_commited_store_buf.end()) {
        res_cmt = apl_commited_store_buf.emplace(lindex, StoreBufEntry()).first;
        memset(&(res_cmt->second), 0, sizeof(res_cmt->second));
    }
    StoreBufEntry &stbuf = res_cmt->second;
    uint64_t offset = (paddr & ((1UL << CACHE_LINE_ADDR_OFFSET) - 1));
    for(int i = 0; i < len && offset + i < CACHE_LINE_LEN_BYTE; i++) {
        stbuf.valid[offset + i] = true;
    }
    memcpy(stbuf.linebuf + offset, &data, len);
}

/**
 * 从committed store buffer中选择最旧的一行写回到dcache
*/
void LSU::_cur_write_commited_store() {
    if(commited_store_buf.empty()) return;
    LineIndexT lindex = commited_store_buf_lru.front();
    uint8_t linebuf[CACHE_LINE_LEN_BYTE];
    SimError res = io_dcache_port->store(lindex << CACHE_LINE_ADDR_OFFSET, 0, linebuf, true);
    if(res == SimError::miss) {
        return;
    }
    else if(res == SimError::success) {
        auto iter = commited_store_buf.find(lindex);
        simroot_assert(iter != commited_store_buf.end());
        auto &stbuf = iter->second;
        simroot_assert(SimError::success == io_dcache_port->load(lindex << CACHE_LINE_ADDR_OFFSET, CACHE_LINE_LEN_BYTE, linebuf, true));
        for(int i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
            if(stbuf.valid[i]) linebuf[i] = stbuf.linebuf[i];
        }
        simroot_assert(SimError::success == io_dcache_port->store(lindex << CACHE_LINE_ADDR_OFFSET, CACHE_LINE_LEN_BYTE, linebuf, true));
        commited_store_buf.erase(iter);
        commited_store_buf_lru.pop_front();

        if(debug_ofile) {
            sprintf(log_buf, "%ld:ST-WB: Line @0x%lx, %ld Remain", simroot::get_current_tick(), lindex, commited_store_buf.size());
            simroot::log_line(debug_ofile, log_buf);
        }

    }
    else if(res == SimError::busy || res == SimError::coherence) {
        commited_store_buf_lru.pop_front();
        commited_store_buf_lru.push_back(lindex);
    }
    else {
        simroot_assert(0);
    }
}

SimError LSU::_do_amo(XSInst *inst) {
    simroot_assert(inst->opcode == RV64OPCode::amo);

    if(inst->param.amo.op == isa::RV64AMOOP5::SC) {
        VirtAddrT vaddr = inst->arg1;
        uint64_t data = inst->arg0;
        uint32_t len = isa::rv64_ls_width_to_length(inst->param.amo.wid);
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            // 不允许对设备内存进行LR-SC
            LOG(ERROR) << "Cannot perform LR/SC on device memory";
            simroot_assert(0);
        }
        else {
            PhysAddrT paddr = 0;
            SimError res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_R | PGFLAG_W);
            if(res != SimError::success) return res;
            res = io_dcache_port->store_conditional(paddr, len, &data);
            if(res == SimError::success) {
                inst->arg0 = 0;
            }
            else if(res == SimError::unconditional) {
                inst->arg0 = 1;
                res = SimError::success;
            }
            return res;
        }
    }
    else if(inst->param.amo.op == isa::RV64AMOOP5::LR) {
        VirtAddrT vaddr = inst->arg1;
        uint32_t len = isa::rv64_ls_width_to_length(inst->param.amo.wid);
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            // 不允许对设备内存进行LR-SC
            LOG(ERROR) << "Cannot perform LR/SC on device memory";
            simroot_assert(0);
        }
        else {
            PhysAddrT paddr = 0;
            SimError res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_R | PGFLAG_W);
            if(res != SimError::success) return res;
            int64_t data = 0;
            res = SimError::miss;
            if(inst->param.amo.wid == isa::RV64LSWidth::word) {
                int32_t tmp = 0;
                res = io_dcache_port->load_reserved(paddr, 4, &tmp);
                data = tmp;
            }
            else {
                res = io_dcache_port->load_reserved(paddr, 8, &data);
            }
            if(res == SimError::success) {
                RAW_DATA_AS(inst->arg0).i64 = data;
            }
            return res;
        }
    }
    else {
        VirtAddrT vaddr = inst->arg1;
        PhysAddrT paddr = 0;
        IntDataT previous = 0;
        IntDataT value = inst->arg0;
        IntDataT stvalue = 0;
        uint32_t len = isa::rv64_ls_width_to_length(inst->param.amo.wid);
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            io_sys_port->dev_amo(cpu_id, vaddr, len, inst->param.amo.op, &value, &previous);
        }
        else {
            SimError res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_R | PGFLAG_W);
            if(res != SimError::success) return res;
            res = io_dcache_port->store(paddr, 0, &stvalue);
            if(res != SimError::success) {
                return res;
            }
            res = SimError::miss;
            if(inst->param.amo.wid == isa::RV64LSWidth::word) {
                int32_t tmp = 0;
                res = io_dcache_port->load(paddr, 4, &tmp);
                RAW_DATA_AS(previous).i64 = tmp;
            }
            else {
                res = io_dcache_port->load(paddr, 8, &previous);
            }
            if(res != SimError::success) {
                return res;
            }
            inst->err = isa::perform_amo_op(inst->param.amo, &stvalue, previous, value);
            if(inst->err == SimError::success) {
                res = io_dcache_port->store(paddr, len, &stvalue);
                if(res == SimError::success) {
                    inst->arg0 = previous;
                }
            }
            return res;
        }
    }
    
    return SimError::success;
}

}}

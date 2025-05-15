
#include "l1l2v2.h"

#include "simroot.h"
#include "configuration.h"

namespace simcache {
namespace moesi {


PrivL1L2Moesi::PrivL1L2Moesi(
    CacheParam &l2_param,
    CacheParam &l1d_param,
    CacheParam &l1i_param,
    BusInterfaceV2 *bus,
    BusPortT my_port_id,
    BusPortMapping *busmap,
    string logname,
    CacheEventTrace *trace
) : bus(bus), my_port_id(my_port_id), busmap(busmap), logname(logname), trace(trace), l2_param(l2_param), l1d_param(l1d_param), l1i_param(l1i_param) {
    do_on_current_tick = 6;
    do_apply_next_tick = 1;

    {
        l1d_block = make_unique<GenericLRUCacheBlock<TagedCacheLine>>(l1d_param.set_offset, l1d_param.way_cnt);
        l1d_query_width = l1d_param.index_width;
        uint32_t query_cycle = l1d_param.index_latency;
        if(query_cycle < 1) query_cycle = 1;
        l1d_ld_queues.resize(query_cycle + 1, SimpleTickQueue<CacheOP*>(l1d_query_width, l1d_query_width, 0));
        l1d_st_queues.resize(query_cycle + 1, SimpleTickQueue<CacheOP*>(l1d_query_width, l1d_query_width, 0));
        l1d_amo_queues.resize(query_cycle + 1, SimpleTickQueue<CacheOP*>(l1d_query_width, l1d_query_width, 0));
        l1d_misc_queues.resize(query_cycle + 1, SimpleTickQueue<CacheOP*>(l1d_query_width, l1d_query_width, 0));
    }

    {
        l1i_block = make_unique<GenericLRUCacheBlock<TagedCacheLine>>(l1i_param.set_offset, l1i_param.way_cnt);
        l1i_query_width = l1i_param.index_width;
        uint32_t query_cycle = l1i_param.index_latency;
        if(query_cycle < 1) query_cycle = 1;
        l1i_ld_queues.resize(query_cycle + 1, SimpleTickQueue<CacheOP*>(l1i_query_width, l1i_query_width, 0));
    }

    mshrs = make_unique<MSHRArray<MSHREntry>>(l2_param.mshr_num);
    block = make_unique<GenericLRUCacheBlock<TagedCacheLine>>(l2_param.set_offset, l2_param.way_cnt);
    index_cycle = l2_param.index_latency;
    if(index_cycle < 1) index_cycle = 1;

    queue_index = make_unique<SimpleTickQueue<ProcessingPackage*>>(1, 1, 0);

    // log_info = true;
}

void PrivL1L2Moesi::dump_core(std::ofstream &ofile) {
    ofile << "L2:\n";
    for(uint32_t s = 0; s < block->set_count; s++) {
        sprintf(log_buf, "set %d: ", s);
        ofile << log_buf;
        for(auto &e : block->p_sets[s]) {
            sprintf(log_buf, "0x%lx:%s-%d ", e.first, get_cache_state_name_str(e.second.state).c_str(), e.second.flag);
            ofile << log_buf;
        }
        ofile << "\n";
    }
    ofile << "mshr: ";
    for(auto &e : mshrs->hashmap) {
        sprintf(log_buf, "0x%lx:%s,%d-%d ", e.first, get_cache_mshr_state_name_str(e.second.state).c_str(), e.second.line_flag, e.second.finish_flag);
        ofile << log_buf;
    }
    ofile << "\n";
    ofile << "L1-I:\n";
    for(uint32_t s = 0; s < l1i_block->set_count; s++) {
        sprintf(log_buf, "set %d: ", s);
        ofile << log_buf;
        for(auto &e : l1i_block->p_sets[s]) {
            sprintf(log_buf, "0x%lx:%d ", e.first, e.second.state);
            ofile << log_buf;
        }
        ofile << "\n";
    }
    sprintf(log_buf, "L1-I-REQ: %d, %s, 0x%lx\n", l1i_req.type, l1i_req.indexing?"indexing":"waiting", l1i_req.lindex);
    ofile << log_buf;
    ofile << "L1-D:\n";
    for(uint32_t s = 0; s < l1d_block->set_count; s++) {
        sprintf(log_buf, "set %d: ", s);
        ofile << log_buf;
        for(auto &e : l1d_block->p_sets[s]) {
            sprintf(log_buf, "0x%lx:%d ", e.first, e.second.flag);
            ofile << log_buf;
        }
        ofile << "\n";
    }
    sprintf(log_buf, "L1-D-REQ: %d, %s, 0x%lx\n", l1d_req.type, l1d_req.indexing?"indexing":"waiting", l1d_req.lindex);
    ofile << log_buf;
}

SimError PrivL1L2Moesi::l1i_load(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = addr_to_line_index(paddr);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));

    TagedCacheLine *p_line = nullptr;
    if(l1i_block->get_line(lindex, &p_line, true)) {
        if(len) {
            if(valid.size() != len) memcpy(buf, ((uint8_t*)(p_line->data)) + offset, len);
            else {
                for(int i = 0; i < len; i++) {
                    if(valid[i]) ((uint8_t*)buf)[i] = ((uint8_t*)(p_line->data))[i + offset];
                }
            }
        }
        statistic.l1i_hit_count++;
        return SimError::success;
    }

    if(l1i_req.type == 0) {
        l1i_req.type = L1REQ_GETS;
        l1i_req.indexing = false;
        l1i_req.lindex = lindex;
        l1i_req.data.clear();
        if(trace) {
            l1i_req.trans_id = trace->alloc_trans_id();
            trace->insert_event(l1i_req.trans_id, CacheEvent::L1_LD_MISS);
        }
        statistic.l1i_miss_count++;
        return SimError::miss;
    }
    else if((l1i_req.type == L1REQ_GETS || l1i_req.type == L1REQ_GETM) && l1i_req.lindex == lindex) {
        return SimError::miss;
    }

    return SimError::busy;
}

void PrivL1L2Moesi::l1i_clear_ld(std::list<CacheOP*> *to_free) {
    for(auto &q : l1i_ld_queues) {
        q.clear(to_free);
    }
}

bool PrivL1L2Moesi::l1i_is_empty() {
    bool empty = true;
    for(auto &q : l1i_ld_queues) {
        empty = (empty && q.empty());
    }
    return empty;
}

SimError PrivL1L2Moesi::l1d_load(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = addr_to_line_index(paddr);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));

    TagedCacheLine *p_line = nullptr;
    if(l1d_block->get_line(lindex, &p_line, true)) {
        if(len) {
            if(valid.size() != len) memcpy(buf, ((uint8_t*)(p_line->data)) + offset, len);
            else {
                for(int i = 0; i < len; i++) {
                    if(valid[i]) ((uint8_t*)buf)[i] = ((uint8_t*)(p_line->data))[i + offset];
                }
            }
        }
        statistic.l1d_hit_count++;
        return SimError::success;
    }

    if(l1d_req.type == 0) {
        l1d_req.type = L1REQ_GETS;
        l1d_req.indexing = false; 
        l1d_req.lindex = lindex;
        l1d_req.data.clear();
        if(trace) {
            l1d_req.trans_id = trace->alloc_trans_id();
            trace->insert_event(l1d_req.trans_id, CacheEvent::L1_LD_MISS);
        }
        statistic.l1d_miss_count++;
        return SimError::miss;
    }
    else if((l1d_req.type == L1REQ_GETS || l1d_req.type == L1REQ_GETM) && l1d_req.lindex == lindex) {
        return SimError::miss;
    }

    return SimError::busy;
}

SimError PrivL1L2Moesi::l1d_store(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = addr_to_line_index(paddr);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));

    TagedCacheLine *p_line = nullptr;
    if(l1d_block->get_line(lindex, &p_line, true) && (p_line->flag & L1FLG_WRITE)) {
        if(len) {
            if(valid.size() != len) memcpy(((uint8_t*)(p_line->data)) + offset, buf, len);
            else {
                for(int i = 0; i < len; i++) {
                    if(valid[i]) ((uint8_t*)(p_line->data))[i + offset] = ((uint8_t*)buf)[i];
                }
            }
        }
        p_line->flag |= L1FLG_DIRTY;
        reserved_address_valid = false;
        statistic.l1d_hit_count++;
        return SimError::success;
    }

    if(l1d_req.type == 0 || (l1d_req.type == L1REQ_GETS && l1d_req.lindex == lindex)) {
        l1d_req.type = L1REQ_GETM;
        l1d_req.indexing = false;
        l1d_req.lindex = lindex;
        l1d_req.data.clear();
        if(trace) {
            l1d_req.trans_id = trace->alloc_trans_id();
            trace->insert_event(l1d_req.trans_id, CacheEvent::L1_ST_MISS);
        }
        statistic.l1d_miss_count++;
        return SimError::miss;
    }
    else if(l1d_req.type == L1REQ_GETM && l1d_req.lindex == lindex) {
        return SimError::miss;
    }

    return SimError::busy;
}

SimError PrivL1L2Moesi::l1d_amo(PhysAddrT paddr, uint32_t len, void *buf, isa::RV64AMOOP5 amoop) {
    if(amoop == isa::RV64AMOOP5::SC) {
        return l1d_store_conditional(paddr, len, buf);
    }
    else if(amoop == isa::RV64AMOOP5::LR) {
        return l1d_load_reserved(paddr, len, buf);
    }
    else {
        vector<bool> _tmp;
        IntDataT previous = 0;
        IntDataT value = 0;
        IntDataT stvalue = 0;
        isa::RV64AMOParam param;
        param.op = amoop;
        SimError res = l1d_store(paddr, 0, &stvalue, _tmp);
        if(res != SimError::success) {
            return res;
        }
        res = SimError::miss;
        if(len == 4) {
            int32_t tmp = 0;
            res = l1d_load(paddr, 4, &tmp, _tmp);
            RAW_DATA_AS(previous).i64 = tmp;
            RAW_DATA_AS(value).i64 = *((int32_t*)buf);
            param.wid = isa::RV64LSWidth::word;
        }
        else {
            res = l1d_load(paddr, 8, &previous, _tmp);
            value = *((uint64_t*)buf);
            param.wid = isa::RV64LSWidth::dword;
        }
        if(res != SimError::success) {
            return res;
        }
        res = isa::perform_amo_op(param, &stvalue, previous, value);
        if(res == SimError::success) {
            res = l1d_store(paddr, len, &stvalue, _tmp);
            if(res == SimError::success) {
                memcpy(buf, &previous, len);
            }
        }
        return res;
    }
}

SimError PrivL1L2Moesi::l1d_load_reserved(PhysAddrT paddr, uint32_t len, void *buf) {
    vector<bool> _tmp;
    SimError ret = l1d_load(paddr, len, buf, _tmp);
    if(ret == SimError::success) {
        reserved_address_valid = true;
        reserved_address = paddr;
    }
    return ret;
}

SimError PrivL1L2Moesi::l1d_store_conditional(PhysAddrT paddr, uint32_t len, void *buf) {
    vector<bool> _tmp;
    bool conditional = false;
    conditional = (reserved_address_valid && (reserved_address == paddr));
    SimError ret = SimError::unconditional;
    if(conditional) ret = l1d_store(paddr, len, buf, _tmp);
    return ret;
}

void PrivL1L2Moesi::l1d_clear_ld(std::list<CacheOP*> *to_free) {
    for(auto &q : l1d_ld_queues) {
        q.clear(to_free);
    }
}

void PrivL1L2Moesi::l1d_clear_st(std::list<CacheOP*> *to_free) {
    for(auto &q : l1d_st_queues) {
        q.clear(to_free);
    }
}

void PrivL1L2Moesi::l1d_clear_amo(std::list<CacheOP*> *to_free) {
    for(auto &q : l1d_amo_queues) {
        q.clear(to_free);
    }
}

void PrivL1L2Moesi::l1d_clear_misc(std::list<CacheOP*> *to_free) {
    for(auto &q : l1d_misc_queues) {
        q.clear(to_free);
    }
}

bool PrivL1L2Moesi::l1d_is_empty() {
    bool empty = true;
    for(auto &q : l1d_ld_queues) {
        empty = (empty && q.empty());
    }
    for(auto &q : l1d_st_queues) {
        empty = (empty && q.empty());
    }
    for(auto &q : l1d_amo_queues) {
        empty = (empty && q.empty());
    }
    for(auto &q : l1d_misc_queues) {
        empty = (empty && q.empty());
    }
    return empty;
}


void PrivL1L2Moesi::p1_fetch() {
    if(queue_index->can_push() == 0) {
        return;
    }

    if(recv_msg && waiting_msg.size() < waiting_buf_size) {
        if(log_info) {
            sprintf(log_buf, "%s: Recv: @0x%lx, %d", logname.c_str(), recv_msg->line, recv_msg->type);
            simroot::print_log_info(log_buf);
        }
        waiting_msg.push_back(recv_msg);
        recv_msg = nullptr;
    }

    for(auto iter = waiting_msg.begin(); iter != waiting_msg.end(); iter++) {
        if(processing_line.find((*iter)->line) == processing_line.end()) {
            ProcessingPackage *pak = new ProcessingPackage;
            pak->lindex = (*iter)->line;
            pak->msg = (*iter);
            switch ((*iter)->type)
            {
            case MSG_INVALID_ACK:
            case MSG_GETM_ACK:
            case MSG_PUT_ACK:
                // 这些只需要索引MSHR，不需要索引CacheBlock
                pak->index_cycle = 1;
                break;
            default:
                pak->index_cycle = index_cycle;
            }
            simroot_assert(queue_index->push(pak));
            processing_line.insert(pak->lindex);
            block->pin(pak->lindex);
            waiting_msg.erase(iter);
            return;
        }
    }

    if(l1d_req.type && !l1d_req.indexing && processing_line.find(l1d_req.lindex) == processing_line.end()) {
        ProcessingPackage *pak = new ProcessingPackage;
        pak->lindex = l1d_req.lindex;
        pak->msg = nullptr;
        pak->index_cycle = index_cycle;
        simroot_assert(queue_index->push(pak));
        processing_line.insert(pak->lindex);
        block->pin(pak->lindex);
        l1d_req.indexing = true;
        return;
    }

    if(l1i_req.type && !l1i_req.indexing && processing_line.find(l1i_req.lindex) == processing_line.end()) {
        ProcessingPackage *pak = new ProcessingPackage;
        pak->lindex = l1i_req.lindex;
        pak->msg = nullptr;
        pak->index_cycle = index_cycle;
        simroot_assert(queue_index->push(pak));
        processing_line.insert(pak->lindex);
        block->pin(pak->lindex);
        l1i_req.indexing = true;
        return;
    }

}

void PrivL1L2Moesi::p2_index() {
    if(queue_index->can_pop() == 0) {
        return;
    }
    ProcessingPackage *pak = queue_index->top();
    if(pak->index_cycle > 1) {
        pak->index_cycle--;
        return;
    }
    if(send_buf.size() + 2 > send_buf_size) {
        return;
    }
    queue_index->pop();

    LineIndexT lindex = pak->lindex;
    BusPortT hn_port = 0;
    simroot_assert(busmap->get_homenode_port(pak->lindex, &hn_port));

    if(pak->msg) {
        uint32_t type = pak->msg->type;
        uint32_t arg = pak->msg->arg;
        uint32_t transid = pak->msg->transid;
        vector<uint8_t> &data = pak->msg->data;

        if(log_info) {
            sprintf(log_buf, "%s: Handle from bus: @0x%lx, %d, %d", logname.c_str(), lindex, type, arg);
            simroot::print_log_info(log_buf);
        }

        if(type == MSG_INVALID) {
            push_send_buf(arg, CHANNEL_ACK, MSG_INVALID_ACK, lindex, 0, transid);
            block->remove_line(lindex);
            l1i_block->remove_line(lindex);
            l1d_block->remove_line(lindex);
            l1i_busy_cycle++;
            l1d_busy_cycle++;
            MSHREntry *mshr = nullptr;
            if((mshr = mshrs->get(lindex))) {
                switch (mshr->state)
                {
                case MSHR_STOI:
                case MSHR_MTOI:
                case MSHR_OTOI:
                    mshr->state = MSHR_ITOI;
                    break;
                case MSHR_OTOM:
                case MSHR_STOM:
                case MSHR_ITOM:
                    mshr->state = MSHR_ITOM;
                    break;
                default:
                    simroot_assert(0);
                }
            }
            if(reserved_address_valid && lindex == addr_to_line_index(reserved_address)) {
                reserved_address_valid = false;
            }
        }
        else if(type == MSG_INVALID_ACK) {
            bool getm_finished = false;
            MSHREntry *mshr = nullptr;
            simroot_assert(mshr = mshrs->get(lindex));
            if(mshr->state == MSHR_ITOM) {
                if(mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack + 1 || mshr->get_data_ready == 0) {
                    mshr->invalid_ack++;
                }
                else {
                    getm_finished = true;
                }
            }
            else if(mshr->state == MSHR_STOM || mshr->state == MSHR_OTOM) {
                if(mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack + 1) {
                    mshr->invalid_ack++;
                }
                else {
                    getm_finished = true;
                }
            }
            if(getm_finished) {
                push_send_buf(hn_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id, transid);
                if(trace) trace->insert_event(transid, CacheEvent::L2_FINISH);
                handle_new_line_nolock(lindex, mshr, CC_MODIFIED);
            }
        }
        else if(type == MSG_GETS_FORWARD) {
            TagedCacheLine *p_line = nullptr;
            bool hit = block->get_line(lindex, &p_line);
            MSHREntry *mshr = mshrs->get(lindex);
            bool mshr_handle = (mshr && (
                mshr->state == MSHR_STOM || 
                mshr->state == MSHR_MTOI || 
                mshr->state == MSHR_STOI || 
                mshr->state == MSHR_ETOI || 
                mshr->state == MSHR_OTOM || 
                mshr->state == MSHR_OTOI
            ));
            simroot_assert(hit || mshr_handle);
            simroot_assert(!hit || !mshr_handle);
            if(hit) {
                snoop_l1d_and_set_readonly(lindex, p_line->data);
                push_send_buf_with_line(arg, CHANNEL_RESP, MSG_GETS_RESP, lindex, 1, p_line->data, transid);
                p_line->state = CC_OWNED;
            }
            else if(mshr_handle) {
                snoop_l1d_and_set_readonly(lindex, mshr->line_buf);
                push_send_buf_with_line(arg, CHANNEL_RESP, MSG_GETS_RESP, lindex, 1, mshr->line_buf, transid);
                if(mshr->state == MSHR_MTOI || mshr->state == MSHR_ETOI || mshr->state == MSHR_STOI) {
                    mshr->state = MSHR_OTOI;
                }
            }
            if(trace) trace->insert_event(transid, CacheEvent::L2_TRANSMIT);
        }
        else if(type == MSG_GETM_FORWARD) {
            TagedCacheLine *p_line = nullptr;
            bool hit = block->get_line(lindex, &p_line);
            MSHREntry *mshr = mshrs->get(lindex);
            bool mshr_handle = (mshr && (
                mshr->state == MSHR_STOM || 
                mshr->state == MSHR_MTOI || 
                mshr->state == MSHR_STOI || 
                mshr->state == MSHR_ETOI || 
                mshr->state == MSHR_OTOM || 
                mshr->state == MSHR_OTOI
            ));
            simroot_assert(hit || mshr_handle);
            simroot_assert(!hit || !mshr_handle);
            if(hit) {
                snoop_l1_and_invalid(lindex, p_line->data);
                push_send_buf_with_line(arg, CHANNEL_RESP, MSG_GETM_RESP, lindex, 0, p_line->data, transid);
                block->remove_line(lindex);
            }
            else if(mshr_handle) {
                snoop_l1_and_invalid(lindex, p_line->data);
                push_send_buf_with_line(arg, CHANNEL_RESP, MSG_GETM_RESP, lindex, 0, mshr->line_buf, transid);
                switch (mshr->state)
                {
                case MSHR_STOM:
                case MSHR_OTOM:
                    mshr->state = MSHR_ITOM;
                    break;
                case MSHR_STOI:
                case MSHR_MTOI:
                case MSHR_ETOI:
                case MSHR_OTOI:
                    mshr->state = MSHR_ITOI;
                    break;
                }
            }
            if(trace) trace->insert_event(transid, CacheEvent::L2_TRANSMIT);
        }
        else if(type == MSG_GETM_ACK) {
            bool getm_finished = false;
            MSHREntry *mshr = nullptr;
            simroot_assert(mshr = mshrs->get(lindex));
            if(mshr->state == MSHR_ITOM) {
                if(arg != mshr->invalid_ack || mshr->get_data_ready == 0) {
                    mshr->get_ack_cnt_ready = 1;
                    mshr->need_invalid_ack = arg;
                }
                else {
                    getm_finished = true;
                }
            }
            else if(mshr->state == MSHR_STOM || mshr->state == MSHR_OTOM) {
                if(arg != mshr->invalid_ack) {
                    mshr->get_ack_cnt_ready = 1;
                    mshr->need_invalid_ack = arg;
                }
                else {
                    getm_finished = true;
                }
            }
            if(getm_finished) {
                push_send_buf(hn_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id, transid);
                if(trace) trace->insert_event(transid, CacheEvent::L2_FINISH);
                handle_new_line_nolock(lindex, mshr, CC_MODIFIED);
            }
        }
        else if(type == MSG_GETS_RESP) {
            MSHREntry *mshr = nullptr;
            simroot_assert((mshr = mshrs->get(lindex)) && MSHR_ITOS);
            cache_line_copy(mshr->line_buf, data.data());
            if(arg > 0) {
                push_send_buf(hn_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id, transid);
            }
            if(trace) trace->insert_event(transid, CacheEvent::L2_FINISH);
            handle_new_line_nolock(lindex, mshr, (arg > 0)?(CC_SHARED):(CC_EXCLUSIVE));
        }
        else if(type == MSG_GETM_RESP) {
            bool getm_finished = false;
            MSHREntry *mshr = nullptr;
            simroot_assert((mshr = mshrs->get(lindex)) && mshr->state == MSHR_ITOM);
            if(arg == 0 && (mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack)) {
                cache_line_copy(mshr->line_buf, data.data());
                mshr->get_data_ready = 1;
            }
            else {
                if(arg == 0) push_send_buf(hn_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id, transid);
                cache_line_copy(mshr->line_buf, data.data());
                if(trace) trace->insert_event(transid, CacheEvent::L2_FINISH);
                handle_new_line_nolock(lindex, mshr, CC_MODIFIED);
            }
        }
        else if(type == MSG_GET_RESP_MEM) {
            push_send_buf(hn_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id, transid);
            MSHREntry *mshr = nullptr;
            simroot_assert(mshr = mshrs->get(lindex));
            cache_line_copy(mshr->line_buf, data.data());
            if(mshr->state == MSHR_ITOM) {
                if(trace) trace->insert_event(transid, CacheEvent::L2_FINISH);
                handle_new_line_nolock(lindex, mshr, CC_MODIFIED);
            }
            else if(mshr->state == MSHR_ITOS) {
                if(trace) trace->insert_event(transid, CacheEvent::L2_FINISH);
                handle_new_line_nolock(lindex, mshr, CC_EXCLUSIVE);
            }
            else {
                simroot_assert(0);
            }
        }
        else if(type == MSG_PUT_ACK) {
            MSHREntry *mshr = nullptr;
            simroot_assert(mshr = mshrs->get(lindex));
            if(mshr->state == MSHR_ITOI || 
                mshr->state == MSHR_MTOI || 
                mshr->state == MSHR_STOI || 
                mshr->state == MSHR_ETOI || 
                mshr->state == MSHR_OTOI
            ) {
                uint32_t mshr_finish_flg = mshr->finish_flag;
                mshrs->remove(lindex);
                if(mshr_finish_flg & MSHR_FIFLG_L1DREQM) {
                    simroot_assert(mshr = mshrs->alloc(lindex));
                    mshr->state = MSHR_ITOM;
                    mshr->finish_flag = mshr_finish_flg;
                    push_send_buf(hn_port, CHANNEL_REQ, MSG_GETM, lindex, my_port_id, (trace?(trace->alloc_trans_id()):0));
                }
                else if((mshr_finish_flg & MSHR_FIFLG_L1DREQS) || (mshr_finish_flg & MSHR_FIFLG_L1IREQ)) {
                    simroot_assert(mshr = mshrs->alloc(lindex));
                    mshr->state = MSHR_ITOS;
                    mshr->finish_flag = mshr_finish_flg;
                    push_send_buf(hn_port, CHANNEL_REQ, MSG_GETS, lindex, my_port_id, (trace?(trace->alloc_trans_id()):0));
                }
            }
            else {
                simroot_assert(0);
            }
        }
    }
    else {
        // 检查L1REQ
        uint32_t req = L1REQ_GETS;
        uint32_t transid = 0;
        bool resp_i = false, resp_d = false;

        if(l1i_req.lindex == lindex && l1i_req.type == L1REQ_GETS) {
            resp_i = true;
            transid = l1i_req.trans_id;
        }
        if(l1d_req.lindex == lindex && l1d_req.type) {
            resp_d = true;
            if(l1d_req.type == L1REQ_GETM) {
                req = L1REQ_GETM;
            }
            transid = l1d_req.trans_id;
            if(resp_i) {
                if(trace) trace->cancel_transaction(l1i_req.trans_id);
                l1i_req.trans_id = transid;
            }
        }

        if(log_info) {
            sprintf(log_buf, "%s: Handle from L1: @0x%lx, %s %s, %d", logname.c_str(), lindex, resp_i?"i":"", resp_d?"d":"", req);
            simroot::print_log_info(log_buf);
        }

        if((resp_i || resp_d) && req == L1REQ_GETS) {
            TagedCacheLine *pline = nullptr;
            MSHREntry *mshr = nullptr;
            if(block->get_line(lindex, &pline, true)) {
                if(trace) trace->insert_event(transid, CacheEvent::L2_HIT);
                if(resp_i) {
                    pline->flag |= L2FLG_IN_I;
                    insert_to_l1i(lindex, pline->data);
                }
                if(resp_d) {
                    pline->flag |= L2FLG_IN_D;
                    insert_to_l1d(lindex, pline->data, false);
                }
                statistic.l2_hit_count ++;
            }
            else if(mshr = mshrs->get(lindex)) {
                if(trace) trace->insert_event(transid, CacheEvent::L2_HIT);
                if(mshr->state == MSHR_OTOM || mshr->state == MSHR_STOM) {
                    if(resp_i) {
                        mshr->line_flag |= L2FLG_IN_I;
                        insert_to_l1i(lindex, mshr->line_buf);
                    }
                    if(resp_d) {
                        mshr->line_flag |= L2FLG_IN_D;
                        insert_to_l1d(lindex, mshr->line_buf, false);
                    }
                }
                else {
                    if(resp_i) mshr->finish_flag |= MSHR_FIFLG_L1IREQ;
                    if(resp_d) mshr->finish_flag |= MSHR_FIFLG_L1DREQS;
                }
                statistic.l2_hit_count ++;
            }
            else if(mshr = mshrs->alloc(lindex)) {
                mshr->state = MSHR_ITOS;
                if(resp_i) mshr->finish_flag |= MSHR_FIFLG_L1IREQ;
                if(resp_d) mshr->finish_flag |= MSHR_FIFLG_L1DREQS;
                push_send_buf(hn_port, CHANNEL_REQ, MSG_GETS, lindex, my_port_id, transid);
                statistic.l2_miss_count ++;
                if(trace) trace->insert_event(transid, CacheEvent::L2_MISS);
            }
            else {
                if(resp_i) l1i_req.indexing = false;
                if(resp_d) l1d_req.indexing = false;
            }
        }
        else if(resp_d && req == L1REQ_GETM) {
            TagedCacheLine *pline = nullptr;
            MSHREntry *mshr = nullptr;
            bool is_miss = true;
            if(block->get_line(lindex, &pline, true)) {
                if(pline->state == CC_EXCLUSIVE || pline->state == CC_MODIFIED) {
                    if(trace) trace->insert_event(transid, CacheEvent::L2_HIT);
                    pline->flag |= L2FLG_IN_D;
                    pline->flag |= L2FLG_IN_D_W;
                    insert_to_l1d(lindex, pline->data, true);
                    is_miss = false;
                }
            }
            else if(mshr = mshrs->get(lindex)) {
                if(mshr->state == MSHR_STOM || mshr->state == MSHR_OTOM || mshr->state == MSHR_ITOM) {
                    mshr->line_flag |= L2FLG_IN_D;
                    mshr->line_flag |= L2FLG_IN_D_W;
                    mshr->finish_flag |= MSHR_FIFLG_L1DREQM;
                    if(trace) trace->insert_event(transid, CacheEvent::L2_HIT);
                }
                else {
                    l1d_req.indexing = false;
                }
                is_miss = false;
            }
            if(is_miss && (mshr = mshrs->alloc(lindex))) {
                if(pline) {
                    cache_line_copy(mshr->line_buf, pline->data);
                    mshr->line_flag = (pline->flag | L2FLG_IN_D | L2FLG_IN_D_W);
                    mshr->finish_flag = MSHR_FIFLG_L1DREQM;
                    switch (pline->state)
                    {
                    case CC_SHARED: mshr->state = MSHR_STOM; break;
                    case CC_OWNED : mshr->state = MSHR_OTOM; break;
                    default: simroot_assert(0);
                    }
                    block->remove_line(lindex);
                }
                else {
                    mshr->line_flag = (L2FLG_IN_D | L2FLG_IN_D_W);
                    mshr->finish_flag = MSHR_FIFLG_L1DREQM;
                    mshr->state = MSHR_ITOM;
                }
                push_send_buf(hn_port, CHANNEL_REQ, MSG_GETM, lindex, my_port_id, transid);
                statistic.l2_miss_count ++;
                if(trace) trace->insert_event(transid, CacheEvent::L2_MISS);
            }
            else if(is_miss) {
                l1d_req.indexing = false;
            }
        }
    }

    processing_line.erase(pak->lindex);
    block->unpin(pak->lindex);
    if(pak->msg) delete pak->msg;
    delete pak;
}

void PrivL1L2Moesi::handle_new_line_nolock(LineIndexT lindex, MSHREntry *mshr, uint32_t init_state) {

    TagedCacheLine newline, replacedline;
    LineIndexT replaced = 0;
    cache_line_copy(newline.data, mshr->line_buf);
    newline.state = init_state;
    newline.flag = mshr->line_flag;

    if(log_info) {
        sprintf(log_buf, "%s: New Line: @0x%lx", logname.c_str(), lindex);
        simroot::print_log_info(log_buf);
    }

    if(init_state == CC_MODIFIED && ((mshr->finish_flag & MSHR_FIFLG_L1DREQM) || (mshr->line_flag & L2FLG_IN_D))) {
        if(log_info) {
            sprintf(log_buf, "%s: Sync to l1d(M): @0x%lx", logname.c_str(), lindex);
            simroot::print_log_info(log_buf);
        }
        newline.flag |= (L2FLG_IN_D | L2FLG_IN_D_W);
        insert_to_l1d(lindex, mshr->line_buf, true);
    }
    else if((mshr->line_flag & L2FLG_IN_D) || (mshr->finish_flag & MSHR_FIFLG_L1DREQS)) {
        if(log_info) {
            sprintf(log_buf, "%s: Sync to l1d(S): @0x%lx", logname.c_str(), lindex);
            simroot::print_log_info(log_buf);
        }
        newline.flag |= L2FLG_IN_D;
        insert_to_l1d(lindex, mshr->line_buf, false);
    }
    if((mshr->line_flag & L2FLG_IN_I) || (mshr->finish_flag & MSHR_FIFLG_L1IREQ)) {
        if(log_info) {
            sprintf(log_buf, "%s: Sync to l1i(S): @0x%lx", logname.c_str(), lindex);
            simroot::print_log_info(log_buf);
        }
        newline.flag |= L2FLG_IN_I;
        insert_to_l1i(lindex, mshr->line_buf);
    }

    if(!block->insert_line(lindex, &newline, &replaced, &replacedline)) {
        mshrs->remove(lindex);
        return ;
    }

    if(log_info) {
        sprintf(log_buf, "%s: L2 Replaced @0x%lx %s %s%s%s: ", logname.c_str(), replaced,
        get_cache_state_name_str(replacedline.state).c_str() ,
        (replacedline.flag & L2FLG_IN_I)?"I":"", (replacedline.flag & L2FLG_IN_D)?"D":"", (replacedline.flag & L2FLG_IN_D_W)?"W":""
        );
        string str = log_buf;
        for(uint32_t i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
            sprintf(log_buf, "%02x ", ((char*)(replacedline.data))[i]);
            str += log_buf;
        }
        simroot::print_log_info(str);
    }

    mshrs->remove(lindex);
    simroot_assert(mshr = mshrs->alloc(replaced));

    if(replacedline.flag & L2FLG_IN_I) {
        l1i_block->remove_line(replaced);
        l1i_busy_cycle++;
    }
    if(replacedline.flag & L2FLG_IN_D) {
        TagedCacheLine *pline = nullptr;
        simroot_assert(l1d_block->get_line(replaced, &pline, false));
        if(pline->flag & L1FLG_DIRTY) {
            cache_line_copy(replacedline.data, pline->data);
            simroot_assert(replacedline.state == CC_MODIFIED || replacedline.state == CC_EXCLUSIVE);
            replacedline.state = CC_MODIFIED;
        }
        l1d_block->remove_line(replaced);
        if(reserved_address_valid && replaced == addr_to_line_index(reserved_address)) {
            reserved_address_valid = false;
        }
        l1d_busy_cycle++;
    }

    cache_line_copy(mshr->line_buf, replacedline.data);

    BusPortT hn_port = 0;
    simroot_assert(busmap->get_homenode_port(replaced, &hn_port));

    switch (replacedline.state)
    {
    case CC_EXCLUSIVE:
        mshr->state = MSHR_ETOI;
        push_send_buf(hn_port, CHANNEL_REQ, MSG_PUTE, replaced, my_port_id, (trace?(trace->alloc_trans_id()):0));
        break;
    case CC_MODIFIED:
        mshr->state = MSHR_MTOI;
        push_send_buf_with_line(hn_port, CHANNEL_REQ, MSG_PUTM, replaced, my_port_id, replacedline.data, (trace?(trace->alloc_trans_id()):0));
        break;
    case CC_SHARED:
        mshr->state = MSHR_STOI;
        push_send_buf(hn_port, CHANNEL_REQ, MSG_PUTS, replaced, my_port_id, (trace?(trace->alloc_trans_id()):0));
        break;
    case CC_OWNED:
        mshr->state = MSHR_OTOI;
        push_send_buf_with_line(hn_port, CHANNEL_REQ, MSG_PUTO, replaced, my_port_id, replacedline.data, (trace?(trace->alloc_trans_id()):0));
        break;
    default:
        simroot_assert(0);
    }

}

void PrivL1L2Moesi::insert_to_l1i(LineIndexT lindex, void * line_buf) {
    TagedCacheLine newline, replacedline;
    LineIndexT replaced = 0;

    l1i_busy_cycle ++;
    l1i_newlines.emplace_back();
    l1i_newlines.back().lindex = lindex;
    l1i_newlines.back().readonly = true;
    cache_line_copy(l1i_newlines.back().data.data(), line_buf);

    if(l1i_req.lindex == lindex) {
        l1i_req.type = l1i_req.lindex = l1i_req.indexing = 0;
        if(trace) trace->insert_event(l1i_req.trans_id, CacheEvent::L1_FINISH);
    }

    newline.flag = newline.state = 0;
    cache_line_copy(newline.data, line_buf);
    if(!l1i_block->insert_line(lindex, &newline, &replaced, &replacedline)) {
        return;
    }

    if(log_info) {
        sprintf(log_buf, "%s: L1I Replaced @0x%lx: ", logname.c_str(), replaced);
        string str = log_buf;
        for(uint32_t i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
            sprintf(log_buf, "%02x ", ((char*)(replacedline.data))[i]);
            str += log_buf;
        }
        simroot::print_log_info(str);
    }

    TagedCacheLine *pline = nullptr;
    MSHREntry *mshr = nullptr;

    if(block->get_line(replaced, &pline, true)) {
        pline->flag &= (~L2FLG_IN_I);
    }
    if(mshr = mshrs->get(replaced)) {
        mshr->line_flag &= (~L2FLG_IN_I);
    }
}

void PrivL1L2Moesi::insert_to_l1d(LineIndexT lindex, void * line_buf, bool writable) {
    TagedCacheLine newline, replacedline;
    LineIndexT replaced = 0;

    l1d_busy_cycle ++;
    l1d_newlines.emplace_back();
    l1d_newlines.back().lindex = lindex;
    l1d_newlines.back().readonly = (!writable);
    cache_line_copy(l1d_newlines.back().data.data(), line_buf);

    if(l1d_req.lindex == lindex) {
        if(l1d_req.type == L1REQ_GETM && !writable) {
            l1d_req.indexing = 0;
        }
        else {
            l1d_req.type = l1d_req.lindex = l1d_req.indexing = 0;
            if(trace) trace->insert_event(l1d_req.trans_id, CacheEvent::L1_FINISH);
        }
    }

    newline.flag = (writable?L1FLG_WRITE:0);
    newline.state = 0;
    cache_line_copy(newline.data, line_buf);
    if(!l1d_block->insert_line(lindex, &newline, &replaced, &replacedline)) {
        return;
    }

    if(log_info) {
        sprintf(log_buf, "%s: L1D Replaced @0x%lx %d: ", logname.c_str(), replaced, replacedline.flag);
        string str = log_buf;
        for(uint32_t i = 0; i < CACHE_LINE_LEN_BYTE; i++) {
            sprintf(log_buf, "%02x ", ((char*)(replacedline.data))[i]);
            str += log_buf;
        }
        simroot::print_log_info(str);
    }

    if(reserved_address_valid && replaced == addr_to_line_index(reserved_address)) {
        reserved_address_valid = false;
    }

    TagedCacheLine *pline = nullptr;
    MSHREntry *mshr = nullptr;

    if(block->get_line(replaced, &pline, true)) {
        pline->flag &= (~L2FLG_IN_D);
        pline->flag &= (~L2FLG_IN_D_W);
        if(replacedline.flag & L1FLG_DIRTY) {
            cache_line_copy(pline->data, replacedline.data);
            simroot_assert(pline->state == CC_MODIFIED || pline->state == CC_EXCLUSIVE);
            pline->state = CC_MODIFIED;
        }
    }
    if(mshr = mshrs->get(replaced)) {
        mshr->line_flag &= (~L2FLG_IN_I);
        pline->flag &= (~L2FLG_IN_D_W);
        if(replacedline.flag & L1FLG_DIRTY) {
            simroot_assert(0); // M状态的行不可能直接进入MSHR
            cache_line_copy(mshr->line_buf, replacedline.data);
        }
    }
}

void PrivL1L2Moesi::snoop_l1d_and_set_readonly(LineIndexT lindex, void * l2_line_buf) {
    TagedCacheLine *pline = nullptr;
    if(l1d_block->get_line(lindex, &pline, false)) {
        if(pline->flag & L1FLG_DIRTY) cache_line_copy(l2_line_buf, pline->data);
        pline->flag &= (~L1FLG_DIRTY);
        pline->flag &= (~L1FLG_WRITE);
    }
    l1d_busy_cycle ++;
    if(reserved_address_valid && lindex == addr_to_line_index(reserved_address)) {
        reserved_address_valid = false;
    }
}

void PrivL1L2Moesi::snoop_l1_and_invalid(LineIndexT lindex, void * l2_line_buf) {
    l1i_block->remove_line(lindex);
    TagedCacheLine *pline = nullptr;
    if(l1d_block->get_line(lindex, &pline, false)) {
        if(pline->flag & L1FLG_DIRTY) cache_line_copy(l2_line_buf, pline->data);
        l1d_block->remove_line(lindex);
    }
    l1d_busy_cycle ++;
    l1i_busy_cycle ++;
    if(reserved_address_valid && lindex == addr_to_line_index(reserved_address)) {
        reserved_address_valid = false;
    }
}

void PrivL1L2Moesi::main_on_current_tick() {
    if(!is_main_cur) {
        return;
    }
    is_main_cur = false;

    cur_recieve_msg();

    p1_fetch();
    p2_index();

    cur_send_msg();

    if(l1i_busy_cycle) {
        l1i_busy_cycle--;
    }
    else {
        uint32_t qcnt = l1i_ld_queues.size();
        for(uint32_t i = 1; i+1 < qcnt; i++) {
            for(uint32_t j = 0; j < l1i_query_width; j++) {
                l1i_ld_queues[i].pass_to(l1i_ld_queues[i+1]);
            }
        }
        for(uint32_t busy = 0;busy < l1i_query_width; busy++) {
            if(l1i_ld_queues[0].can_pop()) {
                CacheOP *op = l1i_ld_queues[0].top();
                op->err = l1i_load(op->addr, op->len, op->data.data(), op->valid);
                simroot_assert(l1i_ld_queues[0].pass_to(l1i_ld_queues[1]));
            }
            else {
                break;
            }
        }
    }

    if(l1d_busy_cycle) {
        l1d_busy_cycle--;
    }
    else {
        uint32_t qcnt = l1d_ld_queues.size();
        for(uint32_t i = 1; i+1 < qcnt; i++) {
            for(uint32_t j = 0; j < l1d_query_width; j++) {
                l1d_ld_queues[i].pass_to(l1d_ld_queues[i+1]);
                l1d_st_queues[i].pass_to(l1d_st_queues[i+1]);
                l1d_amo_queues[i].pass_to(l1d_amo_queues[i+1]);
            }
        }
        for(uint32_t busy = 0;busy < l1d_query_width; busy++) {
            if(l1d_st_queues[0].can_pop()) {
                CacheOP *op = l1d_st_queues[0].top();
                op->err = l1d_store(op->addr, op->len, op->data.data(), op->valid);
                simroot_assert(l1d_st_queues[0].pass_to(l1d_st_queues[1]));
            }
            else if(l1d_ld_queues[0].can_pop()) {
                CacheOP *op = l1d_ld_queues[0].top();
                op->err = l1d_load(op->addr, op->len, op->data.data(), op->valid);
                simroot_assert(l1d_ld_queues[0].pass_to(l1d_ld_queues[1]));
            }
            else if(l1d_amo_queues[0].can_pop()) {
                CacheOP *op = l1d_amo_queues[0].top();
                op->err = l1d_amo(op->addr, op->len, op->data.data(), op->amo);
                simroot_assert(l1d_amo_queues[0].pass_to(l1d_amo_queues[1]));
            }
            else {
                break;
            }
        }
    }
}

void PrivL1L2Moesi::main_apply_next_tick() {
    if(is_main_cur) {
        return;
    }
    is_main_cur = true;

    queue_index->apply_next_tick();

    for(auto &q : l1d_ld_queues) {
        q.apply_next_tick();
    }
    for(auto &q : l1d_st_queues) {
        q.apply_next_tick();
    }
    for(auto &q : l1d_amo_queues) {
        q.apply_next_tick();
    }
    for(auto &q : l1i_ld_queues) {
        q.apply_next_tick();
    }
}

#define LOGTOFILE(fmt, ...) do{sprintf(log_buf, fmt, ##__VA_ARGS__);ofile << log_buf;}while(0)

void PrivL1L2Moesi::print_statistic(std::ofstream &ofile) {
    #define PIPELINE_5_GENERATE_PRINTSTATISTIC(n) ofile << #n << ": " << statistic.n << "\n" ;
    PIPELINE_5_GENERATE_PRINTSTATISTIC(l1i_hit_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(l1i_miss_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(l1d_hit_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(l1d_miss_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(l2_hit_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(l2_miss_count)
}

void PrivL1L2Moesi::print_setup_info(std::ofstream &ofile) {
    LOGTOFILE("port_id: %d\n", my_port_id);
    LOGTOFILE("l1i_way_count: %d\n", l1i_param.way_cnt);
    LOGTOFILE("l1i_set_count: %d\n", 1 << l1i_param.set_offset);
    LOGTOFILE("l1d_way_count: %d\n", l1d_param.way_cnt);
    LOGTOFILE("l1d_set_count: %d\n", 1 << l1d_param.set_offset);
    LOGTOFILE("l2_way_count: %d\n", l2_param.way_cnt);
    LOGTOFILE("l2_set_count: %d\n", 1 << l2_param.set_offset);
    LOGTOFILE("l2_mshr_count: %d\n", l2_param.mshr_num);
    LOGTOFILE("l2_index_width: %d\n", l2_param.index_width);
    LOGTOFILE("l2_index_latency: %d\n", l2_param.index_latency);
}

}}

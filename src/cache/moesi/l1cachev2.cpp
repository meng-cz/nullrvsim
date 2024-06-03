
#include "l1cachev2.h"

#include "common.h"
#include "simroot.h"
#include "configuration.h"

namespace simcache {
namespace moesi {

L1CacheMoesiDirNoiV2::L1CacheMoesiDirNoiV2(
    uint32_t set_addr_offset,
    uint32_t line_per_set,
    uint32_t mshr_num,
    uint32_t query_cycle,
    uint32_t query_width,
    BusInterface *bus,
    BusPortT my_port_id,
    BusPortMapping *busmap,
    string logname
) : mshrs(mshr_num), block(set_addr_offset, line_per_set),
bus(bus), my_port_id(my_port_id), busmap(busmap), log_name(logname), query_width(query_width) {
    do_on_current_tick = 4;
    do_apply_next_tick = 1;
    ld_queues.resize(query_cycle, SimpleTickQueue<CacheOP*>(query_width, query_width, 0));
    st_queues.resize(query_cycle, SimpleTickQueue<CacheOP*>(query_width, query_width, 0));
    amo_queues.resize(query_cycle, SimpleTickQueue<CacheOP*>(query_width, query_width, 0));
    ld_input = std::make_unique<SimpleTickQueue<CacheOP*>>(query_width, query_width, 0);
    ld_output = std::make_unique<SimpleTickQueue<CacheOP*>>(query_width, query_width, 0);
    st_input = std::make_unique<SimpleTickQueue<CacheOP*>>(query_width, query_width, 0);
    st_output = std::make_unique<SimpleTickQueue<CacheOP*>>(query_width, query_width, 0);
    amo_input = std::make_unique<SimpleTickQueue<CacheOP*>>(query_width, query_width, 0);
    amo_output = std::make_unique<SimpleTickQueue<CacheOP*>>(query_width, query_width, 0);
    misc_input = std::make_unique<SimpleTickQueue<CacheOP*>>(query_width, query_width, 0);
    misc_output = std::make_unique<SimpleTickQueue<CacheOP*>>(query_width, query_width, 0);
}

SimError L1CacheMoesiDirNoiV2::load(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = addr_to_line_index(paddr);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));

    BusPortT l2_port = 0;
    if(!busmap->get_uplink_port(lindex, &l2_port)) return SimError::invalidaddr;

    recieve_msg_nolock();

    DefaultCacheLine *p_line = nullptr;
    if(block.get_line(lindex, &p_line, true)) {
        if(debug_log) {
            printf("l1 load: fetch line @0x%lx:", lindex);
            for(int i = 0; i < CACHE_LINE_LEN_I64; i++) {
                printf(" %16lx", p_line->data[i]);
            }
            printf("\n");
        }
        if(len) {
            if(valid.size() != len) memcpy(buf, ((uint8_t*)(p_line->data)) + offset, len);
            else {
                for(int i = 0; i < len; i++) {
                    if(valid[i]) ((uint8_t*)buf)[i] = ((uint8_t*)(p_line->data))[i + offset];
                }
            }
        }
        statistic.l1_hit_count++;
        return SimError::success;
    }

    MSHREntry *mshr = mshrs.get(lindex);
    if(mshr) {
        switch (mshr->state)
        {
        case CacheMSHRState::stom:
        case CacheMSHRState::otom:
            if(len) {
                if(valid.size() != len) memcpy(buf, ((uint8_t*)(mshr->line_buf)) + offset, len);
                else {
                    for(int i = 0; i < len; i++) {
                        if(valid[i]) ((uint8_t*)buf)[i] = ((uint8_t*)(mshr->line_buf))[i + offset];
                    }
                }
            }
            if(debug_log) {
                printf("l1 load: fetch line in mshr @0x%lx:", lindex);
                for(int i = 0; i < CACHE_LINE_LEN_I64; i++) {
                    printf(" %16lx", p_line->data[i]);
                }
                printf("\n");
            }
            statistic.l1_hit_count++;
            return SimError::success;
        case CacheMSHRState::itom:
        case CacheMSHRState::itos:
            return SimError::miss;
        default:
            return SimError::coherence;
        }
    }

    if(send_buf.size() + 1 <= send_buf_size && (mshr = mshrs.alloc(lindex))) {
        push_send_buf(l2_port, CCMsgType::gets, lindex, my_port_id);

        mshr->state = CacheMSHRState::itos;
        mshr->log_start_cycle = simroot::get_current_tick();
        statistic.l1_miss_count++;
        return SimError::miss;
    }

    return SimError::busy;
}

SimError L1CacheMoesiDirNoiV2::store(PhysAddrT paddr, uint32_t len, void *buf, vector<bool> &valid) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = addr_to_line_index(paddr);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));

    BusPortT l2_port = 0;
    if(!busmap->get_uplink_port(lindex, &l2_port)) return SimError::invalidaddr;

    recieve_msg_nolock();

    DefaultCacheLine *p_line = nullptr;
    bool hit = block.get_line(lindex, &p_line, true);
    MSHREntry *mshr = nullptr;

    if(hit && (p_line->state == CacheLineState::exclusive || p_line->state == CacheLineState::modified)) {
        if(len) {
            if(valid.size() != len) memcpy(((uint8_t*)(p_line->data)) + offset, buf, len);
            else {
                for(int i = 0; i < len; i++) {
                    if(valid[i]) ((uint8_t*)(p_line->data))[i + offset] = ((uint8_t*)buf)[i];
                }
            }
        }
        if(debug_log) {
            printf("l1 store: fetch line @0x%lx:", lindex);
            for(int i = 0; i < CACHE_LINE_LEN_I64; i++) {
                printf(" %16lx", p_line->data[i]);
            }
            printf("\n");
        }
        p_line->state = CacheLineState::modified;
        reserved_address_valid = false;
        statistic.l1_hit_count++;
        return SimError::success;
    }

    if(mshr = mshrs.get(lindex)) {
        switch (mshr->state)
        {
        case CacheMSHRState::stom:
        case CacheMSHRState::otom:
        case CacheMSHRState::itom:
            return SimError::miss;
        default:
            return SimError::coherence;
        }
    }

    if(send_buf.size() + 1 <= send_buf_size && (mshr = mshrs.alloc(lindex))) {
        push_send_buf(l2_port, CCMsgType::getm, lindex, my_port_id);

        if(hit) {
            cache_line_copy(mshr->line_buf, p_line->data);
            switch (p_line->state)
            {
            case CacheLineState::shared: mshr->state = CacheMSHRState::stom; break;
            case CacheLineState::owned : mshr->state = CacheMSHRState::otom; break;
            default: assert(0);
            }
            block.remove_line(lindex);
        }
        else {
            mshr->state = CacheMSHRState::itom;
        }

        mshr->log_start_cycle = simroot::get_current_tick();
        statistic.l1_miss_count++;
        return SimError::miss;
    }

    return SimError::busy;
}

SimError L1CacheMoesiDirNoiV2::load_reserved(PhysAddrT paddr, uint32_t len, void *buf) {
    SimError ret = load(paddr, len, buf);
    if(ret == SimError::success) {
        reserved_address_valid = true;
        reserved_address = paddr;
    }
    return ret;
}

SimError L1CacheMoesiDirNoiV2::store_conditional(PhysAddrT paddr, uint32_t len, void *buf) {
    bool conditional = false;
    conditional = (reserved_address_valid && (reserved_address == paddr));
    SimError ret = SimError::unconditional;
    if(conditional) ret = store(paddr, len, buf);
    return ret;
}

SimError L1CacheMoesiDirNoiV2::amo(PhysAddrT paddr, uint32_t len, void *buf, isa::RV64AMOOP5 amoop) {
    if(amoop == isa::RV64AMOOP5::SC) {
        return store_conditional(paddr, len, buf);
    }
    else if(amoop == isa::RV64AMOOP5::LR) {
        return load_reserved(paddr, len, buf);
    }
    else {
        IntDataT previous = 0;
        IntDataT value = 0;
        IntDataT stvalue = 0;
        isa::RV64AMOParam param;
        param.op = amoop;
        SimError res = store(paddr, 0, &stvalue);
        if(res != SimError::success) {
            return res;
        }
        res = SimError::miss;
        if(len == 4) {
            int32_t tmp = 0;
            res = load(paddr, 4, &tmp);
            RAW_DATA_AS(previous).i64 = tmp;
            RAW_DATA_AS(value).i64 = *((int32_t*)buf);
            param.wid = isa::RV64LSWidth::word;
        }
        else {
            res = load(paddr, 8, &previous);
            value = *((uint64_t*)buf);
            param.wid = isa::RV64LSWidth::dword;
        }
        if(res != SimError::success) {
            return res;
        }
        res = isa::perform_amo_op(param, &stvalue, previous, value);
        if(res == SimError::success) {
            res = store(paddr, len, &stvalue);
            if(res == SimError::success) {
                memcpy(buf, &previous, len);
            }
        }
        return res;
    }
}

void L1CacheMoesiDirNoiV2::recieve_msg_nolock() {
    if(!has_recieved && bus->recv(my_port_id, &msgbuf)) {
        has_recieved = true;
    }
}

void L1CacheMoesiDirNoiV2::handle_received_msg_nolock() {
    if(!has_recieved || has_processed) {
        return;
    }

    LineIndexT lindex = msgbuf.line;
    uint32_t arg = msgbuf.arg;

    BusPortT l2_port = 0;
    assert(busmap->get_uplink_port(lindex, &l2_port));

    if(msgbuf.type == CCMsgType::invalid) {
        if(send_buf.size() + 1 > send_buf_size) return;
        push_send_buf(arg, CCMsgType::invalid_ack, lindex, 0);
        block.remove_line(lindex);
        MSHREntry *mshr = nullptr;
        if((mshr = mshrs.get(lindex))) {
            switch (mshr->state)
            {
            case CacheMSHRState::stoi:
            case CacheMSHRState::mtoi:
            case CacheMSHRState::otoi:
                mshr->state = CacheMSHRState::itoi;
                break;
            case CacheMSHRState::otom:
            case CacheMSHRState::stom:
            case CacheMSHRState::itom:
                mshr->state = CacheMSHRState::itom;
                break;
            default:
                simroot_assert(0);
            }
        }
        if(reserved_address_valid && lindex == addr_to_line_index(reserved_address)) {
            reserved_address_valid = false;
        }
    }
    else if(msgbuf.type == CCMsgType::invalid_ack) {
        bool getm_finished = false;
        MSHREntry *mshr = nullptr;
        simroot_assert(mshr = mshrs.get(lindex));
        if(mshr->state == CacheMSHRState::itom) {
            if(mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack + 1 || mshr->get_data_ready == 0) {
                mshr->invalid_ack++;
            }
            else {
                getm_finished = true;
            }
        }
        else if(mshr->state == CacheMSHRState::stom || mshr->state == CacheMSHRState::otom) {
            if(mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack + 1) {
                mshr->invalid_ack++;
            }
            else {
                getm_finished = true;
            }
        }
        if(getm_finished) {
            if(send_buf.size() + 2 > send_buf_size) return;
            push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
            handle_new_line_nolock(lindex, mshr, CacheLineState::modified);
        }
    }
    else if(msgbuf.type == CCMsgType::gets_forward) {
        if(send_buf.size() + 1 > send_buf_size) return;
        DefaultCacheLine *p_line = nullptr;
        bool hit = block.get_line(lindex, &p_line);
        MSHREntry *mshr = mshrs.get(lindex);
        bool mshr_handle = (mshr && (
            mshr->state == CacheMSHRState::stom || 
            mshr->state == CacheMSHRState::mtoi || 
            mshr->state == CacheMSHRState::stoi || 
            mshr->state == CacheMSHRState::etoi || 
            mshr->state == CacheMSHRState::otom || 
            mshr->state == CacheMSHRState::otoi
        ));
        simroot_assert(hit || mshr_handle);
        simroot_assert(!hit || !mshr_handle);
        if(hit) {
            push_send_buf_with_line(arg, CCMsgType::gets_resp, lindex, 1, p_line->data);
            p_line->state = CacheLineState::owned;
        }
        else if(mshr_handle) {
            push_send_buf_with_line(arg, CCMsgType::gets_resp, lindex, 1, mshr->line_buf);
            if(mshr->state == CacheMSHRState::mtoi || mshr->state == CacheMSHRState::etoi || mshr->state == CacheMSHRState::stoi) {
                mshr->state = CacheMSHRState::otoi;
            }
        }
    }
    else if(msgbuf.type == CCMsgType::getm_forward) {
        if(send_buf.size() + 1 > send_buf_size) return;
        DefaultCacheLine *p_line = nullptr;
        bool hit = block.get_line(lindex, &p_line);
        MSHREntry *mshr = mshrs.get(lindex);
        bool mshr_handle = (mshr && (
            mshr->state == CacheMSHRState::stom || 
            mshr->state == CacheMSHRState::mtoi || 
            mshr->state == CacheMSHRState::stoi || 
            mshr->state == CacheMSHRState::etoi || 
            mshr->state == CacheMSHRState::otom || 
            mshr->state == CacheMSHRState::otoi
        ));
        simroot_assert(hit || mshr_handle);
        simroot_assert(!hit || !mshr_handle);
        if(hit) {
            push_send_buf_with_line(arg, CCMsgType::getm_resp, lindex, 0, p_line->data);
            block.remove_line(lindex);
        }
        else if(mshr_handle) {
            push_send_buf_with_line(arg, CCMsgType::getm_resp, lindex, 0, mshr->line_buf);
            switch (mshr->state)
            {
            case CacheMSHRState::stom:
            case CacheMSHRState::otom:
                mshr->state = CacheMSHRState::itom;
                break;
            case CacheMSHRState::stoi:
            case CacheMSHRState::mtoi:
            case CacheMSHRState::etoi:
            case CacheMSHRState::otoi:
                mshr->state = CacheMSHRState::itoi;
                break;
            }
        }
    }
    else if(msgbuf.type == CCMsgType::getm_ack) {
        bool getm_finished = false;
        MSHREntry *mshr = nullptr;
        simroot_assert(mshr = mshrs.get(lindex));
        if(mshr->state == CacheMSHRState::itom) {
            if(arg != mshr->invalid_ack || mshr->get_data_ready == 0) {
                mshr->get_ack_cnt_ready = 1;
                mshr->need_invalid_ack = arg;
            }
            else {
                getm_finished = true;
            }
        }
        else if(mshr->state == CacheMSHRState::stom || mshr->state == CacheMSHRState::otom) {
            if(arg != mshr->invalid_ack) {
                mshr->get_ack_cnt_ready = 1;
                mshr->need_invalid_ack = arg;
            }
            else {
                getm_finished = true;
            }
        }
        if(getm_finished) {
            if(send_buf.size() + 2 > send_buf_size) return;
            push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
            handle_new_line_nolock(lindex, mshr, CacheLineState::modified);
        }
    }
    else if(msgbuf.type == CCMsgType::gets_resp) {
        if(send_buf.size() + 2 > send_buf_size) return;
        MSHREntry *mshr = nullptr;
        simroot_assert((mshr = mshrs.get(lindex)) && mshr->state == CacheMSHRState::itos);
        cache_line_copy(mshr->line_buf, msgbuf.data.data());
        if(arg > 0) {
            push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
        }
        handle_new_line_nolock(lindex, mshr, (arg > 0)?(CacheLineState::shared):(CacheLineState::exclusive));
    }
    else if(msgbuf.type == CCMsgType::getm_resp) {
        bool getm_finished = false;
        MSHREntry *mshr = nullptr;
        simroot_assert((mshr = mshrs.get(lindex)) && mshr->state == CacheMSHRState::itom);
        if(arg == 0 && (mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack)) {
            cache_line_copy(mshr->line_buf, msgbuf.data.data());
            mshr->get_data_ready = 1;
        }
        else {
            if(send_buf.size() + 2 > send_buf_size) return;
            if(arg == 0) push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
            cache_line_copy(mshr->line_buf, msgbuf.data.data());
            handle_new_line_nolock(lindex, mshr, CacheLineState::modified);
        }
    }
    else if(msgbuf.type == CCMsgType::get_resp_mem) {
        if(send_buf.size() + 2 > send_buf_size) return;
        push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
        MSHREntry *mshr = nullptr;
        simroot_assert(mshr = mshrs.get(lindex));
        cache_line_copy(mshr->line_buf, msgbuf.data.data());
        if(mshr->state == CacheMSHRState::itom) {
            handle_new_line_nolock(lindex, mshr, CacheLineState::modified);
        }
        else if(mshr->state == CacheMSHRState::itos) {
            handle_new_line_nolock(lindex, mshr, CacheLineState::exclusive);
        }
        else {
            simroot_assert(0);
        }
    }
    else if(msgbuf.type == CCMsgType::put_ack) {
        MSHREntry *mshr = nullptr;
        simroot_assert(mshr = mshrs.get(lindex));
        if(mshr->state == CacheMSHRState::itoi || 
            mshr->state == CacheMSHRState::mtoi || 
            mshr->state == CacheMSHRState::stoi || 
            mshr->state == CacheMSHRState::etoi || 
            mshr->state == CacheMSHRState::otoi
        ) {
            mshrs.remove(lindex);
        }
        else {
            simroot_assert(0);
        }
    }

    has_processed = true;
    return;
}

void L1CacheMoesiDirNoiV2::send_msg_nolock() {
    if(send_buf.empty()) {
        return;
    }
    if(!bus->can_send(my_port_id)) {
        return;
    }

    assert(bus->send(my_port_id, send_buf.front().first, send_buf.front().second));
    send_buf.pop_front();
}

void L1CacheMoesiDirNoiV2::handle_new_line_nolock(LineIndexT lindex, MSHREntry *mshr, CacheLineState init_state) {

    if(debug_log) {
        printf("l1: get new line @0x%lx:", lindex);
        for(int i = 0; i < CACHE_LINE_LEN_I64; i++) {
            printf(" %16lx", mshr->line_buf[i]);
        }
        printf("\n");
    }

    BusPortT l2_port = 0;
    assert(busmap->get_uplink_port(lindex, &l2_port));

    newlines.emplace_back();
    newlines.back().lindex = lindex;
    newlines.back().readonly = (init_state == CacheLineState::shared);
    cache_line_copy(newlines.back().data.data(), mshr->line_buf);
    
    DefaultCacheLine newline, replacedline;
    cache_line_copy(newline.data, mshr->line_buf);
    newline.state = init_state;
    LineIndexT replaced = 0;
    if(!block.insert_line(lindex, &newline, &replaced, &replacedline)) {
        mshrs.remove(lindex);
        return ;
    }

    if(debug_log) {
        printf("l1: replace line @0x%lx:", replaced);
        for(int i = 0; i < CACHE_LINE_LEN_I64; i++) {
            printf(" %16lx", replacedline.data[i]);
        }
        printf("\n");
    }

    mshrs.remove(lindex);
    assert(mshr = mshrs.alloc(replaced));
    memcpy(mshr->line_buf, replacedline.data, CACHE_LINE_LEN_BYTE);

    assert(busmap->get_uplink_port(replaced, &l2_port));

    switch (replacedline.state)
    {
    case CacheLineState::exclusive:
        mshr->state = CacheMSHRState::etoi;
        push_send_buf(l2_port, CCMsgType::pute, replaced, my_port_id);
        break;
    case CacheLineState::modified:
        mshr->state = CacheMSHRState::mtoi;
        push_send_buf_with_line(l2_port, CCMsgType::putm, replaced, my_port_id, replacedline.data);
        break;
    case CacheLineState::shared:
        mshr->state = CacheMSHRState::stoi;
        push_send_buf(l2_port, CCMsgType::puts, replaced, my_port_id);
        break;
    case CacheLineState::owned:
        mshr->state = CacheMSHRState::otoi;
        push_send_buf_with_line(l2_port, CCMsgType::puto, replaced, my_port_id, replacedline.data);
        break;
    default:
        assert(0);
    }

    return ;
}


void L1CacheMoesiDirNoiV2::on_current_tick() {

    uint32_t busy = 0;
    uint32_t qcnt = ld_queues.size();
    for(uint32_t i = 0; i+1 < qcnt; i++) {
        for(uint32_t j = 0; j < query_width; j++) {
            ld_queues[i].pass_to(ld_queues[i+1]);
            st_queues[i].pass_to(st_queues[i+1]);
            amo_queues[i].pass_to(amo_queues[i+1]);
        }
    }
    if(qcnt > 0) {
        ld_queues[qcnt-1].pass_to(*ld_output);
        st_queues[qcnt-1].pass_to(*st_output);
        amo_queues[qcnt-1].pass_to(*amo_output);
    }
    while(misc_input->can_pop()) {
        misc_input->top()->err = SimError::success;
        assert(misc_input->pass_to(*misc_output));
    }

    recieve_msg_nolock();
    if(has_recieved) {
        handle_received_msg_nolock();
        busy++;
    }
    for(;busy < query_width; busy++) {
        if(st_input->can_pop()) {
            CacheOP *op = st_input->top();
            op->err = store(op->addr, op->len, op->data.data(), op->valid);
            if(ld_queues.empty()) {
                assert(st_input->pass_to(*st_output));
            }
            else {
                assert(st_input->pass_to(st_queues[0]));
            }
        }
        else if(ld_input->can_pop()) {
            CacheOP *op = ld_input->top();
            op->err = load(op->addr, op->len, op->data.data(), op->valid);
            if(ld_queues.empty()) {
                assert(ld_input->pass_to(*ld_output));
            }
            else {
                assert(ld_input->pass_to(ld_queues[0]));
            }
        }
        else if(amo_input->can_pop()) {
            CacheOP *op = amo_input->top();
            op->err = amo(op->addr, op->len, op->data.data(), op->amo);
            if(ld_queues.empty()) {
                assert(amo_input->pass_to(*amo_output));
            }
            else {
                assert(amo_input->pass_to(amo_queues[0]));
            }
        }
        else {
            break;
        }
    }
    send_msg_nolock();
}

void L1CacheMoesiDirNoiV2::apply_next_tick() {
    if(has_recieved && has_processed) {
        has_recieved = has_processed = false;
    }
    for(auto &q : ld_queues) {
        q.apply_next_tick();
    }
    for(auto &q : st_queues) {
        q.apply_next_tick();
    }
    for(auto &q : amo_queues) {
        q.apply_next_tick();
    }
    ld_input->apply_next_tick();
    ld_output->apply_next_tick();
    st_input->apply_next_tick();
    st_output->apply_next_tick();
    amo_input->apply_next_tick();
    amo_output->apply_next_tick();
    misc_input->apply_next_tick();
    misc_output->apply_next_tick();
    arrivals.swap(newlines);
    newlines.clear();
}

bool L1CacheMoesiDirNoiV2::is_empty() {
    bool empty = (ld_input->empty() &&
            ld_output->empty() &&
            st_input->empty() &&
            st_output->empty() &&
            amo_input->empty() &&
            amo_output->empty() &&
            misc_input->empty() &&
            misc_output->empty());
    if(!empty) return empty;
    for(auto &q : ld_queues) {
        empty = (empty && q.empty());
    }
    for(auto &q : st_queues) {
        empty = (empty && q.empty());
    }
    for(auto &q : amo_queues) {
        empty = (empty && q.empty());
    }
    return empty;
}

void L1CacheMoesiDirNoiV2::clear_ld(std::list<CacheOP*> *to_free) {
    ld_input->clear(to_free);
    ld_output->clear(to_free);
    for(auto &q : ld_queues) {
        q.clear(to_free);
    }
}

void L1CacheMoesiDirNoiV2::clear_st(std::list<CacheOP*> *to_free) {
    st_input->clear(to_free);
    st_output->clear(to_free);
    for(auto &q : st_queues) {
        q.clear(to_free);
    }
}

void L1CacheMoesiDirNoiV2::clear_amo(std::list<CacheOP*> *to_free) {
    amo_input->clear(to_free);
    amo_output->clear(to_free);
    for(auto &q : amo_queues) {
        q.clear(to_free);
    }
}

void L1CacheMoesiDirNoiV2::clear_misc(std::list<CacheOP*> *to_free) {
    misc_input->clear(to_free);
    misc_output->clear(to_free);
}


void L1CacheMoesiDirNoiV2::dump_core(std::ofstream &ofile) {
    
    for(uint32_t s = 0; s < block.set_count; s++) {
        sprintf(log_buf, "set %d: ", s);
        ofile << log_buf;
        for(auto &e : block.p_sets[s]) {
            sprintf(log_buf, "0x%lx:%s ", e.first, get_cache_state_name_str(e.second.state).c_str());
            ofile << log_buf;
        }
        ofile << "\n";
    }
    ofile << "mshr: ";
    for(auto &e : mshrs.hashmap) {
        sprintf(log_buf, "0x%lx:%s ", e.first, get_cache_mshr_state_name_str(e.second.state).c_str());
        ofile << log_buf;
    }
    ofile << "\n";
    
}

}}




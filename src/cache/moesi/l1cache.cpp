
#include "l1cache.h"

#include "common.h"
#include "simroot.h"
#include "configuration.h"

namespace simcache {
namespace moesi {

L1CacheMoesiDirNoi::L1CacheMoesiDirNoi(
    uint32_t set_addr_offset,
    uint32_t line_per_set,
    uint32_t mshr_num,
    BusInterface *bus,
    BusPortT my_port_id,
    BusPortMapping *busmap,
    string logname
) : mshrs(mshr_num), block(set_addr_offset, line_per_set),
bus(bus), my_port_id(my_port_id), busmap(busmap), log_name(logname) {
    do_on_current_tick = 4;
    do_apply_next_tick = 1;
}

SimError L1CacheMoesiDirNoi::load(PhysAddrT paddr, uint32_t len, void *buf, bool is_amo) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = addr_to_line_index(paddr);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));

    BusPortT l2_port = 0;
    if(!busmap->get_uplink_port(lindex, &l2_port)) return SimError::invalidaddr;

    lock.lock();
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
        if(len) memcpy(buf, ((uint8_t*)(p_line->data)) + offset, len);
        statistic.l1_hit_count++;
        lock.unlock();
        return SimError::success;
    }

    MSHREntry *mshr = mshrs.get(lindex);
    if(mshr) {
        switch (mshr->state)
        {
        case CacheMSHRState::stom:
        case CacheMSHRState::otom:
            if(len) memcpy(buf, ((uint8_t*)(mshr->line_buf)) + offset, len);
            if(debug_log) {
                printf("l1 load: fetch line in mshr @0x%lx:", lindex);
                for(int i = 0; i < CACHE_LINE_LEN_I64; i++) {
                    printf(" %16lx", p_line->data[i]);
                }
                printf("\n");
            }
            statistic.l1_hit_count++;
            lock.unlock();
            return SimError::success;
        case CacheMSHRState::itom:
        case CacheMSHRState::itos:
            lock.unlock();
            return SimError::miss;
        default:
            lock.unlock();
            return SimError::coherence;
        }
    }

    if(send_buf.size() + 1 <= send_buf_size && (mshr = mshrs.alloc(lindex))) {
        push_send_buf(l2_port, CCMsgType::gets, lindex, my_port_id);

        mshr->state = CacheMSHRState::itos;
        mshr->log_start_cycle = simroot::get_current_tick();
        statistic.l1_miss_count++;
        lock.unlock();
        return SimError::miss;
    }

    lock.unlock();
    return SimError::busy;
}

SimError L1CacheMoesiDirNoi::store(PhysAddrT paddr, uint32_t len, void *buf, bool is_amo) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = addr_to_line_index(paddr);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));

    BusPortT l2_port = 0;
    if(!busmap->get_uplink_port(lindex, &l2_port)) return SimError::invalidaddr;

    lock.lock();
    recieve_msg_nolock();

    DefaultCacheLine *p_line = nullptr;
    bool hit = block.get_line(lindex, &p_line, true);
    MSHREntry *mshr = nullptr;

    if(hit && (p_line->state == CacheLineState::exclusive || p_line->state == CacheLineState::modified)) {
        if(len) memcpy(((uint8_t*)(p_line->data)) + offset, buf, len);
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
        lock.unlock();
        return SimError::success;
    }

    if(mshr = mshrs.get(lindex)) {
        switch (mshr->state)
        {
        case CacheMSHRState::stom:
        case CacheMSHRState::otom:
        case CacheMSHRState::itom:
            lock.unlock();
            return SimError::miss;
        default:
            lock.unlock();
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
        lock.unlock();
        return SimError::miss;
    }

    lock.unlock();
    return SimError::busy;
}

SimError L1CacheMoesiDirNoi::load_reserved(PhysAddrT paddr, uint32_t len, void *buf) {
    SimError ret = load(paddr, len, buf);
    if(ret == SimError::success) {
        lock.lock();
        reserved_address_valid = true;
        reserved_address = paddr;
        lock.unlock();
    }
    return ret;
}

SimError L1CacheMoesiDirNoi::store_conditional(PhysAddrT paddr, uint32_t len, void *buf) {
    bool conditional = false;
    lock.lock();
    conditional = (reserved_address_valid && (reserved_address == paddr));
    lock.unlock();
    SimError ret = SimError::unconditional;
    if(conditional) ret = store(paddr, len, buf);
    return ret;
}


void L1CacheMoesiDirNoi::recieve_msg_nolock() {
    if(!has_recieved && bus->recv(my_port_id, &msgbuf)) {
        has_recieved = true;
    }
}

void L1CacheMoesiDirNoi::handle_received_msg_nolock() {
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

void L1CacheMoesiDirNoi::send_msg_nolock() {
    if(send_buf.empty()) {
        return;
    }
    if(!bus->can_send(my_port_id)) {
        return;
    }

    assert(bus->send(my_port_id, send_buf.front().first, send_buf.front().second));
    send_buf.pop_front();
}

void L1CacheMoesiDirNoi::handle_new_line_nolock(LineIndexT lindex, MSHREntry *mshr, CacheLineState init_state) {

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

uint32_t L1CacheMoesiDirNoi::arrival_line(vector<ArrivalLine> *out) {
    uint64_t ret = newlines.size();
    if(out) {
        out->swap(newlines);
    }
    newlines.clear();
    return ret;
}

void L1CacheMoesiDirNoi::on_current_tick() {
    lock.lock();

    recieve_msg_nolock();
    handle_received_msg_nolock();
    send_msg_nolock();

    lock.unlock();
}

void L1CacheMoesiDirNoi::apply_next_tick() {
    if(has_recieved && has_processed) {
        has_recieved = has_processed = false;
    }
}

void L1CacheMoesiDirNoi::dump_core(std::ofstream &ofile) {
    
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




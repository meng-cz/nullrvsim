
#include "dmaasl1.h"

#include "configuration.h"
#include "simroot.h"

namespace simcache {
namespace moesi {

DMAL1MoesiDirNoi::DMAL1MoesiDirNoi(
    BusInterface *bus,
    BusPortT my_port_id,
    BusPortMapping *busmap
) : bus(bus), my_port_id(my_port_id), busmap(busmap), mshrs(32), push_lock(16) {
    do_on_current_tick = 2;
    do_apply_next_tick = 1;
}

void DMAL1MoesiDirNoi::handle_recv_msg(CacheCohenrenceMsg &msgbuf) {
    LineIndexT lindex = msgbuf.line;
    uint32_t arg = msgbuf.arg;
    BusPortT l2_port = 0;
    assert(busmap->get_uplink_port(lindex, &l2_port));
    MSHREntry *mshr = nullptr;
    simroot_assert(mshr = mshrs.get(lindex));
    bool current_finished = false;

    if(msgbuf.type == CCMsgType::invalid) {
        push_send_buf(arg, CCMsgType::invalid_ack, lindex, 0);
    }
    else if(msgbuf.type == CCMsgType::invalid_ack) {
        simroot_assert(mshr->state == CacheMSHRState::itom);
        if(mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack + 1 || mshr->get_data_ready == 0) {
            mshr->invalid_ack++;
        }
        else {
            push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
            current_finished = true;
        }
    }
    else if(msgbuf.type == CCMsgType::gets_forward) {
        push_send_buf_with_line(arg, CCMsgType::gets_resp, lindex, 1, mshr->line_buf);
    }
    else if(msgbuf.type == CCMsgType::getm_forward) {
        push_send_buf_with_line(arg, CCMsgType::getm_resp, lindex, 0, mshr->line_buf);
    }
    else if(msgbuf.type == CCMsgType::getm_ack) {
        simroot_assert(mshr->state == CacheMSHRState::itom);
        if(arg != mshr->invalid_ack || mshr->get_data_ready == 0) {
            mshr->get_ack_cnt_ready = 1;
            mshr->need_invalid_ack = arg;
        }
        else {
            push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
            current_finished = true;
        }
    }
    else if(msgbuf.type == CCMsgType::gets_resp) {
        simroot_assert(mshr->state == CacheMSHRState::itos);
        cache_line_copy(mshr->line_buf, msgbuf.data.data());
        if(arg > 0) {
            push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
        }
        current_finished = true;
    }
    else if(msgbuf.type == CCMsgType::getm_resp) {
        simroot_assert(mshr->state == CacheMSHRState::itom);
        if(arg == 0 && (mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack)) {
            cache_line_copy(mshr->line_buf, msgbuf.data.data());
            mshr->get_data_ready = 1;
        }
        else {
            if(arg == 0) push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
            cache_line_copy(mshr->line_buf, msgbuf.data.data());
            current_finished = true;
        }
    }
    else if(msgbuf.type == CCMsgType::get_resp_mem) {
        push_send_buf(l2_port, CCMsgType::get_ack, lindex, my_port_id);
        cache_line_copy(mshr->line_buf, msgbuf.data.data());
        current_finished = true;
    }
    else if(msgbuf.type == CCMsgType::put_ack) {
        auto res = wait_lines.find(lindex);
        assert(res != wait_lines.end());
        ProcessingDMAReq *tofree = res->second;
        wait_lines.erase(res);
        mshrs.remove(lindex);
        tofree->line_wait_finish.erase(lindex);
        if(tofree->line_todo.empty() && tofree->line_wait_finish.empty()) {
            if(tofree->req.callbackid) handler->dma_complete_callback(tofree->req.callbackid);
            delete tofree;
        }
    }

    if(!current_finished) {
        return;
    }

    auto res = wait_lines.find(lindex);
    assert(res != wait_lines.end());
    ProcessingDMAReq *req = res->second;

    if(req->req.type == DMAReqType::host_memory_read) {
        VirtAddrT lineaddr = req->req.dma_pa_to_va(lindex << CACHE_LINE_ADDR_OFFSET);
        assert(lineaddr < req->req.vaddr + req->req.size);
        // printf("Get line 0x%lx\n", lindex);
        if(lineaddr >= req->req.vaddr) {
            PhysAddrT target = std::min<PhysAddrT>(lineaddr + CACHE_LINE_LEN_BYTE, req->req.vaddr + req->req.size);
            memcpy(mshr->line_buf, req->req.hostptr + (lineaddr - req->req.vaddr), target - lineaddr);
        }
        else {
            uint64_t offset = req->req.vaddr - lineaddr;
            PhysAddrT target = std::min<PhysAddrT>(lineaddr + CACHE_LINE_LEN_BYTE, req->req.vaddr + req->req.size);
            memcpy(((uint8_t*)(mshr->line_buf)) + offset, req->req.hostptr, target - (lineaddr + offset));
        }
        push_send_buf_with_line(l2_port, CCMsgType::putm, lindex, my_port_id, mshr->line_buf);
        mshr->state = CacheMSHRState::mtoi;
    }
    else {
        PhysAddrT lineaddr = req->req.dma_pa_to_va(lindex << CACHE_LINE_ADDR_OFFSET);
        assert(lineaddr < req->req.vaddr + req->req.size);
        if(lineaddr >= req->req.vaddr) {
            PhysAddrT target = std::min<PhysAddrT>(lineaddr + CACHE_LINE_LEN_BYTE, req->req.vaddr + req->req.size);
            memcpy(req->req.hostptr + (lineaddr - req->req.vaddr), mshr->line_buf, target - lineaddr);
        }
        else {
            uint64_t offset = req->req.vaddr - lineaddr;
            PhysAddrT target = std::min<PhysAddrT>(lineaddr + CACHE_LINE_LEN_BYTE, req->req.vaddr + req->req.size);
            memcpy(req->req.hostptr, ((uint8_t*)(mshr->line_buf)) + offset, target - (lineaddr + offset));
        }
        push_send_buf(l2_port, CCMsgType::puts, lindex, my_port_id);
        mshr->state = CacheMSHRState::stoi;
    }
}

void DMAL1MoesiDirNoi::on_current_tick() {
    CacheCohenrenceMsg recv;
    if(bus->recv(my_port_id, &recv)) {
        handle_recv_msg(recv);
    }

    if(current == nullptr && !dma_req_queue.empty()) {
        current = new ProcessingDMAReq(dma_req_queue.front());
        dma_req_queue.pop_front();
        for(VirtAddrT va = addr_to_line_addr(current->req.vaddr); va < current->req.vaddr + current->req.size; va += CACHE_LINE_LEN_BYTE) {
            current->line_todo.push_back(current->req.dma_va_to_pa(va) >> CACHE_LINE_ADDR_OFFSET);
        }
        assert(!current->line_todo.empty());
        // printf("Add work @0x%lx, size 0x%lx, line cnt %ld\n", current->req.vaddr, current->req.size, current->line_todo.size());
    }

    if(current) {
        if(!current->line_todo.empty()) {
            LineIndexT lindex = current->line_todo.front();
            bool mshr_exist = (mshrs.get(lindex) != nullptr);
            MSHREntry *mshr = nullptr;
            bool mshr_alloced = (mshr_exist?false:((mshr = mshrs.alloc(lindex)) != nullptr));
            if(!mshr_exist && mshr_alloced) {
                current->line_todo.pop_front();
                current->line_wait_finish.insert(lindex);
                wait_lines.emplace(lindex, current);
                BusPortT l2_port = 0;
                assert(busmap->get_uplink_port(lindex, &l2_port));
                // printf("Request line 0x%lx\n", lindex);
                if(current->req.type == DMAReqType::host_memory_read) {
                    mshr->state = CacheMSHRState::itom;
                    push_send_buf(l2_port, CCMsgType::getm, lindex, my_port_id);
                }
                else {
                    mshr->state = CacheMSHRState::itos;
                    push_send_buf(l2_port, CCMsgType::gets, lindex, my_port_id);
                }
            }
        }
        if(current->line_todo.empty()) {
            current = nullptr;
        }
    }

    if(!send_buf.empty() && bus->can_send(my_port_id)) {
        assert(bus->send(my_port_id, send_buf.front().first, send_buf.front().second));
        send_buf.pop_front();
    }
}

}}


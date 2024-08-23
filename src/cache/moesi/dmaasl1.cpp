
#include "dmaasl1.h"

#include "configuration.h"
#include "simroot.h"

namespace simcache {
namespace moesi {

DMAL1MoesiDirNoi::DMAL1MoesiDirNoi(
    BusInterfaceV2 *bus,
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
    assert(busmap->get_homenode_port(lindex, &l2_port));
    MSHREntry *mshr = nullptr;
    simroot_assert(mshr = mshrs.get(lindex));
    bool current_finished = false;

    if(msgbuf.type == MSG_INVALID) {
        push_send_buf(arg, CHANNEL_ACK, MSG_INVALID_ACK, lindex, 0);
    }
    else if(msgbuf.type == MSG_INVALID_ACK) {
        simroot_assert(mshr->state == MSHR_ITOM);
        if(mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack + 1 || mshr->get_data_ready == 0) {
            mshr->invalid_ack++;
        }
        else {
            push_send_buf(l2_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id);
            current_finished = true;
        }
    }
    else if(msgbuf.type == MSG_GETS_FORWARD) {
        push_send_buf_with_line(arg, CHANNEL_RESP, MSG_GETS_RESP, lindex, 1, mshr->line_buf);
    }
    else if(msgbuf.type == MSG_GETM_FORWARD) {
        push_send_buf_with_line(arg, CHANNEL_RESP, MSG_GETM_RESP, lindex, 0, mshr->line_buf);
    }
    else if(msgbuf.type == MSG_GETM_ACK) {
        simroot_assert(mshr->state == MSHR_ITOM);
        if(arg != mshr->invalid_ack || mshr->get_data_ready == 0) {
            mshr->get_ack_cnt_ready = 1;
            mshr->need_invalid_ack = arg;
        }
        else {
            push_send_buf(l2_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id);
            current_finished = true;
        }
    }
    else if(msgbuf.type == MSG_GETS_RESP) {
        simroot_assert(mshr->state == MSHR_ITOS);
        cache_line_copy(mshr->line_buf, msgbuf.data.data());
        if(arg > 0) {
            push_send_buf(l2_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id);
        }
        current_finished = true;
    }
    else if(msgbuf.type == MSG_GETM_RESP) {
        simroot_assert(mshr->state == MSHR_ITOM);
        if(arg == 0 && (mshr->get_ack_cnt_ready == 0 || mshr->need_invalid_ack != mshr->invalid_ack)) {
            cache_line_copy(mshr->line_buf, msgbuf.data.data());
            mshr->get_data_ready = 1;
        }
        else {
            if(arg == 0) push_send_buf(l2_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id);
            cache_line_copy(mshr->line_buf, msgbuf.data.data());
            current_finished = true;
        }
    }
    else if(msgbuf.type == MSG_GET_RESP_MEM) {
        push_send_buf(l2_port, CHANNEL_ACK, MSG_GET_ACK, lindex, my_port_id);
        cache_line_copy(mshr->line_buf, msgbuf.data.data());
        current_finished = true;
    }
    else if(msgbuf.type == MSG_PUT_ACK) {
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
        push_send_buf_with_line(l2_port, CHANNEL_REQ, MSG_PUTM, lindex, my_port_id, mshr->line_buf);
        mshr->state = MSHR_MTOI;
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
        push_send_buf(l2_port, CHANNEL_REQ, MSG_PUTS, lindex, my_port_id);
        mshr->state = MSHR_STOI;
    }
}

void DMAL1MoesiDirNoi::on_current_tick() {
    CacheCohenrenceMsg recv;
    vector<uint8_t> buf;
    for(uint32_t c = 0; c < CHANNEL_CNT; c++) {
        if(bus->can_recv(my_port_id, c)) {
            simroot_assert(bus->recv(my_port_id, c, buf));
            parse_msg_pack(buf, recv);
            handle_recv_msg(recv);
            break;
        }
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
                assert(busmap->get_homenode_port(lindex, &l2_port));
                // printf("Request line 0x%lx\n", lindex);
                if(current->req.type == DMAReqType::host_memory_read) {
                    mshr->state = MSHR_ITOM;
                    push_send_buf(l2_port, CHANNEL_REQ, MSG_GETM, lindex, my_port_id);
                }
                else {
                    mshr->state = MSHR_ITOS;
                    push_send_buf(l2_port, CHANNEL_REQ, MSG_GETS, lindex, my_port_id);
                }
            }
        }
        if(current->line_todo.empty()) {
            current = nullptr;
        }
    }

    vector<bool> can_send;
    bus->can_send(my_port_id, can_send);
    for(auto iter = send_buf.begin(); iter != send_buf.end(); ) {
        if(can_send[iter->cha]) {
            can_send[iter->cha] = false;
            simroot_assert(bus->send(my_port_id, iter->dst, iter->cha, iter->msg));
            iter = send_buf.erase(iter);
        }
        else {
            iter++;
        }
    }

}

}}



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
        if(!(mshr->req->req.flag & DMAFLG_SRC_HOST) && !(mshr->req->req.flag & DMAFLG_DST_HOST) && !mshr->get_line_buf_valid) {
            // 现在处于获取src行的状态
            uint64_t tmp[CACHE_LINE_LEN_I64];
            cache_line_copy(tmp, mshr->line_buf);
            DMAProcessingUnit *unit = mshr->unit;
            ProcessingDMAReq *req = mshr->req;
            mshrs.remove(lindex);
            LineIndexT st_lindex = addr_to_line_index(unit->dst);
            simroot_assert(mshrs.alloc(st_lindex));
            mshr->state = MSHR_ITOM;
            mshr->req = req;
            mshr->unit = unit;
            mshr->get_line_buf_valid = true;
            cache_line_copy(mshr->get_line_buf, tmp);
            simroot_assert(busmap->get_homenode_port(st_lindex, &l2_port));
            push_send_buf(l2_port, CHANNEL_REQ, MSG_GETM, st_lindex, my_port_id);
        }
        else {
            // 已经结束了
            delete mshr->unit;
            ProcessingDMAReq *tofree = mshr->req;
            tofree->line_wait_finish.erase(mshr->unit);
            mshrs.remove(lindex);
            if(tofree->line_todo.empty() && tofree->line_wait_finish.empty()) {
                if(tofree->req.callback) handler->dma_complete_callback(tofree->req.callback);
                delete tofree;
            }
        }
    }

    if(!current_finished) {
        return;
    }

    ProcessingDMAReq *req = mshr->req;
    DMAProcessingUnit *unit = mshr->unit;

    if(!(mshr->req->req.flag & DMAFLG_SRC_HOST) && !(mshr->req->req.flag & DMAFLG_DST_HOST)) {
        if(!mshr->get_line_buf_valid) {
            // 暂存起来，等put完继续getm
            push_send_buf(l2_port, CHANNEL_REQ, MSG_PUTS, lindex, my_port_id);
            mshr->state = MSHR_STOI;
        }
        else {
            simroot_assert(mshr->state == MSHR_ITOM);
            memcpy((uint8_t*)(mshr->line_buf) + unit->off, (uint8_t*)(mshr->get_line_buf) + unit->off, unit->len);
            push_send_buf_with_line(l2_port, CHANNEL_REQ, MSG_PUTM, lindex, my_port_id, mshr->line_buf);
            mshr->state = MSHR_MTOI;
        }
    }
    else if(!(mshr->req->req.flag & DMAFLG_SRC_HOST)) {
        // 从模拟内存复制到主机内存
        simroot_assert(mshr->state == MSHR_ITOS);
        memcpy((uint8_t*)(unit->dst) + unit->off, (uint8_t*)(mshr->line_buf) + unit->off, unit->len);
        push_send_buf(l2_port, CHANNEL_REQ, MSG_PUTS, lindex, my_port_id);
        mshr->state = MSHR_STOI;
    }
    else if(!(mshr->req->req.flag & DMAFLG_DST_HOST)) {
        // 从主机内存复制到模拟内存
        simroot_assert(mshr->state == MSHR_ITOM);
        memcpy((uint8_t*)(mshr->line_buf) + unit->off, (uint8_t*)(unit->src) + unit->off, unit->len);
        push_send_buf_with_line(l2_port, CHANNEL_REQ, MSG_PUTM, lindex, my_port_id, mshr->line_buf);
        mshr->state = MSHR_MTOI;
    }
    else {
        simroot_assert(0);
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
        DMARequestUnit req = dma_req_queue.front();
        dma_req_queue.pop_front();
        current = new ProcessingDMAReq(req);
        if((req.flag & DMAFLG_SRC_HOST) && (req.flag & DMAFLG_DST_HOST)) {
            memcpy((void*)(req.dst), (void*)(req.src), req.size);
            delete current;
            current = nullptr;
            if(req.callback) handler->dma_complete_callback(req.callback);
        }
        else  {
            uint32_t cur_sz = 0;
            if(req.flag & DMAFLG_SRC_HOST) {
                if(req.dst & (CACHE_LINE_LEN_BYTE - 1)) {
                    LineAddrT tmp = addr_to_line_addr(req.dst);
                    uint32_t offset = req.dst - tmp; 
                    cur_sz = CACHE_LINE_LEN_BYTE - offset;
                    current->line_todo.emplace_back(new DMAProcessingUnit{
                        .src = req.src - offset,
                        .dst = tmp,
                        .off = offset,
                        .len = std::min<uint32_t>(cur_sz, req.size)
                    });
                }
            }
            else if(req.flag & DMAFLG_DST_HOST) {
                if(req.src & (CACHE_LINE_LEN_BYTE - 1)) {
                    LineAddrT tmp = addr_to_line_addr(req.src);
                    uint32_t offset = req.src - tmp; 
                    cur_sz = CACHE_LINE_LEN_BYTE - offset;
                    current->line_todo.emplace_back(new DMAProcessingUnit{
                        .src = tmp,
                        .dst = req.dst - offset,
                        .off = offset,
                        .len = std::min<uint32_t>(cur_sz, req.size)
                    });
                }
            }
            else {
                simroot_assertf((req.dst & (CACHE_LINE_LEN_BYTE - 1)) == (req.src & (CACHE_LINE_LEN_BYTE - 1)), "DMA Address Not Aligned: From 0x%lx to 0x%lx, len 0x%x", req.src, req.dst, req.size);
                if(req.src & (CACHE_LINE_LEN_BYTE - 1)) {
                    LineAddrT tmp = addr_to_line_addr(req.src);
                    uint32_t offset = req.src - tmp; 
                    cur_sz = CACHE_LINE_LEN_BYTE - offset;
                    current->line_todo.emplace_back(new DMAProcessingUnit{
                        .src = tmp,
                        .dst = req.dst - offset,
                        .off = offset,
                        .len = std::min<uint32_t>(cur_sz, req.size)
                    });
                }
            }

            while(cur_sz < req.size) {
                uint32_t step = std::min<uint32_t>(CACHE_LINE_LEN_BYTE, req.size - cur_sz);
                current->line_todo.emplace_back(new DMAProcessingUnit{
                    .src = req.src + cur_sz,
                    .dst = req.dst + cur_sz,
                    .off = 0,
                    .len = step
                });
                cur_sz += step;
            }
        }

        // printf("Add work @0x%lx, size 0x%lx, line cnt %ld\n", current->req.vaddr, current->req.size, current->line_todo.size());
    }

    if(current) {
        if(!current->line_todo.empty()) {
            DMAProcessingUnit *unit = current->line_todo.front();
            LineIndexT lindex = addr_to_line_index(unit->src);
            if(current->req.flag & DMAFLG_SRC_HOST) {
                lindex = addr_to_line_index(unit->dst);
            }
            bool mshr_exist = (mshrs.get(lindex) != nullptr);
            MSHREntry *mshr = nullptr;
            bool mshr_alloced = (mshr_exist?false:((mshr = mshrs.alloc(lindex)) != nullptr));
            if(!mshr_exist && mshr_alloced) {
                current->line_todo.pop_front();
                current->line_wait_finish.insert(unit);
                mshr->unit = unit;
                mshr->req = current;
                BusPortT l2_port = 0;
                assert(busmap->get_homenode_port(lindex, &l2_port));
                // printf("Request line 0x%lx\n", lindex);
                if(current->req.flag & DMAFLG_SRC_HOST) {
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


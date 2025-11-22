// MIT License

// Copyright (c) 2024 Meng Chengzhen, in Shandong University

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "lastlevelcache.h"

#include "common.h"
#include "simroot.h"
#include "configuration.h"

namespace simcache {
namespace moesi {

LLCMoesiDirNoi::LLCMoesiDirNoi(
    CacheParam &param,
    BusInterfaceV2 *bus,
    BusPortT my_port_id,
    BusPortMapping *busmap,
    string logname,
    CacheEventTrace *trace
) : bus(bus), my_port_id(my_port_id), busmap(busmap), logname(logname), trace(trace), param(param),
queue_index(1,1,0), queue_writeback(1,1,4), queue_index_result(1,1,0)
{
    do_on_current_tick = 5;
    do_apply_next_tick = 1;

    index_cycle = param.index_latency;
    process_buf_size = param.mshr_num;

    nuca_num = param.nuca_num;
    nuca_index = param.nuca_index;

    simroot_assertf(nuca_num > 0, "NUCA node num must be more than 1: %ld", nuca_num);
    simroot_assertf(nuca_index < nuca_num, "Invalid NUCA node index %ld for %ld nodes", nuca_index, nuca_num)

    block = make_unique<GenericLRUCacheBlock<CacheLineT>>(param.set_offset, param.way_cnt);
    directory = make_unique<GenericLRUCacheBlock<DirEntry>>(param.dir_set_offset, param.dir_way_cnt);
}

void LLCMoesiDirNoi::p1_fetch() {
    CacheCohenrenceMsg msg;
    bool recv = false;
    if(recv_buf.size() < recv_buf_size) {
        vector<bool> can_recv;
        bus->can_recv(my_port_id, can_recv);
        for(uint32_t c = 0; c < CHANNEL_CNT; c++) {
            if(!can_recv[c]) continue;
            vector<uint8_t> buf;
            simroot_assert(bus->recv(my_port_id, c, buf));
            parse_msg_pack(buf, msg);
            recv = true;
            break;
        }
    }
    if(recv) {
        simroot_assertf(nuca_check(msg.line), "Unexpected Line @0x%lx at NUCA node %ld/%ld", msg.line, nuca_index, nuca_num);
        switch (msg.type)
        {
        case MSG_INVALID_ACK :
            break;
        case MSG_GET_ACK :
            processing_lindex.erase(msg.line);
            block->unpin(lindex_to_nuca_tag(msg.line));
            break;
        case MSG_PUTM :
        case MSG_PUTO :
            simroot_assert(msg.data.size() == CACHE_LINE_LEN_BYTE);
        case MSG_GETS :
        case MSG_GETM :
        case MSG_PUTS :
        case MSG_PUTE :
            recv_buf.push_back(msg);
            break;
        default:
            simroot_assert(0);
        }
    }

    if(recv_buf.empty() || !(queue_index.can_push())) {
        return;
    }

    for(auto iter = recv_buf.begin(); iter != recv_buf.end(); iter++) {
        if(processing_lindex.find(iter->line) != processing_lindex.end()) {
            continue;
        }
        RequestPackage *topush = new RequestPackage;
        topush->type = iter->type;
        topush->lindex = iter->line;
        topush->arg = iter->arg;
        topush->transid = iter->transid;
        topush->index_cycle = this->index_cycle;
        if(iter->data.size() > 0) {
            simroot_assert(iter->data.size() == CACHE_LINE_LEN_BYTE);
            cache_line_copy(topush->line_buf, iter->data.data());
        }
        queue_index.push(topush);
        processing_lindex.insert(iter->line);
        block->pin(lindex_to_nuca_tag(iter->line));
        recv_buf.erase(iter);
        break;
    }
}

void LLCMoesiDirNoi::p2_index() {
    if(queue_writeback.can_pop()) {
        RequestPackage *wb = queue_writeback.top();
        if(wb->dir_evict) {
            directory->remove_line(lindex_to_nuca_tag(wb->lindex));
        }
        else {
            LineIndexT tmp = 0;
            DirEntry tmpent;
            simroot_assert(!(directory->insert_line(lindex_to_nuca_tag(wb->lindex), &(wb->entry), &tmp, &tmpent)));
        }
        if(!(wb->delay_commit)) {
            processing_lindex.erase(wb->lindex);
            block->unpin(lindex_to_nuca_tag(wb->lindex));
        }
        queue_writeback.pop();
        delete wb;
        return;
    }

    if(!(queue_index.can_pop())) {
        return;
    }
    if(!(queue_index_result.can_push())) {
        return;
    }
    RequestPackage *pak = queue_index.top();
    if(pak->index_cycle > 1) {
        pak->index_cycle--;
        return;
    }
    queue_index.pop();
    queue_index_result.push(pak);

    BusPortT dst = 0;

    uint32_t transid = pak->transid;

    if(pak->type == MSG_GETS) {
        CacheLineT *pline = nullptr;
        pak->blk_hit = block->get_line(lindex_to_nuca_tag(pak->lindex), &pline, true);
        DirEntry *pentry = nullptr;
        if(pak->dir_hit = directory->get_line(lindex_to_nuca_tag(pak->lindex), &pentry, false)) {
            pak->entry = *pentry;
        }
        pak->blk_replaced = false;

        uint32_t l1_index = 0;
        BusPortT src_port = pak->arg;
        simroot_assert(busmap->get_reqnode_index(src_port, &l1_index));
        
        if(!(pak->dir_hit) && !(pak->blk_hit)) {
            // LLC miss
            simroot_assert(busmap->get_subnode_port(pak->lindex, &dst));
            pak->push_send_buf(dst, CHANNEL_REQ, MSG_GETS_FORWARD, pak->lindex, pak->arg, transid);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
            pak->delay_commit = true;

            if(trace) trace->insert_event(transid, CacheEvent::L3_MISS);
            statistic.llc_miss_count++;
        }
        else if(!(pak->dir_hit) && pak->blk_hit) {
            // LLC block hit, not in L1
            pak->push_send_buf_with_line(src_port, CHANNEL_RESP, MSG_GETS_RESP, pak->lindex, 0, pline->data(), transid);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;

            if(trace) trace->insert_event(transid, CacheEvent::L3_HIT);
            statistic.llc_hit_count++;
        }
        else {
            simroot_assert(pak->entry.exists.find(pak->entry.owner) != pak->entry.exists.end());
            simroot_assert(busmap->get_reqnode_port(pak->entry.owner, &dst));
            pak->push_send_buf(dst, CHANNEL_REQ, MSG_GETS_FORWARD, pak->lindex, pak->arg, transid);

            pak->entry.exists.insert(l1_index);
            pak->delay_commit = true;
            
            if(trace) trace->insert_event(transid, CacheEvent::L3_FORWARD);
            statistic.llc_hit_count++;
        }
    }
    else if(pak->type == MSG_GETM) {
        CacheLineT *pline = nullptr;
        pak->blk_hit = block->get_line(lindex_to_nuca_tag(pak->lindex), &pline, true);
        DirEntry *pentry = nullptr;
        if(pak->dir_hit = directory->get_line(lindex_to_nuca_tag(pak->lindex), &pentry, false)) {
            pak->entry = *pentry;
        }
        pak->blk_replaced = false;

        uint32_t l1_index = 0;
        BusPortT src_port = pak->arg;
        simroot_assert(busmap->get_reqnode_index(src_port, &l1_index));
        
        if(!(pak->dir_hit) && !(pak->blk_hit)) {
            // LLC miss
            simroot_assert(busmap->get_subnode_port(pak->lindex, &dst));
            pak->push_send_buf(dst, CHANNEL_REQ, MSG_GETM_FORWARD, pak->lindex, pak->arg, transid);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
            pak->delay_commit = true;
            
            if(trace) trace->insert_event(transid, CacheEvent::L3_MISS);
            statistic.llc_miss_count++;
        }
        else if(!(pak->dir_hit) && pak->blk_hit) {
            // LLC block hit, not in L1
            pak->push_send_buf_with_line(src_port, CHANNEL_RESP, MSG_GETM_RESP, pak->lindex, 1, pline->data(), transid);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
            
            if(trace) trace->insert_event(transid, CacheEvent::L3_HIT);
            statistic.llc_miss_count++;
        }
        else {
            if(pak->blk_hit) {
                // LLC is out-of-date
                block->remove_line(lindex_to_nuca_tag(pak->lindex));
            }
            bool skip_owner = false;
            if(std::find(pak->entry.exists.begin(), pak->entry.exists.end(), l1_index) == pak->entry.exists.end())
            {
                simroot_assert(busmap->get_reqnode_port(pak->entry.owner, &dst));
                pak->push_send_buf(dst, CHANNEL_REQ, MSG_GETM_FORWARD, pak->lindex, pak->arg, transid);
                skip_owner = true;
            }
            uint32_t invalid_cnt = 0;
            for(auto l1 : pak->entry.exists) {
                if(l1 == l1_index || (skip_owner && l1 == pak->entry.owner)) {
                    continue;
                }
                simroot_assert(busmap->get_reqnode_port(l1, &dst));
                pak->push_send_buf(dst, CHANNEL_RESP, MSG_INVALID, pak->lindex, pak->arg, transid);
                invalid_cnt++;
            }
            pak->push_send_buf(src_port, CHANNEL_RESP, MSG_GETM_ACK, pak->lindex, invalid_cnt, transid);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
            pak->delay_commit = true;
            
            if(trace) trace->insert_event(transid, skip_owner?(CacheEvent::L3_FORWARD):(CacheEvent::L3_HIT));
            statistic.llc_miss_count++;
        }
    }
    else if(pak->type == MSG_PUTS || pak->type == MSG_PUTE) {
        
        uint32_t l1_index = 0;
        BusPortT src_port = pak->arg;
        simroot_assert(busmap->get_reqnode_index(src_port, &l1_index));

        DirEntry *pentry = nullptr;
        pak->dir_hit = directory->get_line(lindex_to_nuca_tag(pak->lindex), &pentry, false);
        if(pak->dir_hit) {
            pak->entry = *pentry;
            pak->entry.exists.erase(l1_index);
            if(l1_index == pak->entry.owner && !(pak->entry.exists.empty())) {
                pak->entry.owner = *(pak->entry.exists.begin());
            }
            pak->dir_evict = (pak->entry.exists.empty());
        }
        else {
            pak->dir_evict = true;
        }
        
        pak->push_send_buf(src_port, CHANNEL_ACK, MSG_PUT_ACK, pak->lindex, 0, 0);
    }
    else if(pak->type == MSG_PUTM || pak->type == MSG_PUTO) {

        DirEntry *pentry = nullptr;
        simroot_assert(pak->dir_hit = directory->get_line(lindex_to_nuca_tag(pak->lindex), &pentry, false));
        pak->entry = *pentry;

        uint32_t l1_index = 0;
        BusPortT src_port = pak->arg;
        simroot_assert(busmap->get_reqnode_index(src_port, &l1_index));

        if(pak->entry.owner == l1_index) {
            CacheLineT new_line, replaced_line;
            // new_line.state = CacheLineState::owned;
            cache_line_copy(new_line.data(), pak->line_buf);
            if(pak->blk_replaced = block->insert_line(lindex_to_nuca_tag(pak->lindex), &new_line, &(pak->lindex_replaced), &replaced_line)) {
                pak->lindex_replaced = nuca_tag_to_lindex(pak->lindex_replaced);
                DirEntry *rep_ent;
                bool rep_dir_hit = directory->get_line(lindex_to_nuca_tag(pak->lindex_replaced), &rep_ent, false);

                if(rep_dir_hit && !(rep_ent->dirty)) {
                    for(auto l1 : rep_ent->exists) {
                        simroot_assert(busmap->get_reqnode_port(l1, &dst));
                        pak->push_send_buf(dst, CHANNEL_RESP, MSG_INVALID, pak->lindex_replaced, my_port_id, 0);
                    }
                    directory->remove_line(lindex_to_nuca_tag(pak->lindex_replaced));
                }

                simroot_assert(busmap->get_subnode_port(pak->lindex_replaced, &dst));
                pak->push_send_buf_with_line(dst, CHANNEL_REQ, MSG_PUTM, pak->lindex_replaced, my_port_id, replaced_line.data(), 0);
            }
        }

        pak->push_send_buf(pak->arg, CHANNEL_ACK, MSG_PUT_ACK, pak->lindex, 0, 0);

        pak->entry.dirty = (pak->entry.owner != l1_index);
        pak->entry.exists.erase(l1_index);
        if(!(pak->entry.exists.empty())) {
            pak->entry.owner = *(pak->entry.exists.begin());
        }
        pak->dir_evict = (pak->entry.exists.empty());
    }
    else {
        simroot_assert(0);
    }
}

void LLCMoesiDirNoi::p3_process() {

    if(process_buf.size() < process_buf_size && queue_index_result.can_pop()) {
        process_buf.push_back(queue_index_result.top());
        queue_index_result.pop();
    }

    if(process_buf.empty()) {
        return;
    }

    vector<bool> can_send;
    bus->can_send(my_port_id, can_send);

    for(auto &pak : process_buf) {
        for(auto iter = pak->need_send.begin(); iter != pak->need_send.end(); ) {
            if(can_send[iter->cha]) {
                simroot_assert(bus->send(my_port_id, iter->dst, iter->cha, iter->msg));
                can_send[iter->cha] = false;
                iter = pak->need_send.erase(iter);
            }
            else {
                iter++;
            }
        }
    }

    if(queue_writeback.can_push()) {
        for(auto iter = process_buf.begin(); iter != process_buf.end(); iter++) {
            RequestPackage *pak = *iter;
            if(!(pak->need_send.empty())) {
                continue;
            }
            queue_writeback.push(pak);
            process_buf.erase(iter);
            break;
        }
    }

}

void LLCMoesiDirNoi::on_current_tick() {
    p1_fetch();
    p2_index();
    p3_process();
}

void LLCMoesiDirNoi::apply_next_tick() {
    queue_index.apply_next_tick();
    queue_writeback.apply_next_tick();
    queue_index_result.apply_next_tick();
}


#define LOGTOFILE(fmt, ...) do{sprintf(log_buf, fmt, ##__VA_ARGS__);ofile << log_buf;}while(0)
#define PIPELINE_5_GENERATE_PRINTSTATISTIC(n) ofile << #n << ": " << statistic.n << "\n" ;

void LLCMoesiDirNoi::print_statistic(std::ofstream &ofile) {
    PIPELINE_5_GENERATE_PRINTSTATISTIC(llc_hit_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(llc_miss_count)
}

void LLCMoesiDirNoi::print_setup_info(std::ofstream &ofile) {
    LOGTOFILE("port_id: %d\n", my_port_id);
    LOGTOFILE("way_count: %d\n", param.way_cnt);
    LOGTOFILE("set_count: %d\n", 1 << param.set_offset);
    LOGTOFILE("mshr_count: %d\n", param.mshr_num);
    LOGTOFILE("index_width: %d\n", param.index_width);
    LOGTOFILE("index_latency: %d\n", param.index_latency);
    LOGTOFILE("nuca_index: %d\n", param.nuca_index);
    LOGTOFILE("nuca_num: %d\n", param.nuca_num);
}


void LLCMoesiDirNoi::dump_core(std::ofstream &ofile) {
    for(uint32_t s = 0; s < block->set_count; s++) {
        sprintf(log_buf, "set %d: ", s);
        ofile << log_buf;
        for(auto &e : block->p_sets[s]) {
            sprintf(log_buf, "0x%lx ", nuca_tag_to_lindex(e.first));
            ofile << log_buf;
        }
        ofile << "\n";
    }
    for(uint32_t s = 0; s < directory->set_count; s++) {
        sprintf(log_buf, "dir %d: ", s);
        ofile << log_buf;
        for(auto &e : directory->p_sets[s]) {
            sprintf(log_buf, "0x%lx-d%d-o%d", nuca_tag_to_lindex(e.first), e.second.dirty, e.second.owner);
            ofile << log_buf;
            for(auto &s : e.second.exists) {
                sprintf(log_buf, "-%d", s);
                ofile << log_buf;
            }
            ofile << " ";
        }
        ofile << "\n";
    }

    ofile << "processing: ";
    for(auto &l : processing_lindex) {
        sprintf(log_buf, "0x%lx ", l);
        ofile << log_buf;
    }
    ofile << "\n";


}



}}


#include "lastlevelcache.h"

#include "common.h"
#include "simroot.h"
#include "configuration.h"

namespace simcache {
namespace moesi {

LLCMoesiDirNoi::LLCMoesiDirNoi(
    uint32_t set_addr_offset,
    uint32_t line_per_set,
    uint32_t dir_set_addr_offset,
    uint32_t dir_line_per_set,
    BusInterface *bus,
    BusPortT my_port_id,
    BusPortMapping *busmap,
    string logname
) : block(set_addr_offset, line_per_set), directory(dir_set_addr_offset, dir_line_per_set),
bus(bus), my_port_id(my_port_id), busmap(busmap), logname(logname),
queue_index(1,1,0), queue_writeback(1,1,4), queue_index_result(1,1,0)
{
    do_on_current_tick = 5;
    do_apply_next_tick = 1;
    index_cycle_count = conf::get_int("llc", "index_cycle", 1);
}

void LLCMoesiDirNoi::p1_fetch() {
    CacheCohenrenceMsg msg;
    if(recv_buf.size() < recv_buf_size && bus->recv(my_port_id, &msg)) {
        switch (msg.type)
        {
        case CCMsgType::invalid_ack :
            break;
        case CCMsgType::get_ack :
            processing_lindex.erase(msg.line);
            block.unpin(msg.line);
            break;
        case CCMsgType::putm :
        case CCMsgType::puto :
            simroot_assert(msg.data.size() == CACHE_LINE_LEN_BYTE);
        case CCMsgType::gets :
        case CCMsgType::getm :
        case CCMsgType::puts :
        case CCMsgType::pute :
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
        if(iter->data.size() > 0) {
            assert(iter->data.size() == CACHE_LINE_LEN_BYTE);
            cache_line_copy(topush->line_buf, iter->data.data());
        }
        queue_index.push(topush);
        processing_lindex.insert(iter->line);
        block.pin(iter->line);
        recv_buf.erase(iter);
        break;
    }
}

void LLCMoesiDirNoi::p2_index() {
    if(queue_writeback.can_pop()) {
        RequestPackage *wb = queue_writeback.top();
        if(wb->dir_evict) {
            directory.remove_line(wb->lindex);
        }
        else {
            LineIndexT tmp = 0;
            DirEntry tmpent;
            simroot_assert(!(directory.insert_line(wb->lindex, &(wb->entry), &tmp, &tmpent)));
        }
        if(!(wb->delay_commit)) {
            processing_lindex.erase(wb->lindex);
            block.unpin(wb->lindex);
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
    queue_index.pop();
    queue_index_result.push(pak);

    BusPortT dst = 0;

    if(pak->type == CCMsgType::gets) {
        DefaultCacheLine *pline = nullptr;
        pak->blk_hit = block.get_line(pak->lindex, &pline, true);
        DirEntry *pentry = nullptr;
        if(pak->dir_hit = directory.get_line(pak->lindex, &pentry, false)) {
            pak->entry = *pentry;
        }
        pak->blk_replaced = false;

        uint32_t l1_index = 0;
        BusPortT src_port = pak->arg;
        assert(busmap->get_downlink_port_index(src_port, &l1_index));
        
        if(!(pak->dir_hit) && !(pak->blk_hit)) {
            // LLC miss
            simroot_assert(busmap->get_memory_port(pak->lindex, &dst));
            pak->push_send_buf(dst, CCMsgType::gets_forward, pak->lindex, pak->arg);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
            pak->delay_commit = true;
        }
        else if(!(pak->dir_hit) && pak->blk_hit) {
            // LLC block hit, not in L1
            pak->push_send_buf_with_line(src_port, CCMsgType::gets_resp, pak->lindex, 0, pline->data);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
        }
        else {
            simroot_assert(pak->entry.exists.find(pak->entry.owner) != pak->entry.exists.end());
            simroot_assert(busmap->get_downlink_port(pak->entry.owner, &dst));
            pak->push_send_buf(dst, CCMsgType::gets_forward, pak->lindex, pak->arg);

            pak->entry.exists.insert(l1_index);
            pak->delay_commit = true;
        }
    }
    else if(pak->type == CCMsgType::getm) {
        DefaultCacheLine *pline = nullptr;
        pak->blk_hit = block.get_line(pak->lindex, &pline, true);
        DirEntry *pentry = nullptr;
        if(pak->dir_hit = directory.get_line(pak->lindex, &pentry, false)) {
            pak->entry = *pentry;
        }
        pak->blk_replaced = false;

        uint32_t l1_index = 0;
        BusPortT src_port = pak->arg;
        assert(busmap->get_downlink_port_index(src_port, &l1_index));
        
        if(!(pak->dir_hit) && !(pak->blk_hit)) {
            // LLC miss
            simroot_assert(busmap->get_memory_port(pak->lindex, &dst));
            pak->push_send_buf(dst, CCMsgType::getm_forward, pak->lindex, pak->arg);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
            pak->delay_commit = true;
        }
        else if(!(pak->dir_hit) && pak->blk_hit) {
            // LLC block hit, not in L1
            pak->push_send_buf_with_line(src_port, CCMsgType::getm_resp, pak->lindex, 1, pline->data);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
        }
        else {
            if(pak->blk_hit) {
                // LLC is out-of-date
                block.remove_line(pak->lindex);
            }
            bool skip_owner = false;
            if(std::find(pak->entry.exists.begin(), pak->entry.exists.end(), l1_index) == pak->entry.exists.end())
            {
                simroot_assert(busmap->get_downlink_port(pak->entry.owner, &dst));
                pak->push_send_buf(dst, CCMsgType::getm_forward, pak->lindex, pak->arg);
                skip_owner = true;
            }
            uint32_t invalid_cnt = 0;
            for(auto l1 : pak->entry.exists) {
                if(l1 == l1_index || (skip_owner && l1 == pak->entry.owner)) {
                    continue;
                }
                simroot_assert(busmap->get_downlink_port(l1, &dst));
                pak->push_send_buf(dst, CCMsgType::invalid, pak->lindex, pak->arg);
                invalid_cnt++;
            }
            pak->push_send_buf(src_port, CCMsgType::getm_ack, pak->lindex, invalid_cnt);

            pak->entry.dirty = true;
            pak->entry.exists.clear();
            pak->entry.exists.insert(l1_index);
            pak->entry.owner = l1_index;
            pak->delay_commit = true;
        }
    }
    else if(pak->type == CCMsgType::puts || pak->type == CCMsgType::pute) {
        
        uint32_t l1_index = 0;
        BusPortT src_port = pak->arg;
        assert(busmap->get_downlink_port_index(src_port, &l1_index));

        DirEntry *pentry = nullptr;
        pak->dir_hit = directory.get_line(pak->lindex, &pentry, false);
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
        
        pak->push_send_buf(src_port, CCMsgType::put_ack, pak->lindex, 0);
    }
    else if(pak->type == CCMsgType::putm || pak->type == CCMsgType::puto) {

        DirEntry *pentry = nullptr;
        simroot_assert(pak->dir_hit = directory.get_line(pak->lindex, &pentry, false));
        pak->entry = *pentry;

        uint32_t l1_index = 0;
        BusPortT src_port = pak->arg;
        assert(busmap->get_downlink_port_index(src_port, &l1_index));

        if(pak->entry.owner == l1_index) {
            DefaultCacheLine new_line, replaced_line;
            new_line.state = CacheLineState::owned;
            cache_line_copy(new_line.data, pak->line_buf);
            if(pak->blk_replaced = block.insert_line(pak->lindex, &new_line, &(pak->lindex_replaced), &replaced_line)) {
                DirEntry *rep_ent;
                bool rep_dir_hit = directory.get_line(pak->lindex_replaced, &rep_ent, false);

                if(rep_dir_hit && !(rep_ent->dirty)) {
                    for(auto l1 : rep_ent->exists) {
                        simroot_assert(busmap->get_downlink_port(l1, &dst));
                        pak->push_send_buf(dst, CCMsgType::invalid, pak->lindex_replaced, my_port_id);
                    }
                    directory.remove_line(pak->lindex_replaced);
                }

                simroot_assert(busmap->get_memory_port(pak->lindex_replaced, &dst));
                pak->push_send_buf_with_line(dst, CCMsgType::putm, pak->lindex_replaced, my_port_id, replaced_line.data);
            }
        }

        pak->push_send_buf(pak->arg, CCMsgType::put_ack, pak->lindex, 0);

        pak->entry.dirty = (pak->entry.owner != l1_index);
        pak->entry.exists.erase(l1_index);
        if(!(pak->entry.exists.empty())) {
            pak->entry.owner = *(pak->entry.exists.begin());
        }
        pak->dir_evict = (pak->entry.exists.empty());
    }
    else {
        assert(0);
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

    if(bus->can_send(my_port_id)) {
        for(auto iter = process_buf.begin(); iter != process_buf.end(); iter++) {
            if((*iter)->need_send.empty()) {
                continue;
            }
            auto &send = (*iter)->need_send.front();
            assert(bus->send(my_port_id, send.first, send.second));
            (*iter)->need_send.pop_front();
            break;
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
    if(index_current_cycle < index_cycle_count) {
        index_current_cycle ++;
    }
    else {
        p2_index();
        index_current_cycle = 1;
    }
    p3_process();
}

void LLCMoesiDirNoi::apply_next_tick() {
    queue_index.apply_next_tick();
    queue_writeback.apply_next_tick();
    queue_index_result.apply_next_tick();
}
    
void LLCMoesiDirNoi::dump_core(std::ofstream &ofile) {
    for(uint32_t s = 0; s < block.set_count; s++) {
        sprintf(log_buf, "set %d: ", s);
        ofile << log_buf;
        for(auto &e : block.p_sets[s]) {
            sprintf(log_buf, "0x%lx ", e.first);
            ofile << log_buf;
        }
        ofile << "\n";
    }
    for(uint32_t s = 0; s < directory.set_count; s++) {
        sprintf(log_buf, "dir %d: ", s);
        ofile << log_buf;
        for(auto &e : directory.p_sets[s]) {
            sprintf(log_buf, "0x%lx-d%d-o%d", e.first, e.second.dirty, e.second.owner);
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

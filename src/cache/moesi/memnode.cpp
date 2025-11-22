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


#include "memnode.h"

#include "simroot.h"
#include "configuration.h"

namespace simcache {
namespace moesi {

MemoryNode::MemoryNode(
    uint8_t *memblk,
    MemCtrlLineAddrMap *addr_map,
    BusInterfaceV2 *bus,
    BusPortT my_port,
    uint32_t dwidth,
    CacheEventTrace *trace
) : memblk(memblk), addr_map(addr_map), bus(bus), my_port(my_port), dwidth(dwidth), trace(trace) {
    do_on_current_tick = 2;
    do_apply_next_tick = 0;

    memory_access_buf_size = conf::get_int("mem", "memory_access_buf_size", 4);
}

#define LOGTOFILE(fmt, ...) do{sprintf(log_buf, fmt, ##__VA_ARGS__);ofile << log_buf;}while(0)

void MemoryNode::print_statistic(std::ofstream &ofile) {
    LOGTOFILE("message_precossed: %ld\n", statistic.request_precossed);
    LOGTOFILE("busy_rate: %f\n", ((double)(statistic.busy_cycles))/(simroot::get_current_tick()));
}

void MemoryNode::print_setup_info(std::ofstream &ofile) {
    LOGTOFILE("port_id: %d\n", my_port);
    LOGTOFILE("data_width: %d\n", dwidth);
}



void MemoryNode::on_current_tick() {
    CacheCohenrenceMsg msg;
    bool recv = false;
    bool busy = false;
    if(membufs.size() < memory_access_buf_size) {
        for(uint32_t c = 0; c < CHANNEL_CNT; c++) {
            if(bus->can_recv(my_port, c)) {
                vector<uint8_t> buf;
                simroot_assert(bus->recv(my_port, c, buf));
                parse_msg_pack(buf, msg);
                recv = true;
                break;
            }
        }
    }

    if(recv) {
        membufs.emplace_back();
        auto &mb = membufs.back();
        mb.lindex = msg.line;
        simroot_assert(addr_map->is_responsible(msg.line));
        mb.hostoff = addr_map->get_local_mem_offset(msg.line);
        mb.src_port = msg.arg;
        mb.transid = msg.transid;
        switch (msg.type)
        {
        case MSG_GETS_FORWARD :
        case MSG_GETM_FORWARD :
            mb.op = 0;
            break;
        case MSG_PUTM :
        case MSG_PUTO :
            mb.op = 1;
            simroot_assert(msg.data.size() == CACHE_LINE_LEN_BYTE);
            cache_line_copy(mb.linebuf, msg.data.data());
            break;
        default:
            simroot_assert(0);
        }
        if(trace) trace->insert_event(msg.transid, CacheEvent::MEM_HANDLE);
        statistic.request_precossed ++;
        busy = true;
    }

    if(membufs.empty()) {
        return;
    }

    for(auto iter = membufs.begin(); iter != membufs.end(); iter++) {
        auto &mb = *iter;
        if(mb.processed < CACHE_LINE_LEN_BYTE) {
            uint32_t sz = std::min(dwidth, CACHE_LINE_LEN_BYTE - mb.processed);
            if(mb.op) {
                memcpy(memblk + (mb.hostoff + mb.processed), mb.linebuf + mb.processed, sz);
            }
            else {
                memcpy(mb.linebuf + mb.processed, memblk + (mb.hostoff + mb.processed), sz);
            }
            mb.processed += sz;
            if(mb.processed >= CACHE_LINE_LEN_BYTE && mb.op) {
                membufs.erase(iter);
            }
            break;
            busy = true;
        }
    }

    if(!(bus->can_send(my_port, CHANNEL_RESP))) {
        return;
    }

    for(auto iter = membufs.begin(); iter != membufs.end(); iter++) {
        if(iter->processed >= CACHE_LINE_LEN_BYTE) {
            simroot_assert(!(iter->op));
            CacheCohenrenceMsg send;
            send.line = iter->lindex;
            send.arg = 0;
            send.type = MSG_GET_RESP_MEM;
            send.transid = iter->transid;
            send.data.assign(CACHE_LINE_LEN_BYTE, 0);
            cache_line_copy(send.data.data(), iter->linebuf);
            vector<uint8_t> buf;
            construct_msg_pack(send, buf);
            simroot_assert(bus->send(my_port, iter->src_port, CHANNEL_RESP, buf));
            membufs.erase(iter);
            break;
            busy = true;
        }
    }

    if(busy) {
        statistic.busy_cycles++;
    }
}

}}

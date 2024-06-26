
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
    uint32_t dwidth
) : memblk(memblk), addr_map(addr_map), bus(bus), my_port(my_port), dwidth(dwidth) {
    do_on_current_tick = 2;
    do_apply_next_tick = 0;

    memory_access_buf_size = conf::get_int("mem", "memory_access_buf_size", 4);
}

void MemoryNode::on_current_tick() {
    CacheCohenrenceMsg msg;
    bool recv = false;
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
        }
    }

    if(!(bus->can_send(my_port, CHANNEL_RESP))) {
        return;
    }

    for(auto iter = membufs.begin(); iter != membufs.end(); iter++) {
        if(iter->processed >= CACHE_LINE_LEN_BYTE) {
            assert(!(iter->op));
            CacheCohenrenceMsg send;
            send.line = iter->lindex;
            send.arg = 0;
            send.type = MSG_GET_RESP_MEM;
            send.data.assign(CACHE_LINE_LEN_BYTE, 0);
            cache_line_copy(send.data.data(), iter->linebuf);
            vector<uint8_t> buf;
            construct_msg_pack(send, buf);
            assert(bus->send(my_port, iter->src_port, CHANNEL_RESP, buf));
            membufs.erase(iter);
            break;
        }
    }
}

}}

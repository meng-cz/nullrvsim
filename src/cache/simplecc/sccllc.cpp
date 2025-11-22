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


#include "simroot.h"
#include "sccllc.h"

namespace simcache {

SimpleCoherentLLCWithMem::SimpleCoherentLLCWithMem(
    CacheParam &param,
    uint32_t port_num, uint32_t bus_width,
    uint64_t mem_size, PhysAddrT mem_base_addr, uint32_t memory_latency
) : llc_param(param), port_num(port_num), mem_base_addr(mem_base_addr), memory_latency(memory_latency) {
    bus_latency = CEIL_DIV(CACHE_LINE_LEN_BYTE, bus_width);

    llc_cb = make_unique<GenericLRUCacheBlock<CacheLineT>>(param.set_offset, param.way_cnt);

    ports.resize(port_num);
    uint32_t idx = 0;
    for(auto &p : ports) {
        memset(&p, 0, sizeof(p));
        p.index = idx++;
    }

    if(mem_size > 512UL*1024UL*1024UL) mem.resize(mem_size);
    else mem.assign(mem_size, 0);
}

void SimpleCoherentLLCWithMem::on_current_tick() {
    for(auto &p : ports) {
        if(p.req_resp_vld_next == p.req_resp_vld_previous && p.req_resp_vld_previous) {
            p.req_resp_vld_previous = p.req_resp_vld_next = 0;
        }
        
        if(p.snoop_next == p.snoop_previous && p.snoop_previous != SCCSnoop::none) {
            p.snoop_previous = p.snoop_next = SCCSnoop::none;
        }
    }

    if(set_snoop > 1) {
        set_snoop --;
        for(auto & p : ports) {
            p.snoop_next = SCCSnoop::none;
        }
    } else if(set_snoop == 1) {
        set_snoop = 0;
        if(need_snoop_data) {
            cache_line_copy(ports[who_need_snoop_data].req_line_buf, ports[wait_snoop_data].snoop_line_buf);
            need_snoop_data = false;
        }
    } else if(busy > 1) {
        busy--;
        return;
    } else if(busy == 1) {
        SCCBundle &port = ports[current];
        if(debug) {
            printf("%ld: L2 to L1-%d: 0x%lx, request readu\n", simroot::get_current_tick(), current, port.req_line);
        }
        port.req_resp_vld_next = 1;
        busy = 0;
        current = (current + 1) % port_num;
    } else {
        for(uint32_t i = 0; i < port_num; i++) {
            if(ports[current].req != SCCReq::none) {
                break;
            }
            current = (current + 1) % port_num;
        }
        SCCBundle &port = ports[current];
        switch (port.req)
        {
        case SCCReq::read:
            port.req = SCCReq::none;
            busy = handle_read(current);
            break;
        case SCCReq::readu:
            port.req = SCCReq::none;
            busy = handle_readu(current);
            break;
        case SCCReq::put:
            port.req = SCCReq::none;
            busy = handle_put(current);
            break; 
        }
    }
}

void SimpleCoherentLLCWithMem::apply_next_tick() {
    for(auto &p : ports) {
        if(p.req_resp_vld_next != p.req_resp_vld_previous) {
            p.req_resp = p.req_resp_vld_next;
        }
        p.req_resp_vld_previous = p.req_resp_vld_next;
        
        if(p.snoop_next != p.snoop_previous) {
            p.snoop = p.snoop_next;
            p.snoop_line = p.snoop_line_next;
        }
        p.snoop_previous = p.snoop_next;
    }
}

uint32_t SimpleCoherentLLCWithMem::handle_read(uint32_t idx) {
    SCCBundle &port = ports[idx];
    LineIndexT line = port.req_line;

    {
        auto iter = state.find(line);
        if(iter != state.end()) {
            SCCHigherState &s = iter->second;
            if(s.shared.find(idx) != s.shared.end()) {
                if(debug) {
                    printf("%ld: L2 to L1-%d: 0x%lx, read, noneed\n", simroot::get_current_tick(), idx, port.req_line);
                }
                return 1;
            }
            // in other l1
            set_snoop = 2;
            ports[s.owner].snoop_next = SCCSnoop::share;
            ports[s.owner].snoop_line_next = line;
            need_snoop_data = true;
            who_need_snoop_data = idx;
            wait_snoop_data = s.owner;
            s.shared.insert(idx);
            if(debug) {
                printf("%ld: L2 to L1-%d: 0x%lx, read, forward %d\n", simroot::get_current_tick(), idx, port.req_line, wait_snoop_data);
            }
            return (1 + bus_latency);
        }
    }

    // l1miss
    auto iter = state.emplace(line, SCCHigherState()).first;
    auto &s = iter->second;
    s.owner = idx;
    CacheLineT *l2cl = nullptr;
    llc_cb->get_line(line, &l2cl, true);
    if(l2cl) {
        // l2 hit
        cache_line_copy(port.req_line_buf, l2cl->data());
        if(debug) {
            printf("%ld: L2 to L1-%d: 0x%lx, read, l2hit\n", simroot::get_current_tick(), idx, port.req_line);
        }
        return (1 + llc_param.index_latency + bus_latency);
    } else {
        cache_line_copy(port.req_line_buf, mem.data() + (CACHE_LINE_LEN_BYTE * line - mem_base_addr));
        if(debug) {
            printf("%ld: L2 to L1-%d: 0x%lx, read, l2miss\n", simroot::get_current_tick(), idx, port.req_line);
        }
        return (1 + llc_param.index_latency + bus_latency * 2 + memory_latency);
    }

}

uint32_t SimpleCoherentLLCWithMem::handle_readu(uint32_t idx) {
    SCCBundle &port = ports[idx];
    LineIndexT line = port.req_line;

    {
        auto iter = state.find(line);
        if(iter != state.end()) {
            uint32_t ret = 1;
            SCCHigherState &s = iter->second;
            ports[s.owner].snoop_next = SCCSnoop::uniq;
            ports[s.owner].snoop_line_next = line;
            set_snoop = 2;
            need_snoop_data = true;
            who_need_snoop_data = idx;
            wait_snoop_data = s.owner;
            if(s.owner != idx) ret += bus_latency;
            if(debug) {
                printf("%ld: L2 to L1-%d: 0x%lx, readu, forward %d\n", simroot::get_current_tick(), idx, port.req_line, wait_snoop_data);
            }
            for(auto other : s.shared) {
                if(other != idx) {
                    ports[s.owner].snoop_next = SCCSnoop::inval;
                    ports[s.owner].snoop_line_next = line;
                    set_snoop = 2;
                }
            }
            s.owner = idx;
            s.shared.clear();
            return ret;
        }
    }

    // l1miss
    auto iter = state.emplace(line, SCCHigherState()).first;
    auto &s = iter->second;
    s.owner = idx;
    CacheLineT *l2cl = nullptr;
    llc_cb->get_line(line, &l2cl, true);
    if(l2cl) {
        // l2 hit
        cache_line_copy(port.req_line_buf, l2cl->data());
        if(debug) {
            printf("%ld: L2 to L1-%d: 0x%lx, readu, l2hit\n", simroot::get_current_tick(), idx, port.req_line);
        }
        return (1 + llc_param.index_latency + bus_latency);
    } else {
        cache_line_copy(port.req_line_buf, mem.data() + (CACHE_LINE_LEN_BYTE * line - mem_base_addr));
        if(debug) {
            printf("%ld: L2 to L1-%d: 0x%lx, readu, l2miss\n", simroot::get_current_tick(), idx, port.req_line);
        }
        return (1 + llc_param.index_latency + bus_latency * 2 + memory_latency);
    }
}

uint32_t SimpleCoherentLLCWithMem::handle_put(uint32_t idx) {
    SCCBundle &port = ports[idx];
    LineIndexT line = port.req_line;

    auto iter = state.find(line);
    if(iter != state.end()) {
        SCCHigherState &s = iter->second;
        if(s.owner == idx) {
            if(s.shared.size()) {
                s.owner = *(s.shared.begin());
                s.shared.erase(s.owner);
                if(debug) {
                    printf("%ld: L2 to L1-%d: 0x%lx, put own-shared\n", simroot::get_current_tick(), idx, port.req_line);
                }
                return 1;
            }
            if(debug) {
                printf("%ld: L2 to L1-%d: 0x%lx, put owned\n", simroot::get_current_tick(), idx, port.req_line);
            }
            state.erase(iter);
            CacheLineT *l2cl = nullptr;
            llc_cb->get_line(line, &l2cl, true);
            if(l2cl) {
                // l2 hit
                cache_line_copy(l2cl->data(), port.req_line_buf);
            } else {
                CacheLineT newline, replaced;
                LineIndexT replaced_line = 0;
                cache_line_copy(newline.data(), port.req_line_buf);
                if(llc_cb->insert_line(line, &newline, &replaced_line, &replaced)) {
                    cache_line_copy(mem.data() + (CACHE_LINE_LEN_BYTE * replaced_line - mem_base_addr), replaced.data());
                }
            }
            return (1 + llc_param.index_latency + bus_latency);
        } else if(s.shared.find(idx) != s.shared.end()) {
            if(debug) {
                printf("%ld: L2 to L1-%d: 0x%lx, put shared\n", simroot::get_current_tick(), idx, port.req_line);
            }
            s.shared.erase(idx);
            return 1;
        }
    }

    // invalided during put
    return 1;
}


}


#include "simroot.h"
#include "sccl1.h"

namespace simcache {


SimpleCoherentL1::SimpleCoherentL1(CacheParam &param, SCCBundle *port) : param(param), port(port) {
    cb = make_unique<GenericLRUCacheBlock<CacheLineT>>(param.set_offset, param.way_cnt);
}

void SimpleCoherentL1::on_current_tick() {
    if(port->req_previous != SCCReq::none && port->req_previous == port->req_next) {
        // This has been passed to slave
        port->req_previous = port->req_next = SCCReq::none;
    }

    if(port->snoop != SCCSnoop::none) {
        if(port->snoop == SCCSnoop::share || port->snoop == SCCSnoop::uniq) {
            if(waiting == SCCReq::put && wait_line == port->snoop_line) {
                cache_line_copy(port->snoop_line_buf, put_line_data.data());
                if(debug) {
                    printf("%ld: L1-%d: 0x%lx, snooped during put\n", simroot::get_current_tick(), port->index, port->snoop_line);
                }
            } else {
                if(debug) {
                    printf("%ld: L1-%d: 0x%lx, snooped\n", simroot::get_current_tick(), port->index, port->snoop_line);
                }
                CacheLineT *res = nullptr;
                simroot_assert(cb->get_line(port->snoop_line, &res, false));
                cache_line_copy(port->snoop_line_buf, res->data());
            }
            writeable.erase(port->snoop_line);
            if(port->snoop == SCCSnoop::uniq) {
                cb->remove_line(port->snoop_line);
            }
        } else {
            if(debug) {
                printf("%ld: L1-%d: 0x%lx, invalid\n", simroot::get_current_tick(), port->index, port->snoop_line);
            }
            writeable.erase(port->snoop_line);
            cb->remove_line(port->snoop_line);
        }
        port->snoop = SCCSnoop::none;
    }

    if((waiting == SCCReq::read || waiting == SCCReq::readu) && port->req_resp) {
        port->req_resp = 0;

        simroot_assert(port->req_line_next == wait_line);
        LineIndexT line = wait_line;
        CacheLineT newline;
        LineIndexT replaced_line = 0;
        cache_line_copy(newline.data(), port->req_line_buf);
        if(waiting == SCCReq::readu) {
            writeable.insert(line);
        }

        if(debug) {
            printf("%ld: L1-%d: 0x%lx, read respond\n", simroot::get_current_tick(), port->index, wait_line);
        }

        if(cb->insert_line(line, &newline, &replaced_line, &put_line_data)) {
            waiting = port->req_next = SCCReq::put;
            wait_line = port->req_line_next = replaced_line;
            writeable.erase(replaced_line);
            cache_line_copy(port->req_line_buf, put_line_data.data());
        } else {
            waiting = SCCReq::none;
        }

        ArrivalLine tmp;
        tmp.data = newline;
        tmp.lindex = line;
        tmp.readonly = (waiting == SCCReq::read);
        arrival.emplace_back(tmp);
    } else if(waiting == SCCReq::put && port->req_resp) {
        port->req_resp = 0;

        simroot_assert(port->req_line_next == wait_line);
        waiting = SCCReq::none;
        
        if(debug) {
            printf("%ld: L1-%d: 0x%lx, put finished\n", simroot::get_current_tick(), port->index, wait_line);
        }
    }
}

void SimpleCoherentL1::apply_next_tick() {
    arrival.clear();
    if(port->req_next != port->req_previous) {
        port->req = port->req_next;
        port->req_line = port->req_line_next;
    }
    port->req_previous = port->req_next;
}

SimError SimpleCoherentL1::load(PhysAddrT paddr, uint32_t len, void *buf, bool noblock) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = (paddr >> CACHE_LINE_ADDR_OFFSET);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));
    if((waiting == SCCReq::read || waiting == SCCReq::readu)) {
        if(wait_line == lindex) return SimError::miss;
        else return SimError::busy;
    }

    CacheLineT * p_line = nullptr;
    if(waiting == SCCReq::put && wait_line == lindex) {
        p_line = &put_line_data;
    } else {
        cb->get_line(lindex, &p_line, true);
    }

    if(p_line) {
        if(len) {
            memcpy(buf, ((uint8_t*)(p_line->data())) + offset, len);
        }
        return SimError::success;
    } else if(waiting == SCCReq::none) {
        if(debug) {
            printf("%ld: L1-%d: 0x%lx, request read\n", simroot::get_current_tick(), port->index, lindex);
        }
        waiting = port->req_next = SCCReq::read;
        wait_line = port->req_line_next = lindex;
        return SimError::miss;
    }
    return SimError::busy;
}

SimError SimpleCoherentL1::store(PhysAddrT paddr, uint32_t len, void *buf, bool noblock) {
    if(len != 0 && (paddr >> CACHE_LINE_ADDR_OFFSET) != ((paddr + len - 1) >> CACHE_LINE_ADDR_OFFSET)) return SimError::unaligned;

    LineIndexT lindex = (paddr >> CACHE_LINE_ADDR_OFFSET);
    SizeT offset = (paddr & (CACHE_LINE_LEN_BYTE - 1));
    if(waiting != SCCReq::none) {
        if(wait_line == lindex) return SimError::miss;
        else return SimError::busy;
    }

    CacheLineT * p_line = nullptr;
    cb->get_line(lindex, &p_line, true);
    bool wr = (writeable.find(lindex) != writeable.end());

    if(p_line && wr) {
        if(len) {
            memcpy(((uint8_t*)(p_line->data())) + offset, buf, len);
        }
        return SimError::success;
    }
    if(debug) {
        printf("%ld: L1-%d: 0x%lx, request readu\n", simroot::get_current_tick(), port->index, lindex);
    }
    waiting = port->req_next = SCCReq::readu;
    wait_line = port->req_line_next = lindex;
    return SimError::miss;
}


SimError SimpleCoherentL1::load_reserved(PhysAddrT paddr, uint32_t len, void *buf) {
    SimError ret = load(paddr, len, buf, false);
    if(ret == SimError::success) {
        reserved_address_valid = true;
        reserved_address = paddr;
    }
    return ret;
}

SimError SimpleCoherentL1::store_conditional(PhysAddrT paddr, uint32_t len, void *buf) {
    bool conditional = false;
    conditional = (reserved_address_valid && (reserved_address == paddr));
    SimError ret = SimError::unconditional;
    if(conditional) ret = store(paddr, len, buf, false);
    return ret;
}

uint32_t SimpleCoherentL1::arrival_line(vector<ArrivalLine> *out) {
    if(out) {
        out->swap(arrival);
        return out->size();
    }
    arrival.clear();
    return 0;
}

}

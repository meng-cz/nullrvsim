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


#include "trace.h"

#include "simroot.h"
#include "configuration.h"

namespace simcache {

unique_ptr<CacheEventTrace> global_event_trace_ptr;

CacheEventTrace *get_global_cache_event_trace() {
    if(!global_event_trace_ptr) [[unlikely]] {
        global_event_trace_ptr = make_unique<CacheEventTrace>();
        simroot::add_sim_object(global_event_trace_ptr.get(), "cache_event_trace", 1);
    }
    return global_event_trace_ptr.get();
}

CacheEventTrace::CacheEventTrace() {
    do_on_current_tick = do_apply_next_tick = 0;
    
    if(conf::get_int("cache", "do_event_log", 0)) {
        log = simroot::create_log_file(conf::get_str("cache", "event_log_file", "cache_event.log"), -1);
    }
}

CacheEventTrace::~CacheEventTrace() {
    if(log) {
        simroot::destroy_log_file(log);
    }
}

void CacheEventTrace::clear_statistic() {
    memset(&statistic, 0, sizeof(statistic));
}

void CacheEventTrace::print_statistic(std::ofstream &ofile) {
    #define CACHEEVENT_PRINT_STATIS(n) ofile << #n << "_cnt: " << statistic.n.cnt << "\n" << #n << "_tick: " << statistic.n.tick << "\n" ;
    #define CACHEEVENT_PRINT_AVG(n) ofile << #n << "_avg: " << statistic.n.val << "\n";

    CACHEEVENT_PRINT_STATIS(l1miss_l2hit);

    CACHEEVENT_PRINT_STATIS(l1miss_l2forward);
    CACHEEVENT_PRINT_AVG(l1miss_l2forward_l1_l2);
    CACHEEVENT_PRINT_AVG(l1miss_l2forward_l2_ol1);
    CACHEEVENT_PRINT_AVG(l1miss_l2forward_ol1_l1);

    CACHEEVENT_PRINT_STATIS(l1miss_l2miss_l3hit);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l1_l2);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l2_l3);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3_l2);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l2_l1);

    CACHEEVENT_PRINT_STATIS(l1miss_l2miss_l3forward);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3forward_l1_l2);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3forward_l2_l3);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3forward_l3_ol2);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3forward_ol2_l2);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3forward_l2_l1);
    
    CACHEEVENT_PRINT_STATIS(l1miss_l2miss_l3miss);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3miss_l1_l2);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3miss_l2_l3);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3miss_l3_mem);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3miss_mem_l2);
    CACHEEVENT_PRINT_AVG(l1miss_l2miss_l3miss_l2_l1);

    #undef CACHEEVENT_PRINT_STATIS
    #undef CACHEEVENT_PRINT_AVG
}

void CacheEventTrace::insert_event(uint32_t trans_id, CacheEvent event) {

    if(!trans_id) return;

    auto search = events.find(trans_id);
    uint64_t tick = simroot::get_current_tick();

    if(search == events.end()) {
        if(event == CacheEvent::L1_LD_MISS || event == CacheEvent::L1_ST_MISS) {
            auto iter = events.emplace(trans_id, vector<EventNode>()).first;
            iter->second.reserve(8);
            iter->second.emplace_back(EventNode{
                .tick = tick,
                .trans_id = trans_id,
                .event = event
            });
        }

        return;
    }

    auto &l = search->second;
    l.emplace_back(EventNode{
        .tick = tick,
        .trans_id = trans_id,
        .event = event
    });

    if(event != CacheEvent::L1_FINISH) {
        return;
    }

    if(log) {
        string str = to_string(tick) + ":" + to_string(trans_id) + ":";
        for(auto &e : l) {
            switch (e.event)
            {
            case CacheEvent::L1_LD_MISS: str += "L1_LD_MISS "; break;
            case CacheEvent::L1_ST_MISS: str += "L1_ST_MISS "; break;
            case CacheEvent::L1_FINISH: str += "L1_FINISH "; break;
            case CacheEvent::L1_TRANSMIT: str += "L1_TRANSMIT "; break;
            case CacheEvent::L2_HIT: str += "L2_HIT "; break;
            case CacheEvent::L2_MISS: str += "L2_MISS "; break;
            case CacheEvent::L2_FORWARD: str += "L2_FORWARD "; break;
            case CacheEvent::L2_TRANSMIT: str += "L2_TRANSMIT "; break;
            case CacheEvent::L2_FINISH: str += "L2_FINISH "; break;
            case CacheEvent::L3_HIT: str += "L3_HIT "; break;
            case CacheEvent::L3_MISS: str += "L3_MISS "; break;
            case CacheEvent::L3_FORWARD: str += "L3_FORWARD "; break;
            case CacheEvent::MEM_HANDLE: str += "MEM_HANDLE "; break;
            default: simroot_assert(0);
            }
        }
        simroot::log_line(log, str);
    }

    enum class TmpState {
        def,
        l1m,
        l1m_l2h,
        l1m_l2f,
        l1m_l2f_l1t,
        l1m_l2m,
        l1m_l2m_l3h,
        l1m_l2m_l3h_l2f,
        l1m_l2m_l3f,
        l1m_l2m_l3f_l2t,
        l1m_l2m_l3f_l2t_l2f,
        l1m_l2m_l3m,
        l1m_l2m_l3m_mem,
        l1m_l2m_l3m_mem_l2f,
    } state = TmpState::def;

    vector<uint64_t> intervals;
    bool finished = false;
    uint64_t cur = 0;
    auto push = [&](uint64_t t) -> void {
        simroot_assert(t >= cur);
        uint64_t i = t - cur;
        cur = t;
        intervals.push_back(i?i:1);
    };

    for(auto &e : l) {
        if(state == TmpState::def) {
            if(e.event == CacheEvent::L1_LD_MISS || e.event == CacheEvent::L1_ST_MISS) { cur = e.tick; state = TmpState::l1m; }
        }
        else if(state == TmpState::l1m) {
            if(e.event == CacheEvent::L2_HIT) { push(e.tick); state = TmpState::l1m_l2h; }
            else if(e.event == CacheEvent::L2_FORWARD) { push(e.tick); state = TmpState::l1m_l2f; }
            else if(e.event == CacheEvent::L2_MISS) { push(e.tick); state = TmpState::l1m_l2m; }
        }
        else if(state == TmpState::l1m_l2h) {
            if(e.event == CacheEvent::L1_FINISH) { push(e.tick); finished = true; break; }
        }
        else if(state == TmpState::l1m_l2f) {
            if(e.event == CacheEvent::L1_TRANSMIT) { push(e.tick); state = TmpState::l1m_l2f_l1t; }
        }
        else if(state == TmpState::l1m_l2f_l1t) {
            if(e.event == CacheEvent::L1_FINISH) { push(e.tick); finished = true; break; }
        }
        else if(state == TmpState::l1m_l2m) {
            if(e.event == CacheEvent::L3_HIT) { push(e.tick); state = TmpState::l1m_l2m_l3h; }
            else if(e.event == CacheEvent::L3_FORWARD) { push(e.tick); state = TmpState::l1m_l2m_l3f; }
            else if(e.event == CacheEvent::L3_MISS) { push(e.tick); state = TmpState::l1m_l2m_l3m; }
        }
        else if(state == TmpState::l1m_l2m_l3h) {
            if(e.event == CacheEvent::L2_FINISH) { push(e.tick); state = TmpState::l1m_l2m_l3h_l2f; }
        }
        else if(state == TmpState::l1m_l2m_l3h_l2f) {
            if(e.event == CacheEvent::L1_FINISH) { push(e.tick); finished = true; break; }
        }
        else if(state == TmpState::l1m_l2m_l3f) {
            if(e.event == CacheEvent::L2_TRANSMIT) { push(e.tick); state = TmpState::l1m_l2m_l3f_l2t; }
        }
        else if(state == TmpState::l1m_l2m_l3f_l2t) {
            if(e.event == CacheEvent::L2_FINISH) { push(e.tick); state = TmpState::l1m_l2m_l3f_l2t_l2f; }
        }
        else if(state == TmpState::l1m_l2m_l3f_l2t_l2f) {
            if(e.event == CacheEvent::L1_FINISH) { push(e.tick); finished = true; break; }
        }
        else if(state == TmpState::l1m_l2m_l3m) {
            if(e.event == CacheEvent::MEM_HANDLE) { push(e.tick); state = TmpState::l1m_l2m_l3m_mem; }
        }
        else if(state == TmpState::l1m_l2m_l3m_mem) {
            if(e.event == CacheEvent::L2_FINISH) { push(e.tick); state = TmpState::l1m_l2m_l3m_mem_l2f; }
        }
        else if(state == TmpState::l1m_l2m_l3m_mem_l2f) {
            if(e.event == CacheEvent::L1_FINISH) { push(e.tick); finished = true; break; }
        }
        else {
            simroot_assert(0);
        }
    }

    auto sum = [&](int cnt) -> uint64_t {
        uint64_t sum = 0;
        for(int i = 0; i < cnt; i++) {
            sum += intervals[i];
        }
        return sum;
    };
    auto to_perc = [&](int cnt) -> vector<double> {
        vector<double> ret(cnt);
        double s = sum(cnt);
        for(int i = 0; i < cnt; i++) {
            ret[i] = intervals[i] / s;
        }
        return ret;
    };

    if(finished) {
        if(state == TmpState::l1m_l2h) {
            statistic.l1miss_l2hit.cnt++;
            statistic.l1miss_l2hit.tick += sum(2);
        }
        else if(state == TmpState::l1m_l2f_l1t) {
            statistic.l1miss_l2forward.cnt++;
            statistic.l1miss_l2forward.tick += sum(3);
            vector<double> perc = to_perc(3);
            statistic.l1miss_l2forward_l1_l2.insert(perc[0]);
            statistic.l1miss_l2forward_l2_ol1.insert(perc[1]);
            statistic.l1miss_l2forward_ol1_l1.insert(perc[2]);
        }
        else if(state == TmpState::l1m_l2m_l3h_l2f) {
            statistic.l1miss_l2miss_l3hit.cnt++;
            statistic.l1miss_l2miss_l3hit.tick += sum(4);
            vector<double> perc = to_perc(4);
            statistic.l1miss_l2miss_l1_l2.insert(perc[0]);
            statistic.l1miss_l2miss_l2_l3.insert(perc[1]);
            statistic.l1miss_l2miss_l3_l2.insert(perc[2]);
            statistic.l1miss_l2miss_l2_l1.insert(perc[3]);
        }
        else if(state == TmpState::l1m_l2m_l3f_l2t_l2f) {
            statistic.l1miss_l2miss_l3forward.cnt++;
            statistic.l1miss_l2miss_l3forward.tick += sum(5);
            vector<double> perc = to_perc(5);
            statistic.l1miss_l2miss_l3forward_l1_l2.insert(perc[0]);
            statistic.l1miss_l2miss_l3forward_l2_l3.insert(perc[1]);
            statistic.l1miss_l2miss_l3forward_l3_ol2.insert(perc[2]);
            statistic.l1miss_l2miss_l3forward_ol2_l2.insert(perc[3]);
            statistic.l1miss_l2miss_l3forward_l2_l1.insert(perc[4]);
        }
        else if(state == TmpState::l1m_l2m_l3m_mem_l2f) {
            statistic.l1miss_l2miss_l3miss.cnt++;
            statistic.l1miss_l2miss_l3miss.tick += sum(5);
            vector<double> perc = to_perc(5);
            statistic.l1miss_l2miss_l3miss_l1_l2.insert(perc[0]);
            statistic.l1miss_l2miss_l3miss_l2_l3.insert(perc[1]);
            statistic.l1miss_l2miss_l3miss_l3_mem.insert(perc[2]);
            statistic.l1miss_l2miss_l3miss_mem_l2.insert(perc[3]);
            statistic.l1miss_l2miss_l3miss_l2_l1.insert(perc[4]);
        }
    }

    events.erase(search);
}

void CacheEventTrace::cancel_transaction(uint32_t trans_id) {
    events.erase(trans_id);
}





}

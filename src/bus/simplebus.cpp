
#include "simplebus.h"

#include "configuration.h"
#include "simroot.h"

namespace simbus {

SimpleBus::SimpleBus(uint16_t port_num, uint32_t bus_width) {
    do_on_current_tick = 0;
    do_apply_next_tick = 2;

    num = port_num;
    width = bus_width;

    ports = new SimpleBusPort[port_num];

    do_log = conf::get_int("bus", "log_info_to_stdout", 0);

    if(conf::get_int("bus", "log_info_to_file", 0)) {
        logfile = new std::ofstream(log_name + "_log.txt");
    }
}

SimpleBus::~SimpleBus() {
    if(ports) delete[] ports;
    if(logfile) delete logfile;
}

bool SimpleBus::can_send(BusPortT port) {
    if(port >= num) return false;
    SimpleBusPort &p = ports[port];
    p.lock.lock();
    bool ret = !p.send_valid;
    p.lock.unlock();
    return ret;
}

bool SimpleBus::send(BusPortT port, BusPortT dst_port, CacheCohenrenceMsg msg) {
    if(port >= num || dst_port >= num) return false;
    SimpleBusPort &p = ports[port];
    bool ret = false;
    p.lock.lock();
    if(!p.send_valid) {
        p.send_valid = true;
        ret = true;
        uint16_t cnt = CEIL_DIV(msg.data.size(), width) + 1;
        p.send_packs.clear();
        p.send_packs.emplace_back();
        {
        auto &pack = p.send_packs.back();
        pack.src = port;
        pack.dst = dst_port;
        pack.total = cnt;
        pack.current = 0;
        pack.payload.head.type = msg.type;
        pack.payload.head.line = msg.line;
        pack.payload.head.arg = msg.arg;
        }
        uint32_t dsz = 0;
        for(int i = 1; i < cnt; i++) {
            p.send_packs.emplace_back();
            auto &pack = p.send_packs.back();
            pack.src = port;
            pack.dst = dst_port;
            pack.total = cnt;
            pack.current = i;
            uint32_t sz = std::min<uint32_t>(msg.data.size() - dsz, width);
            pack.payload.data.assign(sz, 0);
            memcpy(pack.payload.data.data(), msg.data.data() + dsz, sz);
            dsz += sz;
        }
    }
    p.lock.unlock();
    return ret;
}

bool SimpleBus::can_recv(BusPortT port) {
    if(port >= num) return false;
    SimpleBusPort &p = ports[port];
    p.lock.lock();
    bool ret = !p.recv_valid;
    p.lock.unlock();
    return ret;
}

bool SimpleBus::recv(BusPortT port, CacheCohenrenceMsg *msg_buf) {
    if(port >= num) return false;
    SimpleBusPort &p = ports[port];
    bool ret = false;
    p.lock.lock();
    if(p.recv_valid && msg_buf) {
        p.recv_valid = false;
        ret = true;
        {
        auto &pack = p.recv_packs.front();
        msg_buf->type = pack.payload.head.type;
        msg_buf->line = pack.payload.head.line;
        msg_buf->arg = pack.payload.head.arg;
        msg_buf->data.clear();
        }
        p.recv_packs.pop_front();
        for(auto &pack : p.recv_packs) {
            msg_buf->data.insert(msg_buf->data.end(), pack.payload.data.begin(), pack.payload.data.end());
        }
        p.recv_packs.clear();
    }
    p.lock.unlock();
    return ret;
}

void SimpleBus::apply_next_tick() {
    bool busy_cycle = false;
    if(last_op_port.first) {
        SimpleBusPort &p = ports[last_op_port.second];
        auto &pack = p.send_packs.front();
        SimpleBusPort &dp = ports[pack.dst];
        assert(!dp.recv_valid);
        if(do_log) {
            sprint_package_to_log_buf(&pack);
            simroot::print_log_info(log_buf);
        }
        dp.recv_packs.emplace_back(pack);
        if(pack.current + 1 == pack.total) {
            p.send_valid = false;
            dp.recv_valid = true;
            last_op_port.first = false;
        }
        p.send_packs.pop_front();
        busy_cycle = true;
    }
    else {
        for(uint16_t i = 0; i < num; i++) {
            SimpleBusPort &p = ports[i];
            if(!p.send_valid) {
                continue;
            }
            auto &pack = p.send_packs.front();
            SimpleBusPort &dp = ports[pack.dst];
            if(dp.recv_valid) {
                continue;
            }
            if(do_log) {
                sprint_package_to_log_buf(&pack);
                simroot::print_log_info(log_buf);
            }
            if(logfile) {
                sprint_package_to_log_buf_simple(&pack);
                (*logfile) << string(log_buf) << std::endl;
            }
            dp.recv_packs.emplace_back(pack);
            if(pack.current + 1 == pack.total) {
                p.send_valid = false;
                dp.recv_valid = true;
                last_op_port.first = false;
            }
            else {
                last_op_port.first = true;
                last_op_port.second = i;
            }
            p.send_packs.pop_front();
            busy_cycle = true;
            break;
        }
    }
    
    if(busy_cycle) {
        statistic.busy_cycle_cnt++;
    }
    else {
        statistic.free_cycle_cnt++;
    }
}

void SimpleBus::print_statistic(std::ofstream &ofile) {
    #define GENERATE_PRINTSTATISTIC(n) ofile << #n << ": " << statistic.n << "\n" ;
    GENERATE_PRINTSTATISTIC(busy_cycle_cnt);
    GENERATE_PRINTSTATISTIC(free_cycle_cnt);
    #undef GENERATE_PRINTSTATISTIC
}

void SimpleBus::print_setup_info(std::ofstream &ofile) {
    #define GENERATE_PRINTSETUP(s, n) ofile << #s << ": " << n << "\n" ;
    GENERATE_PRINTSETUP(port_number, num);
    GENERATE_PRINTSETUP(bus_width, width);
    #undef GENERATE_PRINTSETUP
}


}

namespace test {

using simbus::SimpleBus;

using simcache::CacheCohenrenceMsg;
using simcache::CCMsgType;

bool test_simple_bus() {
    SimpleBus bus(16, 32);
    bus.do_log = true;

    for(int i = 0; i < 16; i++) {
        printf("Send %d\n", i);
        CacheCohenrenceMsg msg;
        msg.type = CCMsgType::putm;
        msg.line = i;
        msg.arg = i;
        msg.data.assign(CACHE_LINE_LEN_BYTE, i);
        assert(bus.send(i, 15-i, msg));
    }
    uint32_t cnt = 0;
    uint64_t tick = 0;
    CacheCohenrenceMsg buf;
    while(cnt < 16) {
        bus.on_current_tick();
        bus.apply_next_tick();
        tick++;

        for(int i = 0; i < 16; i++) {
            if(bus.recv(i, &buf)) {
                printf("%ld: Recv %d\n", tick, i);
                assert(buf.line == (15-i));
                assert(buf.arg == (15-i));
                assert(buf.data.size() == CACHE_LINE_LEN_BYTE);
                for(int n = 0; n < CACHE_LINE_LEN_BYTE; n++) {
                    assert(buf.data[n] == (15-i));
                }
                cnt++;
            }
        }
    }
    for(int i = 0; i < 16; i++) {
        printf("Send %d\n", i);
        CacheCohenrenceMsg msg;
        msg.type = CCMsgType::putm;
        msg.line = i;
        msg.arg = i;
        msg.data.assign(CACHE_LINE_LEN_BYTE, i);
        assert(bus.send(i, 15-i, msg));
    }
    cnt = 0;
    while(cnt < 16) {
        bus.on_current_tick();
        bus.apply_next_tick();
        tick++;

        for(int i = 0; i < 16; i++) {
            if(bus.recv(i, &buf)) {
                printf("%ld: Recv %d\n", tick, i);
                assert(buf.line == (15-i));
                assert(buf.arg == (15-i));
                assert(buf.data.size() == CACHE_LINE_LEN_BYTE);
                for(int n = 0; n < CACHE_LINE_LEN_BYTE; n++) {
                    assert(buf.data[n] == (15-i));
                }
                cnt++;
            }
        }
    }
    return true;
}

}

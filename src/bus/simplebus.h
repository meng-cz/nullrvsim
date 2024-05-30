#ifndef RVSIM_BUS_SIMPLE_BUS_H
#define RVSIM_BUS_SIMPLE_BUS_H

#include "businterface.h"

#include "cache/cacheinterface.h"

#include "common.h"
#include "spinlocks.h"

namespace simbus {

using simcache::CacheLineState;
using simcache::CCMsgType;

class SimpleBus : public BusInterface, public SimObject {
public:
    
    SimpleBus(uint16_t port_num, uint32_t bus_width);
    ~SimpleBus();

    virtual uint16_t get_port_num() { return num; };
    virtual uint16_t get_bus_width() { return width; };

    virtual bool can_send(BusPortT port);
    virtual bool send(BusPortT port, BusPortT dst_port, CacheCohenrenceMsg msg);
    // virtual bool send_multi(uint16_t port, std::vector<BusPortT> &dst_ports, CacheCohenrenceMsg msg);

    virtual bool can_recv(BusPortT port);
    virtual bool recv(BusPortT port, CacheCohenrenceMsg *msg_buf);

    virtual void apply_next_tick();

    virtual void clear_statistic() { memset(&statistic, 0, sizeof(statistic)); };
    virtual void print_statistic(std::ofstream &ofile);
    virtual void print_setup_info(std::ofstream &ofile);

    bool do_log = false;
private:
    uint16_t num;
    uint32_t width;

    typedef struct {
        CCMsgType   type;
        uint32_t    arg;
        LineIndexT  line;
    } PackageHead;

    typedef struct {
        PackageHead             head;
        std::vector<uint8_t>    data;
    } PackagePayload;

    typedef struct {
        BusPortT    src;
        BusPortT    dst;
        uint16_t    current;
        uint16_t    total;
        PackagePayload  payload;
    } Package;

    typedef struct {
        std::list<Package> send_packs;
        std::list<Package> recv_packs;
        bool send_valid = false;
        bool recv_valid = false;
        SpinLock lock;
    } SimpleBusPort;

    SimpleBusPort *ports = nullptr;

    std::pair<bool, BusPortT> last_op_port;

    struct {
        uint64_t busy_cycle_cnt = 0;
        uint64_t free_cycle_cnt = 0;
    } statistic;

    inline void sprint_package_to_log_buf_simple(Package *pack) {
        if(pack->current == 0) {
            sprintf(log_buf, "%d->%d: %s @line0x%lx arg:%d",
                pack->src, pack->dst,
                simcache::get_cc_msg_type_name_str(pack->payload.head.type).c_str(),
                pack->payload.head.line,
                pack->payload.head.arg
            );
        }
        else {
            sprintf(log_buf, "%d->%d: ", pack->src, pack->dst);
            string info = log_buf;
            for(int i = 0; i < width / 2; i++) {
                sprintf(log_buf, "%02x ", pack->payload.data[i]);
                info += log_buf;
            }
            strcpy(log_buf, info.c_str());
        }
    }

    inline void sprint_package_to_log_buf(Package *pack) {
        if(pack->current == 0) {
            sprintf(log_buf, "%s: %d->%d (%d/%d): %s @line0x%lx arg:%d",
                log_name.c_str(),
                pack->src, pack->dst, pack->current, pack->total,
                simcache::get_cc_msg_type_name_str(pack->payload.head.type).c_str(),
                pack->payload.head.line,
                pack->payload.head.arg
            );
        }
        else {
            sprintf(log_buf, "%s: %d->%d (%d/%d): ", log_name.c_str(), pack->src, pack->dst, pack->current, pack->total);
            string info = log_buf;
            for(int i = 0; i < width / 2; i++) {
                sprintf(log_buf, "%02x ", pack->payload.data[i]);
                info += log_buf;
            }
            strcpy(log_buf, info.c_str());
        }
    }

    string log_name = "simple_bus";
    char log_buf[256];

    std::ofstream *logfile = nullptr;
};


}

namespace test {

bool test_simple_bus();

}

#endif

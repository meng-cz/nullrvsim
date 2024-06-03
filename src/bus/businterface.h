#ifndef RVSIM_BUS_INTERFACE_H
#define RVSIM_BUS_INTERFACE_H

#include "common.h"

#include "cache/coherence.h"

namespace simbus {

typedef uint32_t BusPortT;
typedef uint32_t ChannelT;

using simcache::CacheCohenrenceMsg;

class BusInterfaceV2 {
public:
    virtual uint32_t get_port_num() = 0;
    virtual uint32_t get_channel_num() = 0;

    virtual bool can_send(BusPortT port, ChannelT channel) = 0;
    virtual bool send(BusPortT port, BusPortT dst_port, ChannelT channel, CacheCohenrenceMsg msg) = 0;

    virtual bool can_recv(BusPortT port, ChannelT channel) = 0;
    virtual bool recv(BusPortT port, ChannelT channel, CacheCohenrenceMsg *msg_buf) = 0;

};

class BusInterface {
public:
    virtual uint16_t get_port_num() = 0;
    virtual uint16_t get_bus_width() = 0;

    virtual bool can_send(BusPortT port) = 0;
    virtual bool send(BusPortT port, BusPortT dst_port, CacheCohenrenceMsg msg) = 0;

    // virtual bool send_multi(BusPortT port, std::vector<BusPortT> dst_ports, CacheCohenrenceMsg msg) = 0;

    virtual bool can_recv(BusPortT port) = 0;
    virtual bool recv(BusPortT port, CacheCohenrenceMsg *msg_buf) = 0;
};

class BusRoute {
public:
    virtual BusPortT next(BusPortT this_port, BusPortT dst_port) = 0;
};

class BusPortMapping {
public:
    virtual bool get_uplink_port(LineIndexT line, BusPortT *out) = 0;
    virtual bool get_memory_port(LineIndexT line, BusPortT *out) = 0;

    virtual bool get_downlink_port(uint32_t index, BusPortT *out) = 0;
    virtual bool get_downlink_port_index(BusPortT port, uint32_t *out) = 0;

};

}


#endif

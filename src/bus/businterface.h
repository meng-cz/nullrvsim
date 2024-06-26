#ifndef RVSIM_BUS_INTERFACE_H
#define RVSIM_BUS_INTERFACE_H

#include "common.h"

namespace simbus {

typedef uint32_t BusPortT;
typedef uint32_t ChannelT;

class BusInterfaceV2 : public SimObject {
public:
    virtual void can_send(BusPortT port, vector<bool> &out) = 0;
    virtual bool can_send(BusPortT port, ChannelT channel) = 0;
    virtual bool send(BusPortT port, BusPortT dst_port, ChannelT channel, vector<uint8_t> &data) = 0;

    virtual void can_recv(BusPortT port, vector<bool> &out) = 0;
    virtual bool can_recv(BusPortT port, ChannelT channel) = 0;
    virtual bool recv(BusPortT port, ChannelT channel, vector<uint8_t> &buf) = 0;
};

typedef BusPortT SrcPortT;
typedef BusPortT DstPortT;
typedef unordered_map<SrcPortT, unordered_map<DstPortT, BusPortT>> BusRouteTable;

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

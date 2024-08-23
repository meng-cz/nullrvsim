#ifndef RVSIM_BUS_INTERFACE_H
#define RVSIM_BUS_INTERFACE_H

#include "common.h"

namespace simbus {

typedef uint32_t BusNodeT; // 一个节点可以有多个端口，同节点不同端口间通讯不需要额外转发
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

typedef BusNodeT SrcNodeT;
typedef BusNodeT DstNodeT;
typedef unordered_map<SrcNodeT, unordered_map<DstNodeT, BusNodeT>> BusRouteTable;

class BusRoute {
public:
    virtual BusNodeT next(BusNodeT this_port, BusNodeT dst_port) = 0;
};

class BusPortMapping {
public:
    virtual bool get_homenode_port(LineIndexT line, BusPortT *out) = 0;
    virtual bool get_subnode_port(LineIndexT line, BusPortT *out) = 0;

    virtual bool get_reqnode_port(uint32_t index, BusPortT *out) = 0;
    virtual bool get_reqnode_index(BusPortT port, uint32_t *out) = 0;

};

}


#endif

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

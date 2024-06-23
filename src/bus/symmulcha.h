#ifndef RVSIM_BUS_SYMMETRIC_MULTI_CHANNEL_H
#define RVSIM_BUS_SYMMETRIC_MULTI_CHANNEL_H

#include "businterface.h"

namespace simbus {

typedef uint32_t XmitIDT;

/**
 * 对称多通道的抽象物理层总线模拟实现
 * 包含N个对称的节点，C个不同宽度的通道
 * 节点间的连接方式通过路由表指定
 * 每一Tick内，每两个相连的节点可以全双工的传输一次数据
 */
class SymmetricMultiChannelBus : public BusInterfaceV2 {

public:

    SymmetricMultiChannelBus(
        vector<BusPortT> &node_ids,
        vector<uint32_t> &channels_width_byte,
        BusRouteTable &route,
        string name
    );

    virtual bool can_send(BusPortT port, ChannelT channel);
    virtual bool send(BusPortT port, BusPortT dst_port, ChannelT channel, vector<uint8_t> &data);

    virtual bool can_recv(BusPortT port, ChannelT channel);
    virtual bool recv(BusPortT port, ChannelT channel, vector<uint8_t> &buf);

    virtual void apply_next_tick();

    virtual void clear_statistic();
    virtual void print_statistic(std::ofstream &ofile);
    virtual void print_setup_info(std::ofstream &ofile);
    virtual void dump_core(std::ofstream &ofile);

protected:
    string logname;
    char log_buf[512];

    uint32_t cha_cnt = 0;
    uint32_t width = 0;
    uint32_t route_latency = 0;
    uint32_t node_buf_sz = 0;
    uint32_t xmtid_alloc = 0;
    vector<BusPortT> nodeid;
    vector<uint32_t> cha_widths;
    BusRouteTable route;
    
    typedef struct {
        BusPortT        src;
        BusPortT        tgt;
        ChannelT        cha;
        XmitIDT         xmtid;
        uint32_t        len;
        uint16_t        pac_idx;
        uint16_t        pac_cnt;
        vector<uint8_t> data;
    } MsgPack;

    typedef struct {
        MsgPack         *pack;
        uint32_t        cycle_remained;
    } PipelinedPack;

    typedef struct {
        vector<MsgPack*>    input;
        vector<MsgPack*>    output;
    } EdgeInChannel;

    typedef struct {
        BusPortT                    myid = 0;
        unordered_map<DstPortT, EdgeInChannel*> txs;
        vector<EdgeInChannel*>      rxs;
        uint32_t                    rx_ptr = 0;
        list<MsgPack*>              xmit_buf;
        uint32_t                    xmit_buf_size = 0;
        list<PipelinedPack>         pipeline;
        unordered_map<XmitIDT, list<MsgPack*>>  order_buf; // 为了简化模拟，这里将接收缓存设置成无限大，该缓存的实际上限由来源节点的转发速度限制
        vector<list<MsgPack*>>      recv_buf;
        vector<list<MsgPack*>>      send_buf;
    } NodeInChannel;

    unordered_map<BusPortT, NodeInChannel> nodes;
    vector<EdgeInChannel>   edges;

    void process_node(NodeInChannel *node);
    void process_edge(EdgeInChannel *edge);

};


}

namespace test {

bool test_sym_mul_cha_bus();

}

#endif


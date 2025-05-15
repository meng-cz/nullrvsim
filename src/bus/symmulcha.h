#ifndef RVSIM_BUS_SYMMETRIC_MULTI_CHANNEL_H
#define RVSIM_BUS_SYMMETRIC_MULTI_CHANNEL_H

#include "businterface.h"
#include "simroot.h"

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
        vector<BusPortT> &port_ids,
        vector<BusNodeT> &port_to_node,
        vector<uint32_t> &channels_width_byte,
        BusRouteTable &route,
        string name
    );

    virtual void can_send(BusPortT port, vector<bool> &out);
    virtual bool can_send(BusPortT port, ChannelT channel);
    virtual bool send(BusPortT port, BusPortT dst_port, ChannelT channel, vector<uint8_t> &data);

    virtual void can_recv(BusPortT port, vector<bool> &out);
    virtual bool can_recv(BusPortT port, ChannelT channel);
    virtual bool recv(BusPortT port, ChannelT channel, vector<uint8_t> &buf);

    virtual void apply_next_tick();

    virtual void clear_statistic();
    virtual void print_statistic(std::ofstream &ofile);
    virtual void print_setup_info(std::ofstream &ofile);
    virtual void dump_core(std::ofstream &ofile);

protected:

    uint32_t cha_cnt = 0;
    uint32_t width = 0;
    uint32_t route_latency = 0;
    uint32_t node_buf_sz = 0;
    uint32_t xmtid_alloc = 0;
    vector<uint32_t> cha_widths;
    vector<BusNodeT> init_port_to_node;
    unordered_map<BusPortT, BusNodeT> port2node;
    BusRouteTable route;
    
    typedef struct {
        BusPortT        src;
        BusPortT        dst;
        BusNodeT        tgt;
        ChannelT        cha;
        XmitIDT         xmtid;
        uint32_t        len;
        uint16_t        pac_idx;
        uint16_t        pac_cnt;
        vector<uint8_t> data;
        uint64_t        tx_start_tick;
    } MsgPack;

    typedef struct {
        MsgPack         *pack;
        uint32_t        cycle_remained;
    } PipelinedPack;

    typedef struct {
        vector<MsgPack*>    input;
        vector<MsgPack*>    output;
        uint32_t            from = 0;
        uint32_t            to = 0;
        uint64_t            busy_cycles = 0;
    } EdgeInChannel;

    typedef struct {
        vector<list<MsgPack*>>      recv_buf;
        vector<list<MsgPack*>>      send_buf;
    } PortStruct;

    typedef struct {
        BusNodeT                    myid = 0;

        unordered_map<DstNodeT, EdgeInChannel*> txs;
        vector<EdgeInChannel*>      rxs;
        uint32_t                    rx_ptr = 0;

        list<MsgPack*>              xmit_buf;
        uint32_t                    xmit_buf_size = 0;
        list<PipelinedPack>         pipeline;

        unordered_map<XmitIDT, list<MsgPack*>>  order_buf; // 为了简化模拟，这里将接收缓存设置成无限大，该缓存的实际上限由来源节点的转发速度限制
        
        unordered_map<BusPortT, PortStruct*> port;
        unordered_map<BusPortT, PortStruct*>::iterator sd_ptr;

        uint64_t        busy_cycles = 0;
        uint64_t        passed_packs = 0;
    } NodeStruct;

    unordered_map<BusNodeT, NodeStruct> nodes;
    unordered_map<BusPortT, PortStruct> ports;
    vector<EdgeInChannel>   edges;
    
    void process_node(NodeStruct *node);
    void process_edge(EdgeInChannel *edge);

    uint64_t tx_pack_num = 0;
    uint64_t tx_pack_cycle_sum = 0;
    unordered_map<uint64_t, uint64_t> transmit_cnt;

    string logname;
    simroot::LogFileT log_ofile = nullptr;
    char log_buf[512];

};


}

namespace test {

bool test_sym_mul_cha_bus();

}

#endif


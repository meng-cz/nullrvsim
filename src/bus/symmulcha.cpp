
#include "symmulcha.h"
#include "routetable.h"

#include "simroot.h"
#include "configuration.h"

namespace simbus {


SymmetricMultiChannelBus::SymmetricMultiChannelBus(
    vector<BusPortT> &node_ids,
    vector<uint32_t> &channels_width_byte,
    BusRouteTable &route,
    string name
)
: nodeid(node_ids), cha_widths(channels_width_byte), route(route), logname(name)
{

    width = conf::get_int("symmulcha", "width", 64);
    route_latency = conf::get_int("symmulcha", "route_latency", 3);
    node_buf_sz = conf::get_int("symmulcha", "node_buf_sz", 512);

    cha_cnt = cha_widths.size();

    for(auto src : nodeid) {
        for(auto dst : nodeid) {
            uint32_t max_jmp = nodeid.size();
            BusPortT cur = src;
            for(uint32_t i = 0; i < max_jmp && cur != dst; i++) {
                auto res1 = route.find(cur);
                if(res1 == route.end()) break;
                auto res2 = res1->second.find(dst);
                if(res2 == res1->second.end()) break;
                cur = res2->second;
            }
            simroot_assertf(cur == dst, "Bus: Route Check Failed: %d -> %d Unreachable", src, dst);
        }
    }

    set<std::pair<BusPortT, BusPortT>> all_edges;
    for(auto &e1 : route) {
        BusPortT src = e1.first;
        for(auto &e2 : e1.second) {
            BusPortT dst = e2.first;
            BusPortT conn = e2.second;
            all_edges.emplace(src, conn);
            all_edges.emplace(conn, src);
        }
    }
    assert(all_edges.size() % 2 == 0);

    for(auto n : nodeid) {
        auto iter = nodes.emplace(n, NodeInChannel()).first;
        auto &tmp = iter->second;
        tmp.myid = n;
        tmp.send_buf.resize(cha_cnt);
        tmp.recv_buf.resize(cha_cnt);
    }

    EdgeInChannel empty_cha;
    empty_cha.input.assign(cha_cnt, nullptr);
    empty_cha.output.assign(cha_cnt, nullptr);
    edges.assign(all_edges.size(), empty_cha);
    uint64_t alloc = 0;
    for(auto &e : all_edges) {
        edges[alloc].input.assign(cha_cnt, nullptr);
        edges[alloc].output.assign(cha_cnt, nullptr);
        nodes[e.first].txs.emplace(e.second, &(edges[alloc]));
        nodes[e.second].rxs.push_back(&(edges[alloc]));
        alloc++;
    }
}


void SymmetricMultiChannelBus::clear_statistic() {

}

#define LOGTOFILE(fmt, ...) do{sprintf(log_buf, fmt, ##__VA_ARGS__);ofile << log_buf;}while(0)

void SymmetricMultiChannelBus::print_statistic(std::ofstream &ofile) {
    
}

void SymmetricMultiChannelBus::print_setup_info(std::ofstream &ofile) {
    
}

void SymmetricMultiChannelBus::dump_core(std::ofstream &ofile) {
    
}

#undef LOGTOFILE

void SymmetricMultiChannelBus::apply_next_tick() {
    for(auto &e : nodes) {
        process_node(&(e.second));
    }
    for(auto &e : edges) {
        process_edge(&e);
    }
}



bool SymmetricMultiChannelBus::can_send(BusPortT port, ChannelT channel) {
    simroot_assertf(channel < cha_widths.size(), "Bus: Unknown channel index %d", channel);
    auto res = nodes.find(port);
    simroot_assertf(res != nodes.end(), "Bus: Unknown port %d", channel);
    list<MsgPack*> &sbuf = res->second.send_buf[channel];
    return (sbuf.empty());
}

bool SymmetricMultiChannelBus::send(BusPortT port, BusPortT dst_port, ChannelT channel, vector<uint8_t> &data) {
    simroot_assertf(channel < cha_widths.size(), "Bus: Unknown channel index %d", channel);
    auto res = nodes.find(port);
    simroot_assertf(res != nodes.end(), "Bus: Unknown port %d", channel);

    list<MsgPack*> &sbuf = res->second.send_buf[channel];
    if(!sbuf.empty()) [[unlikely]] return false;

    uint32_t cwid = cha_widths[channel];
    uint32_t len = ALIGN(data.size(), cwid);
    uint32_t pac_cnt = len / cwid;
    uint32_t pos = 0;
    uint32_t xmt = (xmtid_alloc++);
    for(uint32_t i = 0; i < pac_cnt; i++) {
        MsgPack *p = new MsgPack();
        p->src = port;
        p->tgt = dst_port;
        p->cha = channel;
        p->xmtid = xmt;
        p->len = data.size();
        p->pac_idx = i;
        p->pac_cnt = pac_cnt;
        p->data.assign(cwid, 0);
        memcpy(p->data.data(), data.data() + pos, std::min<uint32_t>(cwid, data.size() - pos));
        pos += cwid;
        sbuf.push_back(p);
    }
    return true;
}

bool SymmetricMultiChannelBus::can_recv(BusPortT port, ChannelT channel) {
    simroot_assertf(channel < cha_widths.size(), "Bus: Unknown channel index %d", channel);
    auto res = nodes.find(port);
    simroot_assertf(res != nodes.end(), "Bus: Unknown port %d", channel);
    list<MsgPack*> &rbuf = res->second.recv_buf[channel];
    return (!rbuf.empty() && rbuf.size() >= rbuf.front()->pac_cnt);
}

bool SymmetricMultiChannelBus::recv(BusPortT port, ChannelT channel, vector<uint8_t> &buf) {
    simroot_assertf(channel < cha_widths.size(), "Bus: Unknown channel index %d", channel);
    auto res = nodes.find(port);
    simroot_assertf(res != nodes.end(), "Bus: Unknown port %d", channel);
    list<MsgPack*> &rbuf = res->second.recv_buf[channel];
    if(rbuf.empty() || rbuf.size() < rbuf.front()->pac_cnt) [[unlikely]] return false;

    uint32_t cwid = cha_widths[channel];
    MsgPack *head = rbuf.front();
    uint32_t len = head->len;
    uint32_t pac_cnt = head->pac_cnt;
    buf.resize(ALIGN(len, cwid));
    uint32_t pos = 0;
    for(uint32_t i = 0; i < pac_cnt; i++) {
        MsgPack *p = rbuf.front();
        simroot_assertf(i == p->pac_idx, "Bus: Un-ordered package sequence in port %d, channel %d", port, channel);
        memcpy(buf.data() + pos, p->data.data(), cwid);
        pos += cwid;
        delete p;
        rbuf.pop_front();
    }
    buf.resize(len);
    return true;
}


void SymmetricMultiChannelBus::process_node(NodeInChannel *node) {
    
    MsgPack *recv = nullptr;
    for(int i = 0; i < node->rxs.size(); i++) {
        EdgeInChannel *e = node->rxs[node->rx_ptr];
        for(int c = 0; c < cha_cnt; c++) {
            if(e->output[c]) {
                recv = e->output[c];
                e->output[c] = nullptr;
                break;
            }
        }
        if(recv) {
            break;
        }
        else {
            node->rx_ptr = (node->rx_ptr + 1) % node->rxs.size();
        }
    }

    if(!recv) {
        for(auto &l : node->send_buf) {
            if(!l.empty()) {
                recv = l.front();
                l.pop_front();
                break;
            }
        }
    }

    if(recv) {
        if(recv->tgt == node->myid) {
            XmitIDT xmt = recv->xmtid;
            ChannelT cha = recv->cha;
            uint32_t cnt = recv->pac_cnt;
            if(cnt <= 1) {
                node->recv_buf[cha].push_back(recv);
            }
            else {
                auto res = node->order_buf.find(xmt);
                if(res == node->order_buf.end()) res = node->order_buf.emplace(xmt, list<MsgPack*>()).first;
                auto &l = res->second;
                auto iter = l.begin();
                for(; iter != l.end() && (*iter)->pac_idx < recv->pac_idx; iter++) ;
                l.insert(iter, recv);
                if(l.size() == recv->pac_cnt) {
                    node->recv_buf[cha].splice(node->recv_buf[cha].end(), l);
                    node->order_buf.erase(xmt);
                }
            }
        }
        else {
            node->pipeline.emplace_back();
            node->pipeline.back().pack = recv;
            node->pipeline.back().cycle_remained = route_latency;
        }
    }

    {
        auto iter = node->pipeline.begin();
        while(iter != node->pipeline.end()) {
            if(iter->cycle_remained > 1) {
                iter->cycle_remained--;
                continue;
            }
            iter->cycle_remained = 0;
            MsgPack *p = iter->pack;
            BusPortT next = route[node->myid][p->tgt];
            EdgeInChannel *e = node->txs[next];
            ChannelT cha = p->cha;
            if(e->input[cha] == nullptr) {
                e->input[cha] = p;
                iter = node->pipeline.erase(iter);
            }
            else {
                iter++;
            }
        }
    }
}

void SymmetricMultiChannelBus::process_edge(EdgeInChannel *edge) {
    uint32_t total_sz = 0;
    for(int i = 0; i < cha_cnt && total_sz < width; i++) {
        if(edge->input[i] && !edge->output[i]) {
            edge->output[i] = edge->input[i];
            edge->input[i] = nullptr;
            total_sz += edge->output[i]->data.size();
        }
    }
}

}


namespace test {

using simbus::SymmetricMultiChannelBus;
using simbus::BusPortT;
using simbus::BusRouteTable;

bool test_sym_mul_cha_bus() {
    
    const uint32_t nodenum = 16;
    vector<BusPortT> nodes;
    for(uint32_t i = 0; i < nodenum; i++) nodes.push_back(i);

    BusRouteTable routetable;
    simbus::genroute_mesh2d_xy(nodes, 4, 4, true, routetable);

    const uint32_t chanum = 4;
    vector<uint32_t> channel_width;
    channel_width.assign(chanum, 32);

    unordered_map<uint32_t, unordered_map<uint32_t, vector<list<vector<uint8_t>>>>> trans_table;
    for(int i = 0; i < nodenum; i++) {
        auto iter = trans_table.emplace(i, unordered_map<uint32_t, vector<list<vector<uint8_t>>>>()).first;
        for(int j = 0; j < nodenum; j++) {
            iter->second.emplace(j, vector<list<vector<uint8_t>>>());
            iter->second[j].resize(chanum);
        }
    }

    uint64_t send_cnt = 0;
    uint64_t recv_cnt = 0;
    const uint64_t test_cnt = 100000;
    const uint64_t log_interval = 128;

    unique_ptr<SymmetricMultiChannelBus> bus = make_unique<SymmetricMultiChannelBus>(nodes, channel_width, routetable, "testbus");

    uint32_t send_percent = 20;

    printf("Start\n");

    printf("Send:(0/%ld), Recv:(0/%ld)", test_cnt, test_cnt);
    fflush(stdout);
    for(uint64_t tick = 0; 1; tick++) {
        for(int i = 0; i < nodenum; i++) {
            if(send_cnt < test_cnt && RAND(0, 100) < send_percent) {
                uint32_t cha = RAND(0, chanum);
                if(bus->can_send(nodes[i], cha)) {
                    uint32_t sz = ALIGN(RAND(32, 256), 8);
                    uint32_t dst_i = i;
                    while(dst_i == i) dst_i = RAND(0, nodenum);
                    vector<uint8_t> d;
                    d.resize(sz);
                    for(uint32_t n = 0; n < sz; n++) d[n] = RAND(0, 256);
                    *((uint32_t*)(d.data())) = i;
                    trans_table[i][dst_i][cha].emplace_back(d);
                    assert(bus->send(nodes[i], nodes[dst_i], cha, d));
                    send_cnt++;
                }
            }

            for(uint32_t c = 0; c < chanum; c++) {
                if(bus->can_recv(nodes[i], c)) {
                    vector<uint8_t> d;
                    assert(bus->recv(nodes[i], c, d));
                    uint32_t src_i = *((uint32_t*)(d.data()));
                    vector<uint8_t> &ref = trans_table[src_i][i][c].front();
                    assert(ref.size() == d.size());
                    for(int n = 0; n < ref.size(); n++) assert(ref[n] == d[n]);
                    trans_table[src_i][i][c].pop_front();
                    recv_cnt++;
                }
            }
        }

        if(recv_cnt >= test_cnt) break;

        bus->on_current_tick();
        bus->apply_next_tick();

        if(tick % log_interval == 0) {
            printf("\rSend:(%ld/%ld), Recv(%ld/%ld)", send_cnt, test_cnt, recv_cnt, test_cnt);
            fflush(stdout);
        }
    }
    printf("\n");

    printf("Pass!!!\n");
    return true;
}

}

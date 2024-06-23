
#include "routetable.h"


namespace simbus {

void route_insert(SrcPortT src, DstPortT dst, BusPortT next, BusRouteTable &table) {
    auto res1 = table.find(src);
    if(res1 == table.end()) res1 = table.emplace(src, unordered_map<DstPortT, BusPortT>()).first;
    auto res2 = res1->second.find(dst);
    if(res2 == res1->second.end()) res1->second.emplace(dst, next);
    else res2->second = next;
}

void route_delete(SrcPortT src, DstPortT dst, BusRouteTable &table) {
    auto res1 = table.find(src);
    if(res1 != table.end()) {
        auto res2 = res1->second.find(dst);
        if(res2 != res1->second.end()) {
            res1->second.erase(res2);
        }
        if(res1->second.empty()) {
            table.erase(res1);
        }
    }
}

void genroute_single_ring(vector<BusPortT> &nodes, BusRouteTable &out) {
    unordered_map<BusPortT, uint32_t> nodeidxs;
    for(uint32_t i = 0; i < nodes.size(); i++) {
        nodeidxs.emplace(i, nodes[i]);
    }
    if(nodes.size() != nodeidxs.size()) {
        printf("Error: Node ID Collision\n");
        assert(0);
    }

    out.clear();
    uint32_t cnt = nodes.size();
    for(uint32_t i = 0; i < cnt; i++) {
        BusPortT src = nodes[i];
        uint32_t idx = ((i+1) % cnt);
        BusPortT next = nodes[idx];
        for(uint32_t j = 0; j < cnt - 1; j++) {
            route_insert(src, nodes[idx], next, out);
            idx = ((idx+1) % cnt);
        }
    }
}

void genroute_double_ring(vector<BusPortT> &nodes, BusRouteTable &out) {
    unordered_map<BusPortT, uint32_t> nodeidxs;
    for(uint32_t i = 0; i < nodes.size(); i++) {
        nodeidxs.emplace(i, nodes[i]);
    }
    if(nodes.size() != nodeidxs.size()) {
        printf("Error: Node ID Collision\n");
        assert(0);
    }

    out.clear();
    uint32_t cnt = nodes.size();
    for(uint32_t i = 0; i < cnt; i++) {
        BusPortT src = nodes[i];
        uint32_t idx = ((i+1) % cnt);
        BusPortT next = nodes[idx];
        for(uint32_t j = 0; j < (cnt / 2); j++) {
            route_insert(src, nodes[idx], next, out);
            idx = ((idx+1) % cnt);
        }
        if(i == 0) next = nodes[cnt - 1];
        else next = nodes[i - 1];
        while(idx != i) {
            route_insert(src, nodes[idx], next, out);
            idx = ((idx+1) % cnt);
        }
    }
}

void genroute_mesh2d_xy(vector<BusPortT> &nodes_byx, uint32_t cnt_x, uint32_t cnt_y, bool double_direct, BusRouteTable &out) {
    unordered_map<BusPortT, uint32_t> nodeidxs;
    for(uint32_t y = 0; y < cnt_y; y++) {
        for(uint32_t x = 0; x < cnt_x; x++) {
            nodeidxs.emplace((uint32_t)x | (((uint32_t)y) << 16), nodes_byx[x + y * cnt_x]);
        }
    }
    if(nodes_byx.size() != nodeidxs.size()) {
        printf("Error: Mesh Size Check Fail, or Node ID Collision\n");
        assert(0);
    }

    out.clear();
    for(uint32_t y = 0; y < cnt_y; y++) {
        for(uint32_t x = 0; x < cnt_x; x++) {
            BusPortT src = nodes_byx[x + y * cnt_x];
            uint32_t xidx = ((x + 1) % cnt_x);
            BusPortT next = nodes_byx[xidx + y * cnt_x];
            for(uint32_t i = 0; i < (cnt_x / 2); i++) {
                for(uint32_t y2 = 0; y2 < cnt_y; y2++) {
                    route_insert(src, nodes_byx[xidx + y2 * cnt_x], next, out);
                }
                xidx = ((xidx+1) % cnt_x);
            }
            if(double_direct) {
                if(x == 0) next = nodes_byx[(cnt_x - 1) + y * cnt_x];
                else next = nodes_byx[(x - 1) + y * cnt_x];
            }
            while(xidx != x) {
                for(uint32_t y2 = 0; y2 < cnt_y; y2++) {
                    route_insert(src, nodes_byx[xidx + y2 * cnt_x], next, out);
                }
                xidx = ((xidx+1) % cnt_x);
            }
            uint32_t yidx = ((y + 1) % cnt_y);
            next = nodes_byx[x + yidx * cnt_x];
            for(uint32_t j = 0; j < (cnt_y / 2); j++) {
                route_insert(src, nodes_byx[x + yidx * cnt_x], next, out);
                yidx = ((yidx+1) % cnt_y);
            }
            if(double_direct) {
                if(y == 0) next = nodes_byx[x + (cnt_y - 1) * cnt_x];
                else next = nodes_byx[x + (y - 1) * cnt_x];
            }
            while(yidx != y) {
                route_insert(src, nodes_byx[x + yidx * cnt_x], next, out);
                yidx = ((yidx+1) % cnt_y);
            }
        }
    }
}

void assert_route_table_valid(vector<BusPortT> &nodes, BusRouteTable &route) {
    for(auto src : nodes) {
        for(auto dst : nodes) {
            uint32_t max_jmp = nodes.size();
            BusPortT cur = src;
            for(uint32_t i = 0; i < max_jmp && cur != dst; i++) {
                auto res1 = route.find(cur);
                if(res1 == route.end()) break;
                auto res2 = res1->second.find(dst);
                if(res2 == res1->second.end()) break;
                cur = res2->second;
            }
            if(cur != dst) {
                printf("Bus: Route Check Failed: %d -> %d Unreachable", src, dst);
                assert(0);
            }
        }
    }
}

void print_route_table(BusRouteTable &table, std::ostream *out) {
    char *log_buf = new char[1024];
    #define LOGTOFILE(fmt, ...) do{sprintf(log_buf, fmt, ##__VA_ARGS__);(*out) << log_buf;}while(0)

    for(auto &e1 : table) {
        SrcPortT src = e1.first;
        int i = 0;
        for(auto &e2 : e1.second) {
            LOGTOFILE("0x%x->0x%x:0x%x  ", src, e2.first, e2.second);
            if(i == 3) {
                LOGTOFILE("\n");
                i = 0;
            }
            else {
                i++;
            }
        }
        if(i) LOGTOFILE("\n");
        LOGTOFILE("\n");
    }
    #undef LOGTOFILE
    delete[] log_buf;
}



}


namespace test {

using simbus::BusPortT;
using simbus::BusRouteTable;

bool test_bus_route_table() {
    {
        printf("\nTest Single-Ring Route:\n");
        vector<BusPortT> nodes;
        for(int i = 0; i < 4; i++) {
            nodes.push_back(i);
        }
        printf("Nodes: ");
        for(auto n : nodes) printf("0x%x ", n);
        printf("\n\n");
        BusRouteTable table;
        simbus::genroute_single_ring(nodes, table);
        simbus::print_route_table(table, &(std::cout));
        simbus::assert_route_table_valid(nodes, table);
    }
    {
        printf("\nTest Double-Ring Route:\n");
        vector<BusPortT> nodes;
        for(int i = 0; i < 4; i++) {
            nodes.push_back(i);
        }
        printf("Nodes: ");
        for(auto n : nodes) printf("0x%x ", n);
        printf("\n\n");
        BusRouteTable table;
        simbus::genroute_double_ring(nodes, table);
        simbus::print_route_table(table, &(std::cout));
        simbus::assert_route_table_valid(nodes, table);
    }
    {
        printf("\nTest Mesh2D-XY Route:\n");
        vector<BusPortT> nodes;
        for(int i = 0; i < 4; i++) {
            for(int j = 0; j < 4; j++)
                nodes.push_back((i << 16) + j);
        }
        printf("Nodes: ");
        for(auto n : nodes) printf("0x%x ", n);
        printf("\n\n");
        BusRouteTable table;
        simbus::genroute_mesh2d_xy(nodes, 4, 4, true, table);
        simbus::print_route_table(table, &(std::cout));
        simbus::assert_route_table_valid(nodes, table);
    }

    return true;
}

}



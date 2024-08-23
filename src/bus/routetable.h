#ifndef RVSIM_BUS_ROUTE_TABLE_H
#define RVSIM_BUS_ROUTE_TABLE_H

#include "businterface.h"

namespace simbus {

void route_insert(SrcNodeT src, DstNodeT dst, BusNodeT next, BusRouteTable &table);
void route_delete(SrcNodeT src, DstNodeT dst, BusRouteTable &table);

void genroute_single_ring(vector<BusNodeT> &nodes, BusRouteTable &out);

void genroute_double_ring(vector<BusNodeT> &nodes, BusRouteTable &out);

void genroute_mesh2d_xy(vector<BusNodeT> &nodes_byx, uint32_t cnt_x, uint32_t cnt_y, bool double_direct, BusRouteTable &out);

void print_route_table(BusRouteTable &table, std::ostream *out);

void assert_route_table_valid(vector<BusNodeT> &nodes, BusRouteTable &table);

}

namespace test {

bool test_bus_route_table();

}

#endif

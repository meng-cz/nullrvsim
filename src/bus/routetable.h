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

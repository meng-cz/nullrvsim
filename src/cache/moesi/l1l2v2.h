#ifndef RVSIM_CACHE_MOESI_L1L2_V2_H
#define RVSIM_CACHE_MOESI_L1L2_V2_H

#include "cache/cacheinterface.h"
#include "cache/cachecommon.h"

#include "bus/businterface.h"

#include "common.h"
#include "tickqueue.h"

namespace simcache {
namespace moesi {

using simbus::BusInterfaceV2;
using simbus::BusPortT;
using simbus::BusPortMapping;

// typedef struct {
//     uint32_t l1i_set_addr_bit;
//     uint32_t l1i_n_way = 8;
//     uint32_t l1i_mshr_num;
//     uint32_t l1i_query_cycle;
//     uint32_t l1i_query_width;
//     uint32_t l1d_set_addr_bit;
//     uint32_t l1d_n_way = 8;
//     uint32_t l1d_mshr_num;
//     uint32_t l1d_query_cycle;
//     uint32_t l1d_query_width;
//     uint32_t l2_set_addr_bit;
//     uint32_t l2_n_way;
//     uint32_t l2_mshr_num;
//     uint32_t l2_query_cycle;
//     uint32_t l2_query_width;
//     uint32_t l1l2_latency;
// } PrivL1L2Param;

// class PrivL1L2CacheMoesiV2 : public CacheInterfaceV2 {

// public:

//     PrivL1L2CacheMoesiV2(
//         PrivL1L2Param &param,
//         BusInterfaceV2 *bus,
//         BusPortT my_port_id,
//         BusPortMapping *busmap,
//         string logname
//     );

//     // CacheInterface
//     virtual void clear_ld(std::list<CacheOP*> *to_free);
//     virtual void clear_st(std::list<CacheOP*> *to_free);
//     virtual void clear_amo(std::list<CacheOP*> *to_free);
//     virtual void clear_misc(std::list<CacheOP*> *to_free);

//     virtual bool is_empty();

//     // SimObject
//     virtual void on_current_tick();
//     virtual void apply_next_tick();

//     virtual void dump_core(std::ofstream &ofile);

//     bool do_log = false;



// };


}}

#endif

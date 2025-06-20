#ifndef RVSIM_CACHE_SCC_L1_H
#define RVSIM_CACHE_SCC_L1_H

#include "common.h"

#include "cache/cachecommon.h"
#include "cache/cacheinterface.h"

#include "sccllc.h"

namespace simcache {

class SimpleCoherentL1 : public CacheInterface {

public:

    SimpleCoherentL1(CacheParam &param, SCCBundle *port);

    virtual void on_current_tick();
    virtual void apply_next_tick();
    
    virtual void print_statistic(std::ofstream &ofile) {};
    virtual void print_setup_info(std::ofstream &ofile) {};

    virtual void dump_core(std::ofstream &ofile) {};

    virtual SimError load(PhysAddrT paddr, uint32_t len, void *buf, bool noblock);
    virtual SimError store(PhysAddrT paddr, uint32_t len, void *buf, bool noblock);
    virtual SimError load_reserved(PhysAddrT paddr, uint32_t len, void *buf);
    virtual SimError store_conditional(PhysAddrT paddr, uint32_t len, void *buf);
    virtual uint32_t arrival_line(vector<ArrivalLine> *out);

    bool debug = false;
protected:

    CacheParam param;
    SCCBundle *port;

    unique_ptr<GenericLRUCacheBlock<CacheLineT>> cb;
    unordered_set<LineIndexT> writeable;

    SCCReq waiting = SCCReq::none;
    LineIndexT wait_line = 0;
    CacheLineT put_line_data;

    vector<ArrivalLine> arrival;

    bool reserved_address_valid = false;
    PhysAddrT reserved_address = 0;

    char log_buf[256];
};

}

#endif

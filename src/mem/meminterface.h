#ifndef RVSIM_MEMORY_INTERFACE_H
#define RVSIM_MEMORY_INTERFACE_H

#include "common.h"

#include "bus/businterface.h"

namespace simmem {

using simbus::BusInterface;
using simbus::BusPortT;

enum class MemPortResult {
    success = 0,
    busy,
    invalid_addr,
    unaligned_addr,
};

class MemPortInterface {

public:

    virtual Size32T get_data_width() = 0;

    virtual bool addr_valid(PhysAddrT paddr) = 0;

    virtual MemPortResult read(PhysAddrT paddr, uint8_t *buf) = 0;

    virtual MemPortResult write(PhysAddrT paddr, uint8_t *buf) = 0;

};


}


#endif

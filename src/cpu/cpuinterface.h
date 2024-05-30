#ifndef RVSIM_CPU_INTERFACE_H
#define RVSIM_CPU_INTERFACE_H

#include "common.h"
#include "simerror.h"
#include "isa.h"

using isa::RVRegArray;

using isa::RV64AMOOP5;

namespace simcpu {

class CPUInterface : public SimObject {
public:
    virtual void halt() = 0;
    virtual void redirect(VirtAddrT addr, RVRegArray &regs) = 0;
};

class CPUSystemInterface {
public:
    virtual SimError v_to_p(uint32_t cpu_id, VirtAddrT addr, PhysAddrT *out, PageFlagT flg) = 0;

    virtual uint32_t is_dev_mem(uint32_t cpu_id, VirtAddrT addr) = 0;

    virtual bool dev_input(uint32_t cpu_id, VirtAddrT addr, uint32_t len, void *buf) = 0;
    virtual bool dev_output(uint32_t cpu_id, VirtAddrT addr, uint32_t len, void *buf) = 0;
    virtual bool dev_amo(uint32_t cpu_id, VirtAddrT addr, uint32_t len, RV64AMOOP5 amo, void *src, void *dst) {
        LOG(ERROR) << "Not support AMO operation on device memory";
        assert(0);
    }

    virtual VirtAddrT syscall(uint32_t cpu_id, VirtAddrT pc, RVRegArray &iregs) = 0;
    virtual VirtAddrT ebreak(uint32_t cpu_id, VirtAddrT pc, RVRegArray &iregs) = 0;
};

}


#endif

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
    virtual void flush_tlb(VPageIndexT vpn) {};
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
        return false;
    }

    virtual VirtAddrT ecall(uint32_t cpu_id, VirtAddrT pc, RVRegArray &regs) = 0;
    virtual VirtAddrT ebreak(uint32_t cpu_id, VirtAddrT pc, RVRegArray &regs) = 0;
    virtual VirtAddrT exception(uint32_t cpu_id, VirtAddrT pc, SimError expno, uint64_t arg1, uint64_t arg2, RVRegArray &regs) = 0;
};

}


#endif

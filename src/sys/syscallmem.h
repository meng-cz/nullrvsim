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


#ifndef RVSIM_SYS_SYSCALL_MEMORY_H
#define RVSIM_SYS_SYSCALL_MEMORY_H

#include "common.h"
#include "spinlocks.h"

class SyscallMemory {

public:
    SyscallMemory(VirtAddrT base_addr, uint64_t size);
    ~SyscallMemory();

    uint64_t load_syscall_proxy_lib(string path);

    VirtAddrT smem_alloc(uint64_t size);
    void smem_free(VirtAddrT offset);

    VirtAddrT get_syscall_proxy_entry(uint64_t index) {
        if(index < max_syscall_index) return syscall_proxy_entrys[index];
        else return 0;
    };

    inline uint8_t *get_host_addr(VirtAddrT addr) {
        if(addr >= base && addr < base + sz) return mem + (addr - base);
        else return nullptr;
    };

    inline VirtAddrT get_futex_lock_vaddr() {
        return base + offset_futex_spinlock;
    }

    void debug_print_free_segs();

private:
    char log_buf[256];

    const uint64_t offset_futex_spinlock = 0;

    uint8_t *mem = nullptr;
    VirtAddrT base = 0;
    uint64_t sz = 0;

    SpinLock alloc_lock;
    std::list<std::pair<VirtAddrT, uint64_t>> free_segs;
    std::unordered_map<VirtAddrT, uint64_t> alloced_segs;

    const uint64_t max_syscall_index = 512;
    VirtAddrT *syscall_proxy_entrys = nullptr;

};


namespace test {

bool test_syscall_memory();

};

#endif

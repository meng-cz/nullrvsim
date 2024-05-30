
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

#ifndef RVSIM_SYS_MULTICORE_H
#define RVSIM_SYS_MULTICORE_H

#include "common.h"
#include "simroot.h"
#include "spinlocks.h"

#include "cpu/cpuinterface.h"

#include "cache/dmainterface.h"

#include "rvthread.h"
#include "syscallmem.h"
#include "pagemmap.h"

using simcpu::CPUInterface;
using simcpu::CPUSystemInterface;

using simcache::DMACallBackHandler;
using simcache::DMARequest;
using simcache::DMAReqType;
using simcache::SimDMADevice;

using isa::RVRegArray;

class SimSystemMultiCore : public CPUSystemInterface, public DMACallBackHandler {
public:

    SimSystemMultiCore() {};

    void init(SimWorkload &workload, std::vector<CPUInterface*> &cpus, PhysPageAllocator *ppman, SimDMADevice *dma);

    virtual SimError v_to_p(uint32_t cpu_id, VirtAddrT addr, PhysAddrT *out, PageFlagT flg);
    virtual uint32_t is_dev_mem(uint32_t cpu_id, VirtAddrT addr);
    virtual bool dev_input(uint32_t cpu_id, VirtAddrT addr, uint32_t len, void *buf);
    virtual bool dev_output(uint32_t cpu_id, VirtAddrT addr, uint32_t len, void *buf);
    virtual bool dev_amo(uint32_t cpu_id, VirtAddrT addr, uint32_t len, RV64AMOOP5 amo, void *src, void *dst);
    virtual VirtAddrT syscall(uint32_t cpu_id, VirtAddrT addr, RVRegArray &iregs);
    virtual VirtAddrT ebreak(uint32_t cpu_id, VirtAddrT addr, RVRegArray &iregs);

    virtual void dma_complete_callback(uint64_t callbackid);

protected:
    bool has_init = false;

    SpinLock sch_lock;

    typedef struct {
        CPUInterface *      cpu = nullptr;
        RVThread *          exec_thread = nullptr;
    } CPUDevice;

    uint32_t cpu_num = 0;
    std::vector<CPUDevice> cpu_devs;

    typedef struct {
        RVThread *      thread = nullptr;
        uint32_t        futex_mask = 0;
        uint32_t        last_cpu_id = 0;
    } FutexWaitThread;

    std::unordered_map<PhysAddrT, std::list<FutexWaitThread>> futex_wait_threads;
    inline void futex_wait_thread_insert(PhysAddrT paddr, RVThread * thread, uint32_t futex_mask, uint32_t cpu_id) {
        auto res = futex_wait_threads.find(paddr);
        if(res == futex_wait_threads.end()) {
            futex_wait_threads.emplace(paddr, std::list<FutexWaitThread>());
            res = futex_wait_threads.find(paddr);
        }
        res->second.emplace_back(FutexWaitThread {
            .thread = thread, .futex_mask = futex_mask, .last_cpu_id = cpu_id
        });
    }
    inline bool futex_wait_thread_pop(PhysAddrT paddr, uint32_t futex_mask, FutexWaitThread* out) {
        auto res = futex_wait_threads.find(paddr);
        if(res != futex_wait_threads.end() && !res->second.empty()) {
            auto iter = res->second.begin();
            if(futex_mask != 0) {
                for(; iter != res->second.end(); iter++) {
                    if(iter->futex_mask == futex_mask) break;
                }
            }
            if(iter != res->second.end()) {
                if(out) *out = *iter;
                res->second.erase(iter);
                if(res->second.empty()) futex_wait_threads.erase(res);
                return true;
            }
        }
        return false;
    }

    SimDMADevice *dma = nullptr;
    typedef struct {
        RVThread *      thread = nullptr;
        uint32_t        ref_cnt = 0;
        uint32_t        last_cpu_id = 0;
        std::list<uint8_t*>                        to_free;
        std::list<std::pair<uint8_t*, uint64_t>>   to_unmap;
    } DMAWaitThread;
    std::unordered_map<RVThread *, DMAWaitThread> dma_wait_threads;
    inline void fill_dma_iommu(DMARequest *req, RVThread *thread) {
        for(VPageIndexT vpi = (req->vaddr >> PAGE_ADDR_OFFSET); vpi < CEIL_DIV(req->vaddr + req->size, PAGE_LEN_BYTE); vpi++) {
            PhysAddrT paddr = 0;
            assert(thread->va2pa(vpi << PAGE_ADDR_OFFSET, &paddr, 0) == SimError::success);
            PageIndexT ppi = (paddr >> PAGE_ADDR_OFFSET);
            req->vp2pp->emplace(vpi, ppi);
            req->pp2vp->emplace(ppi, vpi);
        }
    }


    std::list<RVThread*> ready_threads;
    std::set<RVThread*> waiting_threads;

    /**
     * 核心上正在运行的进程需要换出
     * @return 核心是否有后续需要运行的进程,如果返回1则cpu_devs[cpu_id].exec_thread一定有效
    */
    bool switch_next_thread_nolock(uint32_t cpuid, uint32_t flag);
    const uint32_t SWFLAG_EXIT      = (1 << 0); // 该进程由于调用了Exit系统调用而被换出
    const uint32_t SWFLAG_YIELD     = (1 << 1); // 该进程由于调用了Yield系统调用而被换出,可被立即换回
    const uint32_t SWFLAG_WAIT      = (1 << 2); // 该进程由于等待被换出,不可被立即换回
    void insert_ready_thread_nolock(RVThread *thread, uint32_t prefered_cpu);

    uint64_t alloc_current_tid = 0;
    inline uint64_t alloc_tid() {
        return alloc_current_tid++;
    }

    const uint64_t syscall_mem_vaddr_start = MAX_VADDR;
    const uint64_t syscall_mem_length = 0x40000000UL;
    SyscallMemory *syscall_memory = nullptr;
    SpinLock syscall_memory_amo_lock; //同时只能有一个对系统内存的amo操作

    char log_buf[256];
    bool log_info = false;
    bool log_syscall = false;

    std::vector<std::array<char, 256>> log_bufs;

#define MP_SYSCALL_FUNC_NAME(num, name) syscall_##num##_##name
#define MP_SYSCALL_CLAIM(num, name) VirtAddrT MP_SYSCALL_FUNC_NAME(num,name)(uint32_t cpu_id, VirtAddrT pc, RVRegArray &iregs)
#define MP_SYSCALL_DEFINE(num, name) VirtAddrT SimSystemMultiCore::MP_SYSCALL_FUNC_NAME(num,name)(uint32_t cpu_id, VirtAddrT pc, RVRegArray &iregs)

    MP_SYSCALL_CLAIM(57, close);
    MP_SYSCALL_CLAIM(62, lseek);
    MP_SYSCALL_CLAIM(94, exitgroup);
    MP_SYSCALL_CLAIM(96, set_tid_address);
    MP_SYSCALL_CLAIM(99, set_robust_list);
    MP_SYSCALL_CLAIM(124, sched_yield);
    MP_SYSCALL_CLAIM(172, getpid);
    MP_SYSCALL_CLAIM(173, getppid);
    MP_SYSCALL_CLAIM(174, getuid);
    MP_SYSCALL_CLAIM(175, geteuid);
    MP_SYSCALL_CLAIM(176, getgid);
    MP_SYSCALL_CLAIM(177, getegid);
    MP_SYSCALL_CLAIM(178, gettid);
    MP_SYSCALL_CLAIM(198, socket);
    MP_SYSCALL_CLAIM(214, brk);
    MP_SYSCALL_CLAIM(215, munmap);
    MP_SYSCALL_CLAIM(222, mmap);
    MP_SYSCALL_CLAIM(226, mprotect);
    MP_SYSCALL_CLAIM(233, madvise);
    MP_SYSCALL_CLAIM(261, prlimit);
    MP_SYSCALL_CLAIM(901, host_alloc);
    MP_SYSCALL_CLAIM(902, host_free);
    MP_SYSCALL_CLAIM(904, host_get_tid_address);
    MP_SYSCALL_CLAIM(910, host_ioctl_siocgifconf);
    MP_SYSCALL_CLAIM(1017, host_getcwd);
    MP_SYSCALL_CLAIM(1029, host_ioctl);
    MP_SYSCALL_CLAIM(1048, host_faccessat);
    MP_SYSCALL_CLAIM(1056, host_openat);
    MP_SYSCALL_CLAIM(1063, host_read);
    MP_SYSCALL_CLAIM(1064, host_write);
    MP_SYSCALL_CLAIM(1078, host_readlinkat);
    MP_SYSCALL_CLAIM(1079, host_newfstatat);
    MP_SYSCALL_CLAIM(1080, host_fstat);
    MP_SYSCALL_CLAIM(1093, host_exit);
    MP_SYSCALL_CLAIM(1098, host_futex);
    MP_SYSCALL_CLAIM(1113, host_clock_gettime);
    MP_SYSCALL_CLAIM(1134, host_sigaction);
    MP_SYSCALL_CLAIM(1135, host_sigprocmask);
    MP_SYSCALL_CLAIM(1220, host_clone);
    MP_SYSCALL_CLAIM(1261, host_prlimit);
    MP_SYSCALL_CLAIM(1278, host_getrandom);

};


#endif

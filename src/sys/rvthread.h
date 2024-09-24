#ifndef RVSIM_SYS_RVTHREAD_H
#define RVSIM_SYS_RVTHREAD_H

#include "common.h"
#include "simerror.h"

#include "pagemmap.h"

// For Reg-Cnt
#include "cpu/isa.h"

#include "cache/dmainterface.h"

#include <signal.h>
#include <sys/resource.h>

using isa::RVRegArray;

#define DEFAULT_PID (10000)

enum class RVThreadState {
    READY = 0,
    EXEC,
    WAIT,
};

typedef struct {
    uint64_t    clone_flags;
    VirtAddrT   newsp;
    VirtAddrT   parent_tidptr;
    VirtAddrT   tls;
    VirtAddrT   child_tidptr;
} CloneArgs;

#define CLONEFLG_DEFAULT_THREAD  (0x3d0f00)

typedef struct {
    // string      elffile;
    string      name;
    int64_t     addend;
    uint32_t    info;
    uint32_t    othre;
    VirtAddrT   addr;
    VirtAddrT   got;
} DynamicSymbol;

typedef struct {
    uint32_t        alloc_fd = 3;
    std::unordered_map<int32_t, int32_t> tb;
} FDTable;

typedef struct {
    VPageIndexT             vpi;
    VPageIndexT             vpcnt;
    PageFlagT               pgflg;
    std::vector<uint8_t>    data;
} MemPagesToLoad;

typedef std::array<uint8_t, 8> RVSigset;

typedef struct {
    __sighandler_t k_sa_handler;
    unsigned long sa_flags;
    RVSigset sa_mask;
} RVKernelSigaction;

class RVThread {

public:
    RVThread(const RVThread& t) = delete;
    RVThread& operator=(const RVThread& t) = delete;

    RVThread(SimWorkload workload, PhysPageAllocator *pmman, VirtAddrT *out_entry, VirtAddrT *out_sp);
    RVThread(RVThread *parent_thread, uint64_t newtid, uint64_t fork_flag);

    VirtAddrT elf_load_dyn_lib(string elfpath, std::list<MemPagesToLoad> *output, VirtAddrT *out_entry);
    void elf_exec(SimWorkload &param, std::list<MemPagesToLoad> *out_vpgs, VirtAddrT *out_entry, VirtAddrT *out_sp);

    uint64_t pid;
    uint64_t tid;

    RVThreadState state = RVThreadState::READY;

    std::list<RVThread*> childs;
    RVThread *parent = nullptr;
    std::shared_ptr<std::list<RVThread*>> threadgroup;

    std::shared_ptr<SpinRWLock> shared_lock;
    std::shared_ptr<ThreadPageTable> pgtable;
    std::shared_ptr<std::unordered_map<int32_t, RVKernelSigaction>> sig_actions;
    std::shared_ptr<RVSigset> sig_proc_mask;
    std::shared_ptr<FDTable> fdtable;
    PhysPageAllocator *pmman;

    int32_t fdtable_trans(int32_t user_fd);
    int32_t fdtable_insert(int32_t sys_fd);
    int32_t fdtable_pop(int32_t user_fd);

    VirtAddrT sys_brk(VirtAddrT newbrk);
    VirtAddrT sys_mmap(uint64_t length, uint64_t pgflag, int32_t fd, uint64_t offset, string info);
    VirtAddrT sys_mmap_fixed(VirtAddrT addr, uint64_t length, uint64_t pgflag, int32_t fd, uint64_t offset, string info);
    void sys_munmap(VirtAddrT vaddr, uint64_t length);
    void sys_mprotect(VirtAddrT vaddr, uint64_t length, PageFlagT flag);
    void sys_sigaction(int32_t signum, RVKernelSigaction *act, RVKernelSigaction *oldact);
    void sys_sigprocmask(uint32_t how, uint8_t *set, uint8_t *oldset, uint64_t sigsetsize);

    VPageIndexT cow_realloc_missed_page(VPageIndexT missed);
    void cow_free_tmp_page(VPageIndexT tmppage);

    inline SimError va2pa(VirtAddrT addr, PhysAddrT *out, PageFlagT flg) {
        PageIndexT _ppi = 0;
        PageFlagT _flg = 0;
        bool hit = true;
        shared_lock->read_lock();
        auto res = pgtable->pgtable.find(addr >> PAGE_ADDR_OFFSET);
        if(res == pgtable->pgtable.end()) [[unlikely]] hit = false;
        else {
            _ppi = res->second.ppi;
            _flg = res->second.flg;
        }
        shared_lock->read_unlock();
        if(!hit) [[unlikely]] return SimError::invalidaddr;
        if((_flg & flg) != flg) [[unlikely]] {
            if((_flg & PGFLAG_COW) && (flg & PGFLAG_W)) {
                bool owned = false;
                shared_lock->write_lock();
                if(!pgtable->ppman->is_shared(_ppi)) {
                    res = pgtable->pgtable.find(addr >> PAGE_ADDR_OFFSET);
                    if(res != pgtable->pgtable.end()) [[likely]] {
                        res->second.flg &= (~PGFLAG_COW);
                        res->second.flg |= (PGFLAG_W);
                        owned = true;
                    }
                }
                shared_lock->write_unlock();
                if(!owned) return SimError::pagefault;
            }
            else {
                return SimError::unaccessable;
            }
        }
        *out = (_ppi << PAGE_ADDR_OFFSET) + (addr & (PAGE_LEN_BYTE - 1));
        return SimError::success;
    }

    typedef struct {
        RVRegArray  regs;
        bool        recover_a0 = false;
    } RVContext;

    list<RVContext> context_stack;
    void save_context_stack(VirtAddrT nextpc, RVRegArray &regs, bool recover_a0);
    VirtAddrT recover_context_stack(RVRegArray &out_regs);

    // bool ecall_with_ret = false;
    // uint64_t context_host_ecall[RV_REG_CNT_INT];
    // uint64_t context_switch[RV_REG_CNT_INT + RV_REG_CNT_FP];

    // void save_context_host_ecall(VirtAddrT nextpc, RVRegArray &iregs, bool request_ret = true);
    // VirtAddrT recover_context_host_ecall(RVRegArray &out_iregs);
    // void save_context_switch(VirtAddrT nextpc, RVRegArray &regs);
    // VirtAddrT recover_context_switch(RVRegArray &out_regs);

    uint64_t set_child_tid = 0UL;
    uint64_t clear_child_tid = 0UL;

    bool do_child_cleartid = false;

    uint64_t robust_list_head = 0UL;
    uint64_t robust_list_len = 0UL;

    rlimit rlimit_values[RLIM_NLIMITS];
    void sys_prlimit(uint64_t resource, rlimit* p_new, rlimit* p_old);

};


#endif


#include "multicore.h"

#include "simroot.h"
#include "configuration.h"

#include <stdarg.h>
#include <fcntl.h>

#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>

#include <linux/futex.h>

using isa::ireg_value_of;

void SimSystemMultiCore::init(SimWorkload &workload, std::vector<CPUInterface*> &cpus, PhysPageAllocator *ppman, SimDMADevice *dma) {
    cpu_num = cpus.size();
    assert(cpu_num == conf::get_int("multicore", "cpu_number", cpu_num));
    for(int i = 0; i < cpu_num; i++) {
        cpu_devs.emplace_back(CPUDevice {
            .cpu = cpus[i],
            .exec_thread = nullptr
        });
    }

    this->dma = dma;

    syscall_memory = new SyscallMemory(syscall_mem_vaddr_start, syscall_mem_length);
    if(syscall_memory->load_syscall_proxy_lib(workload.syscall_proxy_lib_path) == 0) {
        sprintf(log_buf, "Failed to load syscall proxy library %s", workload.syscall_proxy_lib_path.c_str());
        LOG(ERROR) << log_buf;
        exit(0);
    }
    sprintf(log_buf, "Load risc-v syscall proxy lib: %s", workload.syscall_proxy_lib_path.c_str());
    simroot::print_log_info(log_buf);

    log_info = conf::get_int("sys", "log_info_to_stdout", 0);
    log_syscall = conf::get_int("sys", "log_ecall_to_stdout", 0);
    sch_lock.wait_interval = conf::get_int("sys", "sch_lock_wait_interval", 64);
    syscall_memory_amo_lock.wait_interval = 32;
    log_bufs.assign(cpu_num, std::array<char, 256>());

    uint64_t entry = 0, sp = 0;
    RVThread *init_thread = new RVThread(workload, ppman, &entry, &sp);
    RVRegArray regs;
    isa::zero_regs(regs);
    regs[0] = entry;
    regs[isa::ireg_index_of("sp")] = sp;
    init_thread->save_context_switch(entry, regs);
    insert_ready_thread_nolock(init_thread, 0);

    alloc_current_tid = init_thread->tid + 1;
}



bool SimSystemMultiCore::switch_next_thread_nolock(uint32_t cpuid, uint32_t flag) {
    bool ret = false;
    if(flag == SWFLAG_EXIT) {
        for(auto t : cpu_devs[cpuid].exec_thread->childs) {
            t->parent = nullptr;
        }
        delete cpu_devs[cpuid].exec_thread;
        cpu_devs[cpuid].exec_thread = nullptr;
        bool no_running_thread = (waiting_threads.empty());
        if(!ready_threads.empty()) {
            cpu_devs[cpuid].exec_thread = ready_threads.front();
            ready_threads.pop_front();
            no_running_thread = false;
        }
        else {
            for(auto &entry : cpu_devs) {
                if(entry.exec_thread) {
                    no_running_thread = false;
                }
            }
        }
        ret = (cpu_devs[cpuid].exec_thread != nullptr);
        if(no_running_thread) {
            simroot::print_log_info("No Running Thread, Simulator Exited");
            simroot::stop_sim_and_exit();
        }
    }
    else if(flag == SWFLAG_WAIT) {
        waiting_threads.insert(cpu_devs[cpuid].exec_thread);
        cpu_devs[cpuid].exec_thread = nullptr;
        if(!ready_threads.empty()) {
            cpu_devs[cpuid].exec_thread = ready_threads.front();
            ready_threads.pop_front();
        }
        ret = (cpu_devs[cpuid].exec_thread != nullptr);
    }
    else if(flag == SWFLAG_YIELD) {
        ready_threads.push_back(cpu_devs[cpuid].exec_thread);
        cpu_devs[cpuid].exec_thread = ready_threads.front();
        ready_threads.pop_front();
        return true;
    }
    else {
        LOG(ERROR) << "Unsupported switch flag " << flag;
        assert(0);
    }
    return ret;
}

void SimSystemMultiCore::insert_ready_thread_nolock(RVThread *thread, uint32_t prefered_cpu){
    uint32_t cpuid = cpu_num;
    if(cpu_devs[prefered_cpu].exec_thread == nullptr) {
        cpuid = prefered_cpu;
    }
    else {
        for(uint32_t i = 0; i < cpu_num; i++) {
            if(cpu_devs[i].exec_thread == nullptr) {
                cpuid = i;
                break;
            }
        }
    }
    if(cpuid < cpu_num) {
        waiting_threads.erase(thread);
        cpu_devs[cpuid].exec_thread = thread;
        RVRegArray regs;
        VirtAddrT nextpc = thread->recover_context_switch(regs);
        cpu_devs[cpuid].cpu->redirect(nextpc, regs);
    }
    else {
        ready_threads.push_back(thread);
    }
}

SimError SimSystemMultiCore::v_to_p(uint32_t cpu_id, VirtAddrT addr, PhysAddrT *out, PageFlagT flg) {
    assert(cpu_devs[cpu_id].exec_thread);
    return cpu_devs[cpu_id].exec_thread->va2pa(addr, out, flg);
}

uint32_t SimSystemMultiCore::is_dev_mem(uint32_t cpu_id, VirtAddrT addr) {
    return (((addr >= syscall_mem_vaddr_start) && (addr < syscall_mem_vaddr_start + syscall_mem_length)))?1:0;
}

bool SimSystemMultiCore::dev_input(uint32_t cpu_id, VirtAddrT addr, uint32_t len, void *buf) {
    if(!is_dev_mem(cpu_id, addr)) {
        return false;
    }
    uint8_t *ptr = syscall_memory->get_host_addr(addr);
    memcpy(buf, syscall_memory->get_host_addr(addr), len);
    return true;
}

bool SimSystemMultiCore::dev_output(uint32_t cpu_id, VirtAddrT addr, uint32_t len, void *buf) {
    if(!is_dev_mem(cpu_id, addr)) {
        return false;
    }
    uint8_t *ptr = syscall_memory->get_host_addr(addr);
    memcpy(syscall_memory->get_host_addr(addr), buf, len);
    return true;
}

bool SimSystemMultiCore::dev_amo(uint32_t cpu_id, VirtAddrT addr, uint32_t len, RV64AMOOP5 amo, void *src, void *dst) {
    if(!is_dev_mem(cpu_id, addr)) {
        return false;
    }
    if(len == 8) {
        int64_t *pmem = (int64_t *)(syscall_memory->get_host_addr(addr));
        int64_t *psrc = (int64_t *)src, *pdst = (int64_t *)dst;
        *pdst = *pmem;
        switch (amo)
        {
        case RV64AMOOP5::ADD : *pmem = (*pmem) + (*psrc); break;
        case RV64AMOOP5::AND : *pmem = (*pmem) & (*psrc); break;
        case RV64AMOOP5::OR  : *pmem = (*pmem) | (*psrc); break;
        case RV64AMOOP5::MAX : *pmem = std::max((int64_t)(*pmem), (int64_t)(*psrc)); break;
        case RV64AMOOP5::MIN : *pmem = std::min((int64_t)(*pmem), (int64_t)(*psrc)); break;
        case RV64AMOOP5::MAXU: *pmem = std::max((uint64_t)(*pmem), (uint64_t)(*psrc)); break;
        case RV64AMOOP5::MINU: *pmem = std::min((uint64_t)(*pmem), (uint64_t)(*psrc)); break;
        default: assert(0);
        }
    }
    else if(len == 4) {
        int32_t *pmem = (int32_t *)(syscall_memory->get_host_addr(addr));
        int32_t *psrc = (int32_t *)src, *pdst = (int32_t *)dst;
        *pdst = *pmem;
        switch (amo)
        {
        case RV64AMOOP5::ADD : *pmem = (*pmem) + (*psrc); break;
        case RV64AMOOP5::AND : *pmem = (*pmem) & (*psrc); break;
        case RV64AMOOP5::OR  : *pmem = (*pmem) | (*psrc); break;
        case RV64AMOOP5::MAX : *pmem = std::max((int32_t)(*pmem), (int32_t)(*psrc)); break;
        case RV64AMOOP5::MIN : *pmem = std::min((int32_t)(*pmem), (int32_t)(*psrc)); break;
        case RV64AMOOP5::MAXU: *pmem = std::max((uint32_t)(*pmem), (uint32_t)(*psrc)); break;
        case RV64AMOOP5::MINU: *pmem = std::min((uint32_t)(*pmem), (uint32_t)(*psrc)); break;
        default: assert(0);
        }
    }
    else {
        assert(0);
    }
    return true;
}

VirtAddrT SimSystemMultiCore::syscall(uint32_t cpu_id, VirtAddrT addr, RVRegArray &regs) {
    uint64_t syscallid = regs[17];
    if(log_info) {
        sprintf(log_buf, "CPU%d Raise an ECALL: %ld", cpu_id, syscallid);
        simroot::print_log_info(log_buf);
    }
    assert(cpu_devs[cpu_id].exec_thread);
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    CPUInterface *cpu = cpu_devs[cpu_id].cpu;

    VirtAddrT ret = 0UL;

#define MP_SYSCALL_CASE(num, name) case num: ret = MP_SYSCALL_FUNC_NAME(num, name)(cpu_id, addr, regs); break;

    switch (syscallid)
    {
    MP_SYSCALL_CASE(57, close);
    MP_SYSCALL_CASE(62, lseek);
    MP_SYSCALL_CASE(94, exitgroup);
    MP_SYSCALL_CASE(96, set_tid_address);
    MP_SYSCALL_CASE(99, set_robust_list);
    MP_SYSCALL_CASE(124, sched_yield);
    MP_SYSCALL_CASE(172, getpid);
    MP_SYSCALL_CASE(173, getppid);
    MP_SYSCALL_CASE(174, getuid);
    MP_SYSCALL_CASE(175, geteuid);
    MP_SYSCALL_CASE(176, getgid);
    MP_SYSCALL_CASE(177, getegid);
    MP_SYSCALL_CASE(178, gettid);
    MP_SYSCALL_CASE(198, socket);
    MP_SYSCALL_CASE(214, brk);
    MP_SYSCALL_CASE(215, munmap);
    MP_SYSCALL_CASE(222, mmap);
    MP_SYSCALL_CASE(226, mprotect);
    MP_SYSCALL_CASE(233, madvise);
    MP_SYSCALL_CASE(261, prlimit);

    MP_SYSCALL_CASE(901, host_alloc);
    MP_SYSCALL_CASE(902, host_free);
    MP_SYSCALL_CASE(904, host_get_tid_address);
    MP_SYSCALL_CASE(910, host_ioctl_siocgifconf);
    MP_SYSCALL_CASE(1017, host_getcwd);
    MP_SYSCALL_CASE(1029, host_ioctl);
    MP_SYSCALL_CASE(1048, host_faccessat);
    MP_SYSCALL_CASE(1056, host_openat);
    MP_SYSCALL_CASE(1063, host_read);
    MP_SYSCALL_CASE(1064, host_write);
    MP_SYSCALL_CASE(1078, host_readlinkat);
    MP_SYSCALL_CASE(1079, host_newfstatat);
    MP_SYSCALL_CASE(1080, host_fstat);
    MP_SYSCALL_CASE(1093, host_exit);
    MP_SYSCALL_CASE(1098, host_futex);
    MP_SYSCALL_CASE(1113, host_clock_gettime);
    MP_SYSCALL_CASE(1134, host_sigaction);
    MP_SYSCALL_CASE(1135, host_sigprocmask);
    MP_SYSCALL_CASE(1220, host_clone);
    MP_SYSCALL_CASE(1261, host_prlimit);
    MP_SYSCALL_CASE(1278, host_getrandom);
    default:
        ret = syscall_memory->get_syscall_proxy_entry(syscallid);
        if(ret != 0) {
            // if(log_syscall) {
            //     sprintf(log_bufs[cpu_id].data(), "CPU%d ECALL->Host_Proxy_%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx)",
            //     cpu_id, syscallid,
            //     regs[isa::ireg_index_of("a0")],
            //     regs[isa::ireg_index_of("a1")],
            //     regs[isa::ireg_index_of("a2")],
            //     regs[isa::ireg_index_of("a3")]
            //     );
            //     simroot::print_log_info(log_bufs[cpu_id].data());
            // }
            thread->save_context_host_ecall(addr + 4, regs);
        }
        else {
            sprintf(log_bufs[cpu_id].data(), "CPU%d Raise an Unkonwn ECALL %ld @0x%lx, arg:0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx",
                cpu_id, syscallid, addr,
                regs[isa::ireg_index_of("a0")],
                regs[isa::ireg_index_of("a1")],
                regs[isa::ireg_index_of("a2")],
                regs[isa::ireg_index_of("a3")],
                regs[isa::ireg_index_of("a4")],
                regs[isa::ireg_index_of("a5")]
            );
            LOG(ERROR) << log_bufs[cpu_id].data();
            exit(0);
            return 0UL;
        }
    }

#undef MP_SYSCALL_CASE

    return ret;
}

VirtAddrT SimSystemMultiCore::ebreak(uint32_t cpu_id, VirtAddrT addr, RVRegArray &iregs) {
    if(log_info) {
        sprintf(log_buf, "CPU%d Raise an EBREAK @0x%lx", cpu_id, addr);
        simroot::print_log_info(log_buf);
    }
    assert(cpu_devs[cpu_id].exec_thread);
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    CPUInterface *cpu = cpu_devs[cpu_id].cpu;
    if(is_dev_mem(cpu_id, addr)) {
        // This is an ecall ret
        return thread->recover_context_host_ecall(iregs);
    }
    else {
        sprintf(log_buf, "CPU%d Raise an EBREAK @0x%lx", cpu_id, addr);
        simroot::print_log_info(log_buf);
        cpu->halt();
        simroot::stop_sim_and_exit();
        return 0UL;
    }
}

void SimSystemMultiCore::dma_complete_callback(uint64_t callbackid) {
    sch_lock.lock();
    auto res = dma_wait_threads.find((RVThread*)callbackid);
    if(res != dma_wait_threads.end()) {
        if(res->second.ref_cnt <= 1) {
            for(auto p : res->second.to_free) {
                delete[] p;
            }
            for(auto &entry : res->second.to_unmap) {
                munmap(entry.first, entry.second);
            }
            insert_ready_thread_nolock(res->second.thread, res->second.last_cpu_id);
            dma_wait_threads.erase(res);
        }
        else {
            res->second.ref_cnt -= 1;
        }
    }
    sch_lock.unlock();
}

// -----------------------------------------------------------------
//   Syscall Handlers
// -----------------------------------------------------------------

#define CURT (cpu_devs[cpu_id].exec_thread)
#define IREG_V(rname) (iregs[isa::ireg_index_of(#rname)])
#define HOST_ADDR_OF_IREG(rname) (syscall_memory->get_host_addr(IREG_V(rname)))
#define CPULOG(fmt, ...) if(log_syscall) { sprintf(log_bufs[cpu_id].data(), fmt, ##__VA_ARGS__ ); simroot::print_log_info(log_bufs[cpu_id].data());}
#define CPUFLOG(fmt, ...) sprintf(log_bufs[cpu_id].data(), fmt, ##__VA_ARGS__ ); simroot::print_log_info(log_bufs[cpu_id].data());
#define CPUERROR(fmt, ...) sprintf(log_bufs[cpu_id].data(), fmt, ##__VA_ARGS__ ); LOG(ERROR)<<(log_bufs[cpu_id].data());

#define LOG_SYSCALL_1(name, f0, a0, rf0, r) if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ") -> " rf0 "", cpu_id, a0, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}

#define LOG_SYSCALL_2(name, f0, a0, f1, a1, rf0, r) if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ") -> " rf0 "", cpu_id, a0, a1, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}

#define LOG_SYSCALL_3(name, f0, a0, f1, a1, f2, a2, rf0, r) if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ", " f2 ") -> " rf0 "", cpu_id, a0, a1, a2, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}

#define LOG_SYSCALL_4(name, f0, a0, f1, a1, f2, a2, f3, a3, rf0, r) if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ", " f2 ", " f3 ") -> " rf0 "", cpu_id, a0, a1, a2, a3, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}

#define LOG_SYSCALL_5(name, f0, a0, f1, a1, f2, a2, f3, a3, f4, a4, rf0, r) if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ", " f2 ", " f3 ", " f4 ") -> " rf0 "", cpu_id, a0, a1, a2, a3, a4, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}

#define LOG_SYSCALL_6(name, f0, a0, f1, a1, f2, a2, f3, a3, f4, a4, f5, a5, rf0, r) if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ", " f2 ", " f3 ", " f4 ", " f5 ") -> " rf0 "", cpu_id, a0, a1, a2, a3, a4, a5, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}


MP_SYSCALL_DEFINE(57, close) {
    uint64_t ret = close(CURT->fdtable_pop(IREG_V(a0)));
    LOG_SYSCALL_1("close", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(62, lseek) {
    uint64_t ret = lseek(CURT->fdtable_trans(IREG_V(a0)), IREG_V(a1), IREG_V(a2));
    LOG_SYSCALL_3("lseek", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(94, exitgroup) {
    CPUFLOG("CPU%d Raise an ECALL: EXITGROUP", cpu_id);
    simroot::stop_sim_and_exit();
    return 0UL;
    // if(switch_next_thread_lock(cpu_id, SWFLAG_EXIT)) {
    //     assert(cpu_devs[cpu_id].exec_thread);
    //     RVRegArray regs;
    //     VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_switch(regs);
    //     cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
    //     return nextpc;
    // }
    // else {
    //     cpu_devs[cpu_id].cpu->halt();
    //     return 0UL;
    // }
}

MP_SYSCALL_DEFINE(96, set_tid_address) {
    CURT->clear_child_tid = IREG_V(a0);
    LOG_SYSCALL_1("set_tid_address", "0x%lx", IREG_V(a0), "%ld", CURT->tid);
    IREG_V(a0) = CURT->tid;
    return pc + 4;
}

MP_SYSCALL_DEFINE(99, set_robust_list) {
    LOG_SYSCALL_2("set_robust_list", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "%ld", -1L);
    IREG_V(a0) = -1;
    return pc + 4;
}

MP_SYSCALL_DEFINE(124, sched_yield) {
    LOG_SYSCALL_1("sched_yield", "%ld", IREG_V(a0), "%ld", 0UL);
    IREG_V(a0) = 0;
    CURT->save_context_switch(pc + 4, iregs);
    sch_lock.lock();
    assert(switch_next_thread_nolock(cpu_id, SWFLAG_YIELD));
    sch_lock.unlock();
    RVRegArray regs;
    VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_switch(regs);
    cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
    return nextpc;
}

MP_SYSCALL_DEFINE(172, getpid) {
    int64_t ret = CURT->pid;
    LOG_SYSCALL_1("getpid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(173, getppid) {
    int64_t ret = ((CURT->parent)?(CURT->parent->pid):getpid());
    LOG_SYSCALL_1("getppid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(174, getuid) {
    int64_t ret = getuid();
    LOG_SYSCALL_1("getuid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(175, geteuid) {
    int64_t ret = geteuid();
    LOG_SYSCALL_1("geteuid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(176, getgid) {
    int64_t ret = getgid();
    LOG_SYSCALL_1("getgid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(177, getegid) {
    int64_t ret = getegid();
    LOG_SYSCALL_1("getegid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(178, gettid) {
    int64_t ret = CURT->tid;
    LOG_SYSCALL_1("gettid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(198, socket) {
    uint64_t ret = socket(IREG_V(a0), IREG_V(a1), IREG_V(a2));
    ret = CURT->fdtable_insert(ret);
    LOG_SYSCALL_3("socket", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(214, brk) {
    uint64_t arg0 = IREG_V(a0);
    IREG_V(a0) = CURT->sys_brk(arg0);
    LOG_SYSCALL_1("brk", "0x%lx", arg0, "0x%lx", IREG_V(a0));
    return pc + 4;
}

MP_SYSCALL_DEFINE(215, munmap) {
    VirtAddrT vaddr = IREG_V(a0);
    uint64_t length = IREG_V(a1);
    LOG_SYSCALL_2("munmap", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "%ld", 0UL);
    CURT->sys_munmap(vaddr, length);
    IREG_V(a0) = 0;
    return pc + 4;
}


MP_SYSCALL_DEFINE(222, mmap) {
    VirtAddrT vaddr = IREG_V(a0);
    uint64_t length = IREG_V(a1);
    uint64_t prot = IREG_V(a2);
    uint64_t flags = IREG_V(a3);
    int32_t fd = IREG_V(a4);
    uint64_t offset = IREG_V(a5);
    VirtAddrT ret = 0;
    RVThread *thread = cpu_devs[cpu_id].exec_thread;

    if(flags & MAP_ANONYMOUS) {
        PageFlagT pgflg = PGFLAG_ANON;
        if(prot & PROT_EXEC) pgflg |= PGFLAG_X;
        if(prot & PROT_READ) pgflg |= PGFLAG_R;
        if(prot & PROT_WRITE) pgflg |= PGFLAG_W;
        if(flags & MAP_SHARED) pgflg |= PGFLAG_SHARE;
        if(flags & MAP_PRIVATE) pgflg |= PGFLAG_PRIV;
        if(flags & MAP_STACK) pgflg |= (PGFLAG_R | PGFLAG_W | PGFLAG_STACK);
        bool initzero = (!(flags & MAP_STACK));
        string info = " ";
        if(flags & MAP_STACK) info = "stack";
        ret = (flags & MAP_FIXED)?(thread->sys_mmap_fixed(vaddr, length, pgflg, -1, 0, info)):(thread->sys_mmap(length, pgflg, -1, 0, info));
        LOG_SYSCALL_6("mmap", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", IREG_V(a4), "0x%lx", IREG_V(a5), "0x%lx", ret);
        IREG_V(a0) = ret;
        return pc + 4;
    }
    else if((flags & MAP_PRIVATE) && fd > 0) {
        PageFlagT pgflg = PGFLAG_PRIV;
        if(prot & PROT_EXEC) pgflg |= PGFLAG_X;
        if(prot & PROT_READ) pgflg |= PGFLAG_R;
        if(prot & PROT_WRITE) pgflg |= PGFLAG_W;
        ret = (flags & MAP_FIXED)?(thread->sys_mmap_fixed(vaddr, length, pgflg, -1, 0, "file")):(thread->sys_mmap(length, pgflg, -1, 0, "file"));
        LOG_SYSCALL_6("mmap", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", IREG_V(a4), "0x%lx", IREG_V(a5), "0x%lx", ret);
        IREG_V(a0) = ret;
        if(ret == 0) return pc+4;
        thread->save_context_switch(pc + 4, iregs);

        uint8_t *buf = new uint8_t[length];
        struct stat _fs;
        int sysfd = thread->fdtable_trans(fd);
        fstat(sysfd, &_fs);
        if(_fs.st_size > offset) {
            uint64_t valid_sz = std::min<uint64_t>(length, _fs.st_size - offset);
            uint8_t *tmpmap = (uint8_t *)mmap(0, valid_sz, PROT_READ, MAP_PRIVATE, thread->fdtable_trans(fd), offset);
            memcpy(buf, tmpmap, valid_sz);
            if(length > valid_sz) memset(buf + valid_sz, 0, length - valid_sz);
            munmap(tmpmap, valid_sz);
        }
        else {
            memset(buf, 0, length);
        }

        DMARequest req(DMAReqType::host_memory_read, ret, buf, length, (uint64_t)thread);
        fill_dma_iommu(&req, thread);
        dma->push_dma_request(req);

        DMAWaitThread waitthread;
        waitthread.thread = thread;
        waitthread.last_cpu_id = cpu_id;
        waitthread.ref_cnt = 1;
        waitthread.to_free.emplace_back(buf);

        sch_lock.lock();
        dma_wait_threads.emplace(thread, waitthread);
        bool nextthread = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
        sch_lock.unlock();
        if(nextthread) {
            assert(cpu_devs[cpu_id].exec_thread);
            RVRegArray regs;
            VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_switch(regs);
            cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
            return nextpc;
        }
        cpu_devs[cpu_id].cpu->halt();
        return 0;
    }
    else {
        CPUERROR("CPU%d Raise an mmap with unknown FLAGS: mmap(0x%lx, %ld, 0x%lx, 0x%lx, %d, %ld)",
            cpu_id, vaddr, length, prot, flags, fd, offset);
        assert(0);
    }
}

MP_SYSCALL_DEFINE(226, mprotect) {
    LOG_SYSCALL_3("mprotect", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", 0UL);
    uint32_t pgflag = 0;
    uint32_t flg = IREG_V(a2);
    if(flg & PROT_READ) pgflag |= PGFLAG_R;
    if(flg & PROT_WRITE) pgflag |= PGFLAG_W;
    if(flg & PROT_EXEC) pgflag |= PGFLAG_X;
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    thread->sys_mprotect(IREG_V(a0), IREG_V(a1), pgflag);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(233, madvise) {
    LOG_SYSCALL_3("madvise", "0x%lx", IREG_V(a0), "%ld", IREG_V(a1), "%ld", IREG_V(a2), "%ld", 0UL);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(261, prlimit) {
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    uint64_t resource = IREG_V(a1);
    if(IREG_V(a0) != 0 && IREG_V(a0) != thread->tid) {
        CPUERROR("CPU%d Raise an prlimit syscall with unknown thread id %ld @0x%lx", cpu_id, IREG_V(a0), pc);
        exit(0);
        return 0UL;
    }
    if(resource == RLIMIT_STACK) {
        LOG_SYSCALL_4("prlimit", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", 0UL);
        thread->save_context_host_ecall(pc + 4, iregs);
        return syscall_memory->get_syscall_proxy_entry(261);
    }
    else {
        CPUERROR("CPU%d Raise an prlimit syscall with unknown resource id %ld @0x%lx", cpu_id, resource, pc);
        exit(0);
        return 0UL;
    }
}

MP_SYSCALL_DEFINE(901, host_alloc) {
    IREG_V(a0) = syscall_memory->smem_alloc(IREG_V(a0));
    return pc + 4;
}

MP_SYSCALL_DEFINE(902, host_free) {
    syscall_memory->smem_free(IREG_V(a0));
    IREG_V(a0) = 0UL;
    return pc + 4;
}


MP_SYSCALL_DEFINE(904, host_get_tid_address) {
    IREG_V(a0) = CURT->clear_child_tid;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1017, host_getcwd) {
    char* retp = getcwd((char*)HOST_ADDR_OF_IREG(a0), IREG_V(a1));
    LOG_SYSCALL_2("host_getcwd", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "%s", (retp?((char*)HOST_ADDR_OF_IREG(a0)):""));
    IREG_V(a0) = (uint64_t)retp;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1029, host_ioctl) {
    uint64_t cmd = IREG_V(a1);
    if(cmd == 0) {
        CPUERROR("Unknown ioctl cmd 0x%lx", IREG_V(a3));
        assert(0);
    }
    int64_t ret = ioctl(CURT->fdtable_trans(IREG_V(a0)), cmd, HOST_ADDR_OF_IREG(a2));
    LOG_SYSCALL_4("host_ioctl", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
} 

MP_SYSCALL_DEFINE(910, host_ioctl_siocgifconf) {
    assert(IREG_V(a1) == SIOCGIFCONF);
    struct __tmp__ifconf {
        int     len;
        uint8_t *buf;
    } tmp;
    struct __tmp__ifconf * p = (struct __tmp__ifconf *)HOST_ADDR_OF_IREG(a2);
    tmp.len = p->len;
    tmp.buf = ((p->buf)?(syscall_memory->get_host_addr((uint64_t)(p->buf))):nullptr);
    int64_t ret = ioctl(CURT->fdtable_trans(IREG_V(a0)), IREG_V(a1), &tmp);
    p->len = tmp.len;
    LOG_SYSCALL_4("host_ioctl", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1048, host_faccessat) {
    uint64_t ret = faccessat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), IREG_V(a2), IREG_V(a3));
    LOG_SYSCALL_4("host_faccessat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1056, host_openat) {
    uint64_t ret = openat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), IREG_V(a2), IREG_V(a3));
    ret = CURT->fdtable_insert(ret);
    LOG_SYSCALL_4("host_openat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "0x%lx", IREG_V(a2), "%ld", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1063, host_read) {
    uint64_t ret = read(CURT->fdtable_trans(IREG_V(a0)), (void*)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
    LOG_SYSCALL_3("host_read", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1064, host_write) {
    uint64_t ret = write(CURT->fdtable_trans(IREG_V(a0)), (void*)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
    LOG_SYSCALL_3("host_write", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1078, host_readlinkat) {
    uint64_t ret = readlinkat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), (char*)HOST_ADDR_OF_IREG(a2), IREG_V(a3));
    LOG_SYSCALL_4("host_readlinkat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "%s", (char*)HOST_ADDR_OF_IREG(a2), "%ld", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1079, host_newfstatat) {
    uint64_t ret = fstatat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), (struct stat*)HOST_ADDR_OF_IREG(a2), IREG_V(a3));
    LOG_SYSCALL_4("host_newfstatat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1080, host_fstat) {
    uint64_t ret = fstat(CURT->fdtable_trans(IREG_V(a0)), (struct stat*)HOST_ADDR_OF_IREG(a1));
    LOG_SYSCALL_2("host_newfstatat", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1093, host_exit) {
    CPUFLOG("CPU%d Raise an ECALL: EXIT(%ld)", cpu_id, IREG_V(a0));
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    sch_lock.lock();
    if(thread->clear_child_tid) {
        // Wake up all futex
        PhysAddrT paddr = 0;
        assert(thread->va2pa(thread->clear_child_tid, &paddr, 0) == SimError::success);
        auto res = futex_wait_threads.find(paddr);
        if(res != futex_wait_threads.end()) {
            auto &tl = res->second;
            for(auto &fwt : tl) {
                fwt.thread->context_switch[isa::ireg_index_of("a0")] = 0;
                insert_ready_thread_nolock(fwt.thread, fwt.last_cpu_id);
            }
            futex_wait_threads.erase(res);
        }
    }
    if(switch_next_thread_nolock(cpu_id, SWFLAG_EXIT)) {
        sch_lock.unlock();
        assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_switch(regs);
        cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
        return nextpc;
    }
    else {
        sch_lock.unlock();
        cpu_devs[cpu_id].cpu->halt();
        return 0UL;
    }
}

typedef struct {
    uint64_t    uaddr;
    uint32_t    fval;
    uint32_t    futex_op;
    uint32_t    val;
    uint32_t    val2;
    uint64_t    uaddr2;
    uint32_t    fval2;
    uint32_t    val3;
    uint32_t    unlocked;
} HostFutexArgs;

MP_SYSCALL_DEFINE(1098, host_futex) {
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    HostFutexArgs *pargs = (HostFutexArgs*)HOST_ADDR_OF_IREG(a0);

    LOG_SYSCALL_6("host_futex", "0x%lx", pargs->uaddr, "0x%x", pargs->futex_op, "0x%x", pargs->val, "0x%lx", pargs->uaddr2, "0x%x", pargs->val2, "0x%x", pargs->val3, "%s", "xxx");

    uint64_t *plock = (uint64_t*)(syscall_memory->get_host_addr(syscall_memory->get_futex_lock_vaddr()));
    PhysAddrT paddr = 0, paddr2 = 0;
    assert(thread->va2pa(pargs->uaddr, &paddr, 0) == SimError::success);
    if(pargs->uaddr2) assert(thread->va2pa(pargs->uaddr2, &paddr2, 0) == SimError::success);

    uint32_t futex_op = pargs->futex_op;
    uint32_t futex_flag = (futex_op >> 8);
    futex_op &= 127;

    if(futex_op == FUTEX_WAIT || futex_op == FUTEX_WAIT_BITSET) {
        if(pargs->fval != pargs->val) {
            IREG_V(a0) = EAGAIN;
            return pc + 4;
        }
        CPULOG("CPU %d Futex Wait", cpu_id);
        thread->save_context_switch(pc + 4, iregs);
        sch_lock.lock();
        futex_wait_thread_insert(paddr, thread, (futex_op == FUTEX_WAIT_BITSET)?(pargs->val3):0, cpu_id);
        bool ret = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
        sch_lock.unlock();
        if(ret) {
            assert(cpu_devs[cpu_id].exec_thread);
            RVRegArray regs;
            VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_switch(regs);
            cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
            return nextpc;
        }
        cpu_devs[cpu_id].cpu->halt();
        return 0;
    }
    else if(futex_op == FUTEX_WAKE || futex_op == FUTEX_WAKE_BITSET) {
        FutexWaitThread buf;
        uint32_t futex_mask = ((futex_op == FUTEX_WAKE_BITSET)?(pargs->val3):0);
        uint64_t wake_cnt = 0;
        sch_lock.lock();
        for(wake_cnt = 0; wake_cnt < pargs->val; wake_cnt++) {
            if(!futex_wait_thread_pop(paddr, futex_mask, &buf)) {
                break;
            }
            insert_ready_thread_nolock(buf.thread, buf.last_cpu_id);
        }
        sch_lock.unlock();
        IREG_V(a0) = wake_cnt;
        return pc + 4;
    }
    else if(futex_op == FUTEX_WAKE) {
        LOG(ERROR) << "Unknown futex op";
        assert(0);
    }
    else if(futex_op == FUTEX_WAKE_BITSET) {
        LOG(ERROR) << "Unknown futex op";
        assert(0);
    }
    else if(futex_op == FUTEX_REQUEUE) {
        LOG(ERROR) << "Unknown futex op";
        assert(0);
    }
    else if(futex_op == FUTEX_CMP_REQUEUE) {
        LOG(ERROR) << "Unknown futex op";
        assert(0);
    }
    else if(futex_op == FUTEX_WAKE_OP) {
        LOG(ERROR) << "Unknown futex op";
        assert(0);
    }
    else if(futex_op == FUTEX_LOCK_PI) {
        LOG(ERROR) << "Unknown futex op";
        assert(0);
    }
    else if(futex_op == FUTEX_UNLOCK_PI) {
        LOG(ERROR) << "Unknown futex op";
        assert(0);
    }
    LOG(ERROR) << "Unknown futex op";
    assert(0);
    return 0;
}

MP_SYSCALL_DEFINE(1113, host_clock_gettime) {
    struct timespec* time = (struct timespec*)HOST_ADDR_OF_IREG(a1);
    uint64_t time_us = 0;
    uint64_t ret = 0;
    switch (IREG_V(a0))
    {
    case CLOCK_REALTIME:
        time_us = simroot::get_sim_time_us();
        break;
    case CLOCK_MONOTONIC:
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
        time_us = simroot::get_current_tick() / (simroot::get_global_freq() / 1000000UL);
        break;
    default:
        LOG(ERROR) << "Unknown clock type: " << IREG_V(a0);
        assert(0);
    }
    time->tv_sec = time_us / 1000000UL;
    time->tv_nsec = (time_us % 1000000UL) * 1000UL;
    LOG_SYSCALL_2("host_clock_gettime", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1134, host_sigaction) {
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    thread->sys_sigaction(IREG_V(a0), (RVKernelSigaction *)HOST_ADDR_OF_IREG(a1), (RVKernelSigaction *)HOST_ADDR_OF_IREG(a2));
    LOG_SYSCALL_3("host_sigaction", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", 0UL);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1135, host_sigprocmask) {
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    thread->sys_sigprocmask(IREG_V(a0), (sigset_t *)HOST_ADDR_OF_IREG(a1), (sigset_t *)HOST_ADDR_OF_IREG(a2));
    LOG_SYSCALL_3("host_sigprocmask", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", 0UL);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1220, host_clone) {
    uint64_t clone_flags = IREG_V(a0);
    VirtAddrT newsp = IREG_V(a1);
    VirtAddrT parent_tidptr = IREG_V(a2);
    VirtAddrT tls = IREG_V(a3);
    VirtAddrT child_tidptr = IREG_V(a4);
    RVThread *thread = cpu_devs[cpu_id].exec_thread;

    sch_lock.lock();

    VirtAddrT ret = alloc_tid();
    RVThread *newthread = new RVThread(thread, ret, clone_flags);
    newthread->clear_child_tid = child_tidptr;

    RVRegArray newregs;
    memcpy(newregs.data(), iregs.data(), iregs.size() * sizeof(uint64_t));
    newregs[isa::ireg_index_of("tp")] = newthread->context_host_ecall[isa::ireg_index_of("tp")] = tls;
    newregs[isa::ireg_index_of("sp")] = newthread->context_host_ecall[isa::ireg_index_of("sp")] = newsp;
    newregs[isa::ireg_index_of("a0")] = 0;
    newthread->save_context_switch(pc + 4, newregs);

    insert_ready_thread_nolock(newthread, cpu_id);

    sch_lock.unlock();

    LOG_SYSCALL_5("host_clone", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "0x%lx", IREG_V(a4), "%ld", ret);
    
    iregs[isa::ireg_index_of("a0")] = ret;
    return pc + 4;
}
// unsigned long, clone_flags, unsigned long, newsp,
// 		 int __user *, parent_tidptr,
// 		 unsigned long, tls,
// 		 int __user *, child_tidptr

MP_SYSCALL_DEFINE(1261, host_prlimit) {
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    thread->sys_prlimit(IREG_V(a1), (rlimit*)HOST_ADDR_OF_IREG(a2), (rlimit*)HOST_ADDR_OF_IREG(a3));
    LOG_SYSCALL_4("host_prlimit", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", 0UL);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1278, host_getrandom) {
    uint8_t* p = (uint8_t*)HOST_ADDR_OF_IREG(a0);
    uint64_t num = IREG_V(a1);
    for(uint64_t i = 0; i < num; i++) {
        p[i] = RAND(0,256);
    }
    LOG_SYSCALL_2("host_getrandom", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", num);
    IREG_V(a0) = num;
    return pc + 4;
}


#undef CURT
#undef IREG_V
#undef HOST_ADDR_OF_IREG
#undef LOG_SYSCALL_1
#undef LOG_SYSCALL_2
#undef LOG_SYSCALL_3
#undef LOG_SYSCALL_4
#undef LOG_SYSCALL_5
#undef CPULOG
#undef CPUFLOG
#undef CPUERROR

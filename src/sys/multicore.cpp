
#include "multicore.h"

#include "simroot.h"
#include "configuration.h"

#include <stdarg.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include <net/if.h>

#include <linux/futex.h>
#include <linux/sched.h>

using isa::ireg_value_of;

void SimSystemMultiCore::init(SimWorkload &workload, std::vector<CPUInterface*> &cpus, PhysPageAllocator *ppman, SimDMADevice *dma) {
    cpu_num = cpus.size();
    simroot_assert(cpu_num == conf::get_int("multicore", "cpu_number", cpu_num));
    for(int i = 0; i < cpu_num; i++) {
        cpu_devs.emplace_back(CPUDevice {
            .cpu = cpus[i],
            .exec_thread = nullptr
        });
    }

    this->dma = dma;

    this->def_ldpaths = workload.ldpaths;
    this->def_stacksz = workload.stack_size;
    this->def_syscall_proxy_path = workload.syscall_proxy_lib_path;

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
    log_bufs.assign(cpu_num, std::array<char, 512>());

    uint64_t entry = 0, sp = 0;
    RVThread *init_thread = new RVThread(workload, ppman, &entry, &sp);
    RVRegArray regs;
    isa::zero_regs(regs);
    regs[0] = entry;
    regs[isa::ireg_index_of("sp")] = sp;
    init_thread->save_context_stack(entry, regs, true);
    insert_ready_thread_nolock(init_thread, 0);

    alloc_current_tid = init_thread->tid + 1;
}



bool SimSystemMultiCore::switch_next_thread_nolock(uint32_t cpuid, uint32_t flag) {
    bool ret = false;
    uint64_t tid = cpu_devs[cpuid].exec_thread->tid;
    if(flag == SWFLAG_EXIT) {
        RVThread *thread = cpu_devs[cpuid].exec_thread;
        for(auto iter = thread->threadgroup->begin(); iter != thread->threadgroup->end(); ) {
            if(*iter == thread) iter = thread->threadgroup->erase(iter);
            else iter++;
        }
        // std::remove(thread->threadgroup->begin(), thread->threadgroup->end(), thread);
        for(auto t : thread->childs) {
            t->parent = nullptr;
        }
        delete thread;
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
        else if(log_syscall) {
            sprintf(log_buf, "SCHD: Exited thread %ld @CPU %d -> thread %ld", tid, cpuid, ret?(cpu_devs[cpuid].exec_thread->tid):0);
            simroot::print_log_info(log_buf);
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
        if(log_syscall) {
            sprintf(log_buf, "SCHD: Waited thread %ld @CPU %d -> thread %ld", tid, cpuid, ret?(cpu_devs[cpuid].exec_thread->tid):0);
            simroot::print_log_info(log_buf);
        }
    }
    else if(flag == SWFLAG_YIELD) {
        ready_threads.push_back(cpu_devs[cpuid].exec_thread);
        cpu_devs[cpuid].exec_thread = ready_threads.front();
        ready_threads.pop_front();
        if(log_syscall) {
            sprintf(log_buf, "SCHD: Yield thread %ld @CPU %d -> thread %ld", tid, cpuid, cpu_devs[cpuid].exec_thread->tid);
            simroot::print_log_info(log_buf);
        }
        return true;
    }
    else {
        LOG(ERROR) << "Unsupported switch flag " << flag;
        simroot_assert(0);
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
        VirtAddrT nextpc = thread->recover_context_stack(regs);
        cpu_devs[cpuid].cpu->redirect(nextpc, regs);
    }
    else {
        ready_threads.push_back(thread);
    }
    if(log_syscall) {
        if(cpuid < cpu_num) sprintf(log_buf, "SCHD: Ready thread %ld -> CPU %d", thread->tid, cpuid);
        else sprintf(log_buf, "SCHD: Ready thread %ld -> Wait Queue", thread->tid);
        simroot::print_log_info(log_buf);
    }
}

SimError SimSystemMultiCore::v_to_p(uint32_t cpu_id, VirtAddrT addr, PhysAddrT *out, PageFlagT flg) {
    simroot_assert(cpu_devs[cpu_id].exec_thread);
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
        default: simroot_assert(0);
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
        default: simroot_assert(0);
        }
    }
    else {
        simroot_assert(0);
    }
    return true;
}

VirtAddrT SimSystemMultiCore::ecall(uint32_t cpu_id, VirtAddrT addr, RVRegArray &regs) {
    uint64_t syscallid = regs[RV_REG_a7];
    if(log_info) {
        sprintf(log_buf, "CPU%d Raise an ECALL: %ld", cpu_id, syscallid);
        simroot::print_log_info(log_buf);
    }
    simroot_assert(cpu_devs[cpu_id].exec_thread);
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    CPUInterface *cpu = cpu_devs[cpu_id].cpu;

    VirtAddrT retpc = 0UL;

#define MP_SYSCALL_CASE(num, name) case num: retpc = MP_SYSCALL_FUNC_NAME(num, name)(cpu_id, addr, regs); break;
#define MP_EMPTY_CASE(num, name) case num: retpc = pc + 4; break;

    switch (syscallid)
    {
    MP_SYSCALL_CASE(24, dup3);
    MP_SYSCALL_CASE(57, close);
    MP_SYSCALL_CASE(62, lseek);
    MP_SYSCALL_CASE(94, exitgroup);
    MP_SYSCALL_CASE(96, set_tid_address);
    MP_SYSCALL_CASE(99, set_robust_list);
    MP_SYSCALL_CASE(122, sched_setaffinity);
    MP_SYSCALL_CASE(123, sched_getaffinity);
    MP_SYSCALL_CASE(124, sched_yield);
    MP_SYSCALL_CASE(131, tgkill);
    MP_SYSCALL_CASE(172, getpid);
    MP_SYSCALL_CASE(173, getppid);
    MP_SYSCALL_CASE(174, getuid);
    MP_SYSCALL_CASE(175, geteuid);
    MP_SYSCALL_CASE(176, getgid);
    MP_SYSCALL_CASE(177, getegid);
    MP_SYSCALL_CASE(178, gettid);
    MP_SYSCALL_CASE(198, socket);
    MP_SYSCALL_CASE(201, listen);
    MP_SYSCALL_CASE(214, brk);
    MP_SYSCALL_CASE(215, munmap);
    MP_SYSCALL_CASE(222, mmap);
    MP_SYSCALL_CASE(226, mprotect);
    MP_SYSCALL_CASE(233, madvise);
    MP_SYSCALL_CASE(261, prlimit);

    MP_SYSCALL_CASE(901, host_alloc);
    MP_SYSCALL_CASE(902, host_free);
    MP_SYSCALL_CASE(904, host_get_tid_address);
    MP_SYSCALL_CASE(905, host_free_cow_tmp_page);
    MP_SYSCALL_CASE(910, host_ioctl_siocgifconf);
    MP_SYSCALL_CASE(1017, host_getcwd);
    MP_SYSCALL_CASE(1025, host_fcntl);
    MP_SYSCALL_CASE(1029, host_ioctl);
    MP_SYSCALL_CASE(1048, host_faccessat);
    MP_SYSCALL_CASE(1056, host_openat);
    MP_SYSCALL_CASE(1059, host_pipe2);
    MP_SYSCALL_CASE(1061, host_getdents);
    MP_SYSCALL_CASE(1063, host_read);
    MP_SYSCALL_CASE(1064, host_write);
    MP_SYSCALL_CASE(1072, host_pselect6);
    MP_SYSCALL_CASE(1073, host_ppoll);
    MP_SYSCALL_CASE(1078, host_readlinkat);
    MP_SYSCALL_CASE(1079, host_newfstatat);
    MP_SYSCALL_CASE(1080, host_fstat);
    MP_SYSCALL_CASE(1093, host_exit);
    MP_SYSCALL_CASE(1098, host_futex);
    MP_SYSCALL_CASE(1113, host_clock_gettime);
    MP_SYSCALL_CASE(1115, host_clock_nanosleep);
    MP_SYSCALL_CASE(1134, host_sigaction);
    MP_SYSCALL_CASE(1135, host_sigprocmask);
    MP_SYSCALL_CASE(1199, host_socketpair);
    MP_SYSCALL_CASE(1200, host_bind);
    MP_SYSCALL_CASE(1203, host_connect);
    MP_SYSCALL_CASE(1204, host_getsockname);
    MP_SYSCALL_CASE(1206, host_sendto);
    MP_SYSCALL_CASE(1208, host_setsockopt);
    MP_SYSCALL_CASE(1209, host_getsockopt);
    MP_SYSCALL_CASE(1212, host_recvmsg);
    MP_SYSCALL_CASE(1220, host_clone);
    MP_SYSCALL_CASE(1221, host_execve);
    MP_SYSCALL_CASE(1261, host_prlimit);
    MP_SYSCALL_CASE(1278, host_getrandom);
    MP_SYSCALL_CASE(1435, host_clone3);
    MP_SYSCALL_CASE(1439, host_faccessat2);
    default:
        retpc = syscall_memory->get_syscall_proxy_entry(syscallid);
        if(retpc != 0 && syscallid < 500) {
            // if(log_syscall) {
            //     sprintf(log_bufs[cpu_id].data(), "CPU%d ECALL->Host_Proxy_%ld(0x%lx, 0x%lx, 0x%lx, 0x%lx)",
            //     cpu_id, syscallid,
            //     regs[RV_REG_a0],
            //     regs[RV_REG_a1],
            //     regs[RV_REG_a2],
            //     regs[RV_REG_a3]
            //     );
            //     simroot::print_log_info(log_bufs[cpu_id].data());
            // }
            thread->save_context_stack(addr + 4, regs, false);
        }
        else {
            sprintf(log_bufs[cpu_id].data(), "CPU%d Raise an Unkonwn ECALL %ld @0x%lx, arg:0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx",
                cpu_id, syscallid, addr,
                regs[RV_REG_a0],
                regs[RV_REG_a1],
                regs[RV_REG_a2],
                regs[RV_REG_a3],
                regs[RV_REG_a4],
                regs[RV_REG_a5]
            );
            simroot::print_log_info(string(log_bufs[cpu_id].data()));
            printf("%s\n", log_bufs[cpu_id].data());
            fflush(stdout);
            exit(0);
            return 0UL;
        }
    }

#undef MP_EMPTY_CASE
#undef MP_SYSCALL_CASE

    return retpc;
}

VirtAddrT SimSystemMultiCore::ebreak(uint32_t cpu_id, VirtAddrT addr, RVRegArray &iregs) {
    if(log_info) {
        sprintf(log_buf, "CPU%d Raise an EBREAK @0x%lx", cpu_id, addr);
        simroot::print_log_info(log_buf);
    }
    simroot_assert(cpu_devs[cpu_id].exec_thread);
    RVThread *thread = cpu_devs[cpu_id].exec_thread;
    CPUInterface *cpu = cpu_devs[cpu_id].cpu;
    if(is_dev_mem(cpu_id, addr)) {
        // This is an ecall ret
        return thread->recover_context_stack(iregs);
    }
    else {
        sprintf(log_buf, "CPU%d Raise an EBREAK @0x%lx", cpu_id, addr);
        simroot::print_log_info(log_buf);
        cpu->halt();
        simroot::stop_sim_and_exit();
        return 0UL;
    }
}

VirtAddrT SimSystemMultiCore::exception(uint32_t cpu_id, VirtAddrT pc, SimError expno, uint64_t arg1, uint64_t arg2, RVRegArray &iregs) {
    if(expno == SimError::pagefault) {
        RVThread *curt = cpu_devs[cpu_id].exec_thread;
        curt->save_context_stack(pc, iregs, true);

        VPageIndexT missed = (arg1 >> PAGE_ADDR_OFFSET);
        VPageIndexT tmppage = curt->cow_realloc_missed_page(missed);

        iregs[RV_REG_a0] = (missed << PAGE_ADDR_OFFSET);
        iregs[RV_REG_a1] = (tmppage << PAGE_ADDR_OFFSET);
        iregs[RV_REG_a2] = PAGE_LEN_BYTE;

        if(log_syscall) {
            sprintf(log_bufs[cpu_id].data(), "CPU%d: Page Fault @0x%lx @0x%lx", cpu_id, pc, missed << PAGE_ADDR_OFFSET);
            simroot::print_log_info(log_bufs[cpu_id].data());
        }

        return syscall_memory->get_syscall_proxy_entry(501);
    }
    else {
        sprintf(log_bufs[cpu_id].data(), "CPU%d: Unknown Exception %d @0x%lx", cpu_id, (int32_t)expno, pc);
        LOG(ERROR)<<(log_bufs[cpu_id].data());
        simroot_assert(0);
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

void * SimSystemMultiCore::poll_wait_thread_function(void * _param) {
    PollWaitThread *p = (PollWaitThread*)_param;
    SimSystemMultiCore * sys = p->sys;

    double simtime2realtime = ((double)(simroot::get_global_freq()) / (double)(simroot::get_sim_tick_per_real_sec()));
    int64_t ret = poll((struct pollfd *)(p->host_fds), p->nfds, (simtime2realtime * p->timeout_us) / 1000);
    if(ret < 0) ret = -errno;
    p->thread->context_stack.back().regs[RV_REG_a0] = ret;

    if(sys->log_syscall) {
        char buf[256];
        sprintf(buf, "Thread %ld Syscall host_poll Return %ld", p->thread->tid, ret);
        simroot::print_log_info(buf);
    }

    sys->sch_lock.lock();
    sys->poll_wait_threads.erase(p->thread);
    sys->insert_ready_thread_nolock(p->thread, p->cpuid);
    sys->sch_lock.unlock();

    return nullptr;
}

void * SimSystemMultiCore::select_wait_thread_function(void * _param) {
    SelectWaitThread *p = (SelectWaitThread*)_param;
    SimSystemMultiCore * sys = p->sys;

    double simtime2realtime = ((double)(simroot::get_global_freq()) / (double)(simroot::get_sim_tick_per_real_sec()));
    uint64_t rtmo_us = (simtime2realtime * p->timeout_us);
    timespec timeout;
    timeout.tv_sec = rtmo_us / 1000000UL;
    timeout.tv_nsec = 1000UL * (rtmo_us % 1000000UL);
    
    int64_t ret = pselect(
        p->host_nfds,
        ((p->readfds)?(&p->host_readfds):nullptr),
        ((p->writefds)?(&p->host_writefds):nullptr),
        ((p->exceptfds)?(&p->host_exceptfds):nullptr),
        &timeout,
        nullptr
    );
    if(ret < 0) ret = -errno;
    p->thread->context_stack.back().regs[RV_REG_a0] = ret;

    if(ret > 0) {
        if(p->readfds) {
            FD_ZERO(p->readfds);
            for(int i = 0; i < p->host_nfds; i++) {
                if(FD_ISSET(i, &p->host_readfds)) FD_SET(p->hostfd_to_simfd[i], p->readfds);
            }
        }
        if(p->writefds) {
            FD_ZERO(p->writefds);
            for(int i = 0; i < p->host_nfds; i++) {
                if(FD_ISSET(i, &p->host_writefds)) FD_SET(p->hostfd_to_simfd[i], p->writefds);
            }
        }
        if(p->exceptfds) {
            FD_ZERO(p->exceptfds);
            for(int i = 0; i < p->host_nfds; i++) {
                if(FD_ISSET(i, &p->host_exceptfds)) FD_SET(p->hostfd_to_simfd[i], p->exceptfds);
            }
        }
    }

    if(sys->log_syscall) {
        char buf[256];
        sprintf(buf, "Thread %ld Syscall host_select Return %ld", p->thread->tid, ret);
        simroot::print_log_info(buf);
    }

    sys->sch_lock.lock();
    sys->select_wait_threads.erase(p->thread);
    sys->insert_ready_thread_nolock(p->thread, p->cpuid);
    sys->sch_lock.unlock();

    return nullptr;
}

void * SimSystemMultiCore::sleep_wait_thread_function(void * _param) {
    SleepWaitThread *p = (SleepWaitThread*)_param;
    SimSystemMultiCore * sys = p->sys;

    int64_t ret = clock_nanosleep(0, 0, &p->host_time, 0);
    if(ret < 0) ret = -errno;
    p->thread->context_stack.back().regs[RV_REG_a0] = ret;

    if(sys->log_syscall) {
        char buf[256];
        sprintf(buf, "Thread %ld Syscall host_clock_nanosleep Return %ld", p->thread->tid, ret);
        simroot::print_log_info(buf);
    }

    sys->sch_lock.lock();
    sys->poll_wait_threads.erase(p->thread);
    sys->insert_ready_thread_nolock(p->thread, p->cpuid);
    sys->sch_lock.unlock();

    return nullptr;
}

void * SimSystemMultiCore::blkread_wait_thread_function(void * _param) {
    BlockreadWaitThread *p = (BlockreadWaitThread*)_param;
    SimSystemMultiCore * sys = p->sys;

    int64_t ret = read(p->hostfd, p->buf, p->bufsz);
    if(ret < 0) ret = -errno;
    p->thread->context_stack.back().regs[RV_REG_a0] = ret;

    if(sys->log_syscall) {
        char buf[256];
        sprintf(buf, "Thread %ld Syscall host_read_blocked Return %ld", p->thread->tid, ret);
        simroot::print_log_info(buf);
    }

    sys->sch_lock.lock();
    sys->blkread_wait_threads.erase(p->thread);
    sys->insert_ready_thread_nolock(p->thread, p->cpuid);
    sys->sch_lock.unlock();

    return nullptr;
}

void * SimSystemMultiCore::socksend_wait_thread_function(void * _param) {
    SockSendWaitThread *p = (SockSendWaitThread*)_param;
    SimSystemMultiCore * sys = p->sys;

    int64_t ret = sendto(p->hostfd, p->buf, p->bufsz, p->flags, (struct sockaddr *)(p->dest_addr), p->addrlen);
    if(ret < 0) ret = -errno;
    p->thread->context_stack.back().regs[RV_REG_a0] = ret;

    if(sys->log_syscall) {
        char buf[256];
        sprintf(buf, "Thread %ld Syscall host_sendto Return %ld", p->thread->tid, ret);
        simroot::print_log_info(buf);
    }

    sys->sch_lock.lock();
    sys->socksend_wait_threads.erase(p->thread);
    sys->insert_ready_thread_nolock(p->thread, p->cpuid);
    sys->sch_lock.unlock();

    return nullptr;
}

void * SimSystemMultiCore::sockrecv_wait_thread_function(void * _param) {
    SockRecvWaitThread *p = (SockRecvWaitThread*)_param;
    SimSystemMultiCore * sys = p->sys;

    int64_t ret = recvmsg(p->hostfd, &p->msg, p->flags);
    if(ret < 0) ret = -errno;
    else {
        p->hbuf_msg->msg_controllen = p->msg.msg_controllen;
        p->hbuf_msg->msg_namelen = p->msg.msg_namelen;
        p->hbuf_msg->msg_flags = p->msg.msg_flags;
    }
    p->thread->context_stack.back().regs[RV_REG_a0] = ret;

    if(sys->log_syscall) {
        char buf[256];
        sprintf(buf, "Thread %ld Syscall host_recvmsg Return %ld", p->thread->tid, ret);
        simroot::print_log_info(buf);
    }

    sys->sch_lock.lock();
    sys->sockrecv_wait_threads.erase(p->thread);
    sys->insert_ready_thread_nolock(p->thread, p->cpuid);
    sys->sch_lock.unlock();

    return nullptr;
}




// -----------------------------------------------------------------
//   Syscall Handlers
// -----------------------------------------------------------------

#define CURT (cpu_devs[cpu_id].exec_thread)
#define IREG_V(rname) (iregs[RV_REG_##rname])
#define HOST_ADDR_OF_IREG(rname) (syscall_memory->get_host_addr(IREG_V(rname)))
#define CPULOG(fmt, ...) do {if(log_syscall) { sprintf(log_bufs[cpu_id].data(), fmt, ##__VA_ARGS__ ); simroot::print_log_info(log_bufs[cpu_id].data());}} while(0)
#define CPUFLOG(fmt, ...) do {sprintf(log_bufs[cpu_id].data(), fmt, ##__VA_ARGS__ ); simroot::print_log_info(log_bufs[cpu_id].data());} while(0)
#define CPUERROR(fmt, ...) do {sprintf(log_bufs[cpu_id].data(), fmt, ##__VA_ARGS__ ); LOG(ERROR)<<(log_bufs[cpu_id].data());} while(0)

#define LOG_SYSCALL_1(name, f0, a0, rf0, r) do {if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ") -> " rf0 "", cpu_id, a0, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}} while(0)

#define LOG_SYSCALL_2(name, f0, a0, f1, a1, rf0, r) do {if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ") -> " rf0 "", cpu_id, a0, a1, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}} while(0)

#define LOG_SYSCALL_3(name, f0, a0, f1, a1, f2, a2, rf0, r) do {if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ", " f2 ") -> " rf0 "", cpu_id, a0, a1, a2, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}} while(0)

#define LOG_SYSCALL_4(name, f0, a0, f1, a1, f2, a2, f3, a3, rf0, r) do {if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ", " f2 ", " f3 ") -> " rf0 "", cpu_id, a0, a1, a2, a3, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}} while(0)

#define LOG_SYSCALL_5(name, f0, a0, f1, a1, f2, a2, f3, a3, f4, a4, rf0, r) do {if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ", " f2 ", " f3 ", " f4 ") -> " rf0 "", cpu_id, a0, a1, a2, a3, a4, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}} while(0)

#define LOG_SYSCALL_6(name, f0, a0, f1, a1, f2, a2, f3, a3, f4, a4, f5, a5, rf0, r) do {if(log_syscall) {\
sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall " name "(" f0 ", " f1 ", " f2 ", " f3 ", " f4 ", " f5 ") -> " rf0 "", cpu_id, a0, a1, a2, a3, a4, a5, r);\
simroot::print_log_info(log_bufs[cpu_id].data());}} while(0)


MP_SYSCALL_DEFINE(24, dup3) {
    int32_t sim_oldfd = IREG_V(a0);
    int32_t sim_newfd = IREG_V(a1);
    uint64_t flg = IREG_V(a2);
    int32_t host_oldfd = CURT->fdtable_trans(sim_oldfd);
    int32_t host_newfd = CURT->fdtable_trans(sim_newfd);
    int64_t ret = 0;

    if(flg) {
        CPUERROR("Unknown dup3 flag: 0x%lx", flg);
        simroot_assert(0);
    }

    if(!host_oldfd) {
        ret = -EBADF;
    }
    else if(sim_newfd == sim_oldfd) {
        ret = -EINVAL;
    }
    else {
        if(host_newfd > 2) {
            close(host_newfd);
        }
        CURT->fdtable_force_insert(sim_newfd, dup(host_oldfd));
        ret = 0;
    }

    LOG_SYSCALL_3("dup3", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);

    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(57, close) {
    int32_t host_fd = CURT->fdtable_pop(IREG_V(a0));
    int64_t ret = ((host_fd > 2)?close(host_fd):0);
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_2("close", "%ld", IREG_V(a0), "%d", host_fd, "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(62, lseek) {
    int64_t ret = lseek(CURT->fdtable_trans(IREG_V(a0)), IREG_V(a1), IREG_V(a2));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_3("lseek", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(94, exitgroup) {
    CPUFLOG("CPU%d Raise an ECALL: EXITGROUP", cpu_id);
    RVThread *thread = CURT;
    sch_lock.lock();
    // 结束所有子线程
    for(RVThread *child : *(CURT->threadgroup)) {
        if(child == CURT) continue;
        for(auto &u : cpu_devs) {
            if(u.exec_thread == child) {
                u.cpu->halt();
                u.exec_thread = nullptr;
            }
        }
        for(auto &e : futex_wait_threads) {
            for(auto iter = e.second.begin(); iter != e.second.end(); ) {
                if(iter->thread == child) iter = e.second.erase(iter);
                else iter++;
            }
        }
        cancle_wait_thread_nolock(child);
        for(auto iter = ready_threads.begin(); iter != ready_threads.end(); ) {
            if(*iter == child) iter = ready_threads.erase(iter);
            else iter++;
        }
        // std::remove(ready_threads.begin(), ready_threads.end(), child);
        waiting_threads.erase(child);
        CPUFLOG("CPU%d : Cancle Thread %ld", cpu_id, child->tid);
        delete child;
    }
    if(thread->do_child_cleartid) {
        // Wake up all futex
        PhysAddrT paddr = 0;
        simroot_assert(thread->va2pa(thread->clear_child_tid, &paddr, 0) == SimError::success);
        auto res = futex_wait_threads.find(paddr);
        if(res != futex_wait_threads.end()) {
            auto &tl = res->second;
            for(auto &fwt : tl) {
                fwt.thread->context_stack.back().regs[RV_REG_a0] = 0;
                insert_ready_thread_nolock(fwt.thread, fwt.last_cpu_id);
            }
            futex_wait_threads.erase(res);
        }
    }
    if(switch_next_thread_nolock(cpu_id, SWFLAG_EXIT)) {
        sch_lock.unlock();
        simroot_assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
        cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
        return nextpc;
    }
    else {
        sch_lock.unlock();
        cpu_devs[cpu_id].cpu->halt();
        return 0UL;
    }
    // simroot::stop_sim_and_exit();
    // return 0UL;
    // if(switch_next_thread_lock(cpu_id, SWFLAG_EXIT)) {
    //     simroot_assert(cpu_devs[cpu_id].exec_thread);
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

MP_SYSCALL_DEFINE(122, sched_setaffinity) {
    LOG_SYSCALL_3("sched_setaffinity", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", 0UL);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(123, sched_getaffinity) {
    LOG_SYSCALL_3("sched_getaffinity", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", 0UL);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(124, sched_yield) {
    LOG_SYSCALL_1("sched_yield", "%ld", IREG_V(a0), "%ld", 0UL);
    IREG_V(a0) = 0;
    CURT->save_context_stack(pc + 4, iregs, true);
    sch_lock.lock();
    simroot_assert(switch_next_thread_nolock(cpu_id, SWFLAG_YIELD));
    sch_lock.unlock();
    RVRegArray regs;
    VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
    cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
    return nextpc;
}

MP_SYSCALL_DEFINE(131, tgkill) {
    LOG_SYSCALL_3("tgkill", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "%ld", IREG_V(a2), "%ld", 0UL);
    uint32_t signal = IREG_V(a2);
    uint64_t tgid = IREG_V(a0);
    uint64_t tid = IREG_V(a1);
    IREG_V(a0) = -EINVAL;
    CPUERROR("Unknown Signal %d\n", signal);
    return pc + 4;
}

MP_SYSCALL_DEFINE(172, getpid) {
    int64_t ret = CURT->pid;
    LOG_SYSCALL_1("getpid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(173, getppid) {
    int64_t ret = ((CURT->parent)?(CURT->parent->pid):getpid());
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_1("getppid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(174, getuid) {
    int64_t ret = getuid();
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_1("getuid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(175, geteuid) {
    int64_t ret = geteuid();
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_1("geteuid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(176, getgid) {
    int64_t ret = getgid();
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_1("getgid", "%ld", IREG_V(a0), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(177, getegid) {
    int64_t ret = getegid();
    if(ret < 0) ret = -errno;
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
    int64_t ret = socket(IREG_V(a0), IREG_V(a1), IREG_V(a2));
    if(ret < 0) ret = -errno;
    else ret = CURT->fdtable_insert(ret);
    LOG_SYSCALL_3("socket", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(201, listen) {
    int64_t ret = listen(CURT->fdtable_trans(IREG_V(a0)), IREG_V(a1));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_2("listen", "%ld", IREG_V(a0), "%ld", IREG_V(a1), "%ld", ret);
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
        LOG_SYSCALL_6("mmap", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", IREG_V(a4), "0x%lx", IREG_V(a5), "%ld", ret);
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
        thread->save_context_stack(pc + 4, iregs, true);

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

        list<DMARequestUnit> reqs;
        VirtAddrT dma_dst = ret;
        uint64_t cur = 0;
        if(dma_dst & (PAGE_LEN_BYTE - 1)) {
            uint32_t offset = (dma_dst & (PAGE_LEN_BYTE - 1));
            cur = std::min<uint32_t>(PAGE_LEN_BYTE - offset, length);
            PhysAddrT paddr = 0;
            simroot_assert(thread->va2pa(dma_dst, &paddr, 0) == SimError::success);
            reqs.emplace_back(DMARequestUnit{
                .src = (PhysAddrT)buf,
                .dst = paddr,
                .size = (uint32_t)cur,
                .flag = DMAFLG_SRC_HOST,
                .callback = (uint64_t)thread
            });
        }
        while(cur < length) {
            PhysAddrT paddr = 0;
            simroot_assert(thread->va2pa(dma_dst + cur, &paddr, 0) == SimError::success);
            uint32_t step = std::min<uint32_t>(PAGE_LEN_BYTE, length - cur);
            reqs.emplace_back(DMARequestUnit{
                .src = ((PhysAddrT)buf) + cur,
                .dst = paddr,
                .size = step,
                .flag = DMAFLG_SRC_HOST,
                .callback = (uint64_t)thread
            });
            cur += step;
        }

        DMAWaitThread waitthread;
        waitthread.thread = thread;
        waitthread.last_cpu_id = cpu_id;
        waitthread.ref_cnt = reqs.size();
        waitthread.to_free.emplace_back(buf);

        dma->push_dma_requests(reqs);

        sch_lock.lock();
        dma_wait_threads.emplace(thread, waitthread);
        bool nextthread = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
        sch_lock.unlock();
        if(nextthread) {
            simroot_assert(cpu_devs[cpu_id].exec_thread);
            RVRegArray regs;
            VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
            cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
            return nextpc;
        }
        cpu_devs[cpu_id].cpu->halt();
        return 0;
    }
    else {
        CPUERROR("CPU%d Raise an mmap with unknown FLAGS: mmap(0x%lx, %ld, 0x%lx, 0x%lx, %d, %ld)",
            cpu_id, vaddr, length, prot, flags, fd, offset);
        simroot_assert(0);
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
        thread->save_context_stack(pc + 4, iregs, true);
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

MP_SYSCALL_DEFINE(905, host_free_cow_tmp_page) {
    CURT->cow_free_tmp_page(IREG_V(a0) >> PAGE_ADDR_OFFSET);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1017, host_getcwd) {
    char* retp = getcwd((char*)HOST_ADDR_OF_IREG(a0), IREG_V(a1));
    if((int64_t)retp < 0) retp = (char*)((uint64_t)(-errno));
    LOG_SYSCALL_2("host_getcwd", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "%s", (retp?((char*)HOST_ADDR_OF_IREG(a0)):""));
    IREG_V(a0) = (uint64_t)retp;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1025, host_fcntl) {
    int32_t hostfd = CURT->fdtable_trans(IREG_V(a0));
    uint64_t op = IREG_V(a1);
    int64_t ret = 0;
    if(op == F_GETFD || op == F_SETFD || op == F_GETFL || op == F_SETFL) {
        ret = fcntl(hostfd, op, IREG_V(a2));
        if(ret < 0) ret = -errno;
    }
    else [[unlikely]] {
        CPUERROR("Unknown fcntl op 0x%lx", op);
        simroot_assert(0);
    }
    LOG_SYSCALL_3("host_fcntl", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1029, host_ioctl) {
    uint64_t cmd = IREG_V(a1);
    if(cmd == 0) [[unlikely]] {
        CPUERROR("Unknown ioctl cmd 0x%lx", IREG_V(a3));
        simroot_assert(0);
    }
    int64_t ret = ioctl(CURT->fdtable_trans(IREG_V(a0)), cmd, HOST_ADDR_OF_IREG(a2));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_4("host_ioctl", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
} 

MP_SYSCALL_DEFINE(910, host_ioctl_siocgifconf) {
    simroot_assert(IREG_V(a1) == SIOCGIFCONF);
    struct __tmp__ifconf {
        int     len;
        uint8_t *buf;
    } tmp;
    struct __tmp__ifconf * p = (struct __tmp__ifconf *)HOST_ADDR_OF_IREG(a2);
    tmp.len = p->len;
    tmp.buf = ((p->buf)?(syscall_memory->get_host_addr((uint64_t)(p->buf))):nullptr);
    int64_t ret = ioctl(CURT->fdtable_trans(IREG_V(a0)), IREG_V(a1), &tmp);
    if(ret < 0) ret = -errno;
    p->len = tmp.len;
    LOG_SYSCALL_4("host_ioctl", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1048, host_faccessat) {
    int64_t ret = faccessat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), IREG_V(a2), IREG_V(a3));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_4("host_faccessat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1056, host_openat) {
    int64_t ret = openat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), IREG_V(a2), IREG_V(a3));
    if(ret < 0) ret = -errno;
    else ret = CURT->fdtable_insert(ret);
    LOG_SYSCALL_4("host_openat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "0x%lx", IREG_V(a2), "%ld", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1059, host_pipe2) {
    int32_t fds[2];
    int64_t ret = pipe2(fds, IREG_V(a1));
    if(ret < 0) ret = -errno;
    int32_t *simfds = (int32_t*)HOST_ADDR_OF_IREG(a0);
    simfds[0] = CURT->fdtable_insert(fds[0]);
    simfds[1] = CURT->fdtable_insert(fds[1]);
    LOG_SYSCALL_3("host_pipe2", "%ld", IREG_V(a1), "->%d", simfds[0], "->%d", simfds[1], "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1061, host_getdents) {
    int64_t ret = syscall(SYS_getdents, CURT->fdtable_trans(IREG_V(a0)), (void*)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
    LOG_SYSCALL_3("host_getdents", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1063, host_read) {
    int32_t simfd = IREG_V(a0);
    int32_t hostfd = CURT->fdtable_trans(IREG_V(a0));
    if(hostfd < 0) {
        IREG_V(a0) = -EBADF;
        return pc + 4;
    }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(hostfd, &fds);
    struct timeval zerotime;
    zerotime.tv_sec = zerotime.tv_usec = 0;
    int64_t select_ret = select(hostfd + 1, &fds, nullptr, nullptr, &zerotime);
    if(select_ret > 0) {
        int64_t ret = read(CURT->fdtable_trans(IREG_V(a0)), (void*)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
        if(ret < 0) ret = -errno;
        LOG_SYSCALL_3("host_read", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
        IREG_V(a0) = ret;
        return pc + 4;
    }
    else if(select_ret == 0) {
        BlockreadWaitThread wt;
        wt.thread = CURT;
        wt.sys = this;
        wt.cpuid = cpu_id;
        wt.simfd = simfd;
        wt.hostfd = hostfd;
        wt.buf = (uint8_t*)HOST_ADDR_OF_IREG(a1);
        wt.bufsz = IREG_V(a2);
        
        LOG_SYSCALL_3("host_read_blocked", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%s", "blocked");

        sch_lock.lock();
        auto iter = blkread_wait_threads.emplace(CURT, wt).first;
        sch_lock.unlock();
        pthread_create(&wt.th, nullptr, blkread_wait_thread_function, &(iter->second));

        CURT->save_context_stack(pc + 4, iregs, true);
        sch_lock.lock();
        bool ret = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
        sch_lock.unlock();
        if(ret) {
            simroot_assert(cpu_devs[cpu_id].exec_thread);
            RVRegArray regs;
            VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
            cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
            return nextpc;
        }
        cpu_devs[cpu_id].cpu->halt();
        return 0;
    }
    
    CPUERROR("CPU %d Failed to read fd %d->%d", cpu_id, simfd, hostfd);
    simroot_assert(0);
}

MP_SYSCALL_DEFINE(1064, host_write) {
    if(IREG_V(a0) == 1) {
        simroot::log_stdout((char*)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
    }
    else if(IREG_V(a0) == 2) {
        simroot::log_stderr((char*)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
    }
    int64_t ret = write(CURT->fdtable_trans(IREG_V(a0)), (void*)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_3("host_write", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1072, host_pselect6) {
    int nfds = IREG_V(a0);
    fd_set *readfds = (fd_set*)HOST_ADDR_OF_IREG(a1);
    fd_set *writefds = (fd_set*)HOST_ADDR_OF_IREG(a2);
    fd_set *exceptfds = (fd_set*)HOST_ADDR_OF_IREG(a3);
    struct timespec * tmo = (struct timespec *)HOST_ADDR_OF_IREG(a4);
    sigset_t * sigmask = (sigset_t *)HOST_ADDR_OF_IREG(a5);

    if(sigmask) {
        CPUERROR("Un-implemented: pselect with sigmask");
        simroot_assert(0);
    }

    SelectWaitThread wt;
    wt.thread = CURT;
    wt.nfds = nfds;
    wt.timeout_us = (tmo?(tmo->tv_nsec / 1000L + tmo->tv_sec * 1000000L):-1L);
    wt.sys = this;
    wt.cpuid = cpu_id;
    wt.readfds = readfds;
    wt.writefds = writefds;
    wt.exceptfds = exceptfds;
    FD_ZERO(&wt.host_readfds);
    FD_ZERO(&wt.host_writefds);
    FD_ZERO(&wt.host_exceptfds);
    
    bool invalid_fd = false;
    for(int i = 0; i < nfds; i++) {
        if(readfds && FD_ISSET(i, readfds)) {
            int32_t hostfd = CURT->fdtable_trans(i);
            if(hostfd < 0) {
                invalid_fd = true;
                break;
            }
            wt.host_nfds = std::max<int32_t>(wt.host_nfds, hostfd + 1);
            wt.hostfd_to_simfd.emplace(hostfd, i);
            FD_SET(hostfd, &wt.host_readfds);
        }
        if(writefds && FD_ISSET(i, writefds)) {
            int32_t hostfd = CURT->fdtable_trans(i);
            if(hostfd < 0) {
                invalid_fd = true;
                break;
            }
            wt.host_nfds = std::max<int32_t>(wt.host_nfds, hostfd + 1);
            wt.hostfd_to_simfd.emplace(hostfd, i);
            FD_SET(hostfd, &wt.host_writefds);
        }
        if(exceptfds && FD_ISSET(i, exceptfds)) {
            int32_t hostfd = CURT->fdtable_trans(i);
            if(hostfd < 0) {
                invalid_fd = true;
                break;
            }
            wt.host_nfds = std::max<int32_t>(wt.host_nfds, hostfd + 1);
            wt.hostfd_to_simfd.emplace(hostfd, i);
            FD_SET(hostfd, &wt.host_exceptfds);
        }
    }
    
    if(log_syscall) {
        sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall host_pselect6: TMO %ldus", cpu_id, wt.timeout_us);
        string str = string(log_bufs[cpu_id].data());
        if(readfds) {
            str += ", RFDs: ";
            for(int i = 0; i < nfds; i++) {
                if(FD_ISSET(i, readfds)) {
                    sprintf(log_bufs[cpu_id].data(), "%d->%d ", i, CURT->fdtable_trans(i));
                    str += string(log_bufs[cpu_id].data());
                }
            }
        }
        if(writefds) {
            str += ", WFDs: ";
            for(int i = 0; i < nfds; i++) {
                if(FD_ISSET(i, writefds)) {
                    sprintf(log_bufs[cpu_id].data(), "%d->%d ", i, CURT->fdtable_trans(i));
                    str += string(log_bufs[cpu_id].data());
                }
            }
        }
        if(exceptfds) {
            str += ", EFDs: ";
            for(int i = 0; i < nfds; i++) {
                if(FD_ISSET(i, exceptfds)) {
                    sprintf(log_bufs[cpu_id].data(), "%d->%d ", i, CURT->fdtable_trans(i));
                    str += string(log_bufs[cpu_id].data());
                }
            }
        }
        if(invalid_fd) {
            str += " -> EFAULT";
        }
        else {
            str += " -> 0";
        }
        simroot::print_log_info(str);
    }
    
    if(invalid_fd) {
        IREG_V(a0) = -EFAULT;
        return pc + 4;
    }

    sch_lock.lock();
    auto iter = select_wait_threads.emplace(CURT, wt).first;
    sch_lock.unlock();
    pthread_create(&wt.th, nullptr, select_wait_thread_function, &(iter->second));

    CURT->save_context_stack(pc + 4, iregs, true);
    sch_lock.lock();
    bool ret = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
    sch_lock.unlock();
    if(ret) {
        simroot_assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
        cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
        return nextpc;
    }
    cpu_devs[cpu_id].cpu->halt();
    return 0;
}

MP_SYSCALL_DEFINE(1073, host_ppoll) {
    struct pollfd * hostfd = (struct pollfd *)HOST_ADDR_OF_IREG(a0);
    uint64_t nfds = IREG_V(a1);
    struct timespec * tmo = (struct timespec *)HOST_ADDR_OF_IREG(a2);
    sigset_t * sigmask = (sigset_t *)HOST_ADDR_OF_IREG(a3);

    if(sigmask) {
        CPUERROR("Un-implemented: ppoll with sigmask");
        simroot_assert(0);
    }
    
    PollWaitThread wt;
    wt.thread = CURT;
    wt.host_fds = (uint8_t*)hostfd;
    wt.nfds = nfds;
    wt.timeout_us = (tmo?(tmo->tv_nsec / 1000L + tmo->tv_sec * 1000000L):-1L);
    wt.sys = this;
    wt.cpuid = cpu_id;

    bool invalid_fd = false;
    if(log_syscall) {
        sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall host_ppoll: TMO %ldus, FDs: ", cpu_id, wt.timeout_us);
        string str = string(log_bufs[cpu_id].data());
        for(uint64_t i = 0; i < nfds; i++) {
            int simfd = hostfd[i].fd;
            hostfd[i].fd = CURT->fdtable_trans(simfd);
            if(hostfd[i].fd < 0) invalid_fd = true;
            sprintf(log_bufs[cpu_id].data(), "%d->%d %d, ", simfd, hostfd[i].fd, hostfd[i].events);
            str += string(log_bufs[cpu_id].data());
        }
        if(invalid_fd) {
            str += " -> EFAULT";
        }
        else {
            str += " -> 0";
        }
        simroot::print_log_info(str);
    }
    else {
        for(uint64_t i = 0; i < nfds; i++) {
            hostfd[i].fd = CURT->fdtable_trans(hostfd[i].fd);
            if(hostfd[i].fd < 0) invalid_fd = true;
        }
    }

    // LOG_SYSCALL_4("host_ppoll", "0x%lx", IREG_V(a0), "%ld", IREG_V(a1), "%ldus", wt.timeout_us, "0x%lx", IREG_V(a3), "%s", "xxx");

    if(invalid_fd) {
        IREG_V(a0) = -EFAULT;
        return pc + 4;
    }

    sch_lock.lock();
    auto iter = poll_wait_threads.emplace(CURT, wt).first;
    sch_lock.unlock();
    pthread_create(&wt.th, nullptr, poll_wait_thread_function, &(iter->second));

    CURT->save_context_stack(pc + 4, iregs, true);
    sch_lock.lock();
    bool ret = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
    sch_lock.unlock();
    if(ret) {
        simroot_assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
        cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
        return nextpc;
    }
    cpu_devs[cpu_id].cpu->halt();
    return 0;
}

MP_SYSCALL_DEFINE(1078, host_readlinkat) {
    int64_t ret = readlinkat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), (char*)HOST_ADDR_OF_IREG(a2), IREG_V(a3));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_4("host_readlinkat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "%s", (char*)HOST_ADDR_OF_IREG(a2), "%ld", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1079, host_newfstatat) {
    int64_t ret = fstatat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), (struct stat*)HOST_ADDR_OF_IREG(a2), IREG_V(a3));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_4("host_newfstatat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1080, host_fstat) {
    int64_t ret = fstat(CURT->fdtable_trans(IREG_V(a0)), (struct stat*)HOST_ADDR_OF_IREG(a1));
    if(ret < 0) ret = -errno;
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
        simroot_assert(thread->va2pa(thread->clear_child_tid, &paddr, 0) == SimError::success);
        auto res = futex_wait_threads.find(paddr);
        if(res != futex_wait_threads.end()) {
            auto &tl = res->second;
            for(auto &fwt : tl) {
                fwt.thread->context_stack.back().regs[RV_REG_a0] = 0;
                insert_ready_thread_nolock(fwt.thread, fwt.last_cpu_id);
            }
            futex_wait_threads.erase(res);
        }
    }
    if(switch_next_thread_nolock(cpu_id, SWFLAG_EXIT)) {
        sch_lock.unlock();
        simroot_assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
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
    simroot_assert(thread->va2pa(pargs->uaddr, &paddr, 0) == SimError::success);
    if(pargs->uaddr2) simroot_assert(thread->va2pa(pargs->uaddr2, &paddr2, 0) == SimError::success);

    uint32_t futex_op = pargs->futex_op;
    uint32_t futex_flag = (futex_op >> 8);
    futex_op &= 127;

    if(futex_op == FUTEX_WAIT || futex_op == FUTEX_WAIT_BITSET) {
        if(pargs->fval != pargs->val) {
            IREG_V(a0) = -EAGAIN;
            return pc + 4;
        }
        CPULOG("CPU %d Futex Wait", cpu_id);
        thread->save_context_stack(pc + 4, iregs, true);
        sch_lock.lock();
        futex_wait_thread_insert(paddr, thread, (futex_op == FUTEX_WAIT_BITSET)?(pargs->val3):0, cpu_id);
        bool ret = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
        sch_lock.unlock();
        if(ret) {
            simroot_assert(cpu_devs[cpu_id].exec_thread);
            RVRegArray regs;
            VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
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
    else if(futex_op == FUTEX_REQUEUE) {
        LOG(ERROR) << "Unknown futex op " << futex_op;
        simroot_assert(0);
    }
    else if(futex_op == FUTEX_CMP_REQUEUE) {
        LOG(ERROR) << "Unknown futex op " << futex_op;
        simroot_assert(0);
    }
    else if(futex_op == FUTEX_WAKE_OP) {
        LOG(ERROR) << "Unknown futex op " << futex_op;
        simroot_assert(0);
    }
    else if(futex_op == FUTEX_LOCK_PI) {
        LOG(ERROR) << "Unknown futex op " << futex_op;
        simroot_assert(0);
    }
    else if(futex_op == FUTEX_UNLOCK_PI) {
        LOG(ERROR) << "Unknown futex op " << futex_op;
        simroot_assert(0);
    }
    LOG(ERROR) << "Unknown futex op " << futex_op;
    simroot_assert(0);
    return 0;
}

MP_SYSCALL_DEFINE(1115, host_clock_nanosleep) {
    struct timespec* time = (struct timespec*)HOST_ADDR_OF_IREG(a2);
    double simtime2realtime = ((double)(simroot::get_global_freq()) / (double)(simroot::get_sim_tick_per_real_sec()));
    struct timespec ht;
    ht.tv_nsec = time->tv_nsec * simtime2realtime;
    ht.tv_sec = time->tv_sec * simtime2realtime;
    ht.tv_sec += (ht.tv_nsec / 1000000000UL);
    ht.tv_nsec = (ht.tv_nsec % 1000000000UL);
    
    SleepWaitThread wt;
    wt.thread = CURT;
    wt.host_time = ht;
    wt.sys = this;
    wt.cpuid = cpu_id;

    sch_lock.lock();
    auto iter = sleep_wait_threads.emplace(CURT, wt).first;
    sch_lock.unlock();
    pthread_create(&wt.th, nullptr, sleep_wait_thread_function, &(iter->second));

    CURT->save_context_stack(pc + 4, iregs, true);
    sch_lock.lock();
    bool ret = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
    sch_lock.unlock();
    if(ret) {
        simroot_assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
        cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
        return nextpc;
    }
    cpu_devs[cpu_id].cpu->halt();
    return 0;
}

MP_SYSCALL_DEFINE(1113, host_clock_gettime) {
    struct timespec* time = (struct timespec*)HOST_ADDR_OF_IREG(a1);
    uint64_t time_us = 0;
    int64_t ret = 0;
    switch (IREG_V(a0))
    {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        time_us = simroot::get_sim_time_us();
        break;
    case CLOCK_MONOTONIC:
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
    case CLOCK_MONOTONIC_RAW:
        time_us = simroot::get_current_tick() / (simroot::get_global_freq() / 1000000UL);
        break;
    default:
        LOG(ERROR) << "Unknown clock type: " << IREG_V(a0);
        simroot_assert(0);
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
    thread->sys_sigprocmask(IREG_V(a0), HOST_ADDR_OF_IREG(a1), HOST_ADDR_OF_IREG(a2), IREG_V(a3));
    LOG_SYSCALL_4("host_sigprocmask", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", IREG_V(a3), "%ld", 0UL);
    IREG_V(a0) = 0;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1199, host_socketpair) {
    int32_t fds[2];
    int64_t ret = socketpair(IREG_V(a0), IREG_V(a1), IREG_V(a2), fds);
    if(ret < 0) ret = -errno;
    int32_t *simfds = (int32_t*)HOST_ADDR_OF_IREG(a3);
    simfds[0] = CURT->fdtable_insert(fds[0]);
    simfds[1] = CURT->fdtable_insert(fds[1]);
    LOG_SYSCALL_5("host_socketpair", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "->%d", simfds[0], "->%d", simfds[1], "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1200, host_bind) {
    int64_t ret = bind(CURT->fdtable_trans(IREG_V(a0)), (struct sockaddr *)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_3("host_bind", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1203, host_connect) {
    int64_t ret = connect(CURT->fdtable_trans(IREG_V(a0)), (struct sockaddr *)HOST_ADDR_OF_IREG(a1), IREG_V(a2));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_3("host_connect", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1204, host_getsockname) {
    int64_t ret = getsockname(CURT->fdtable_trans(IREG_V(a0)), (struct sockaddr *)HOST_ADDR_OF_IREG(a1), (uint32_t*)HOST_ADDR_OF_IREG(a2));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_3("host_getsockname", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "%d", *(uint32_t*)HOST_ADDR_OF_IREG(a2), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1206, host_sendto) {
    int32_t simfd = IREG_V(a0);
    int32_t hostfd = CURT->fdtable_trans(simfd);
    if(hostfd < 0) {
        IREG_V(a0) = -EBADF;
        return pc + 4;
    }
    SockSendWaitThread wt;
    wt.thread = CURT;
    wt.sys = this;
    wt.cpuid = cpu_id;
    wt.simfd = simfd;
    wt.hostfd = hostfd;
    wt.buf = (uint8_t*)HOST_ADDR_OF_IREG(a1);
    wt.bufsz = IREG_V(a2);
    wt.flags = IREG_V(a3);
    wt.dest_addr = (uint8_t*)HOST_ADDR_OF_IREG(a4);
    wt.addrlen = IREG_V(a5);
    
    LOG_SYSCALL_6("host_sendto", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "%ld", IREG_V(a2), "0x%lx", IREG_V(a3), "0x%lx", IREG_V(a4), "%ld", IREG_V(a5), "%s", "blocked");

    sch_lock.lock();
    auto iter = socksend_wait_threads.emplace(CURT, wt).first;
    sch_lock.unlock();
    pthread_create(&wt.th, nullptr, socksend_wait_thread_function, &(iter->second));

    CURT->save_context_stack(pc + 4, iregs, true);
    sch_lock.lock();
    bool ret = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
    sch_lock.unlock();
    if(ret) {
        simroot_assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
        cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
        return nextpc;
    }
    cpu_devs[cpu_id].cpu->halt();
    return 0;
}

MP_SYSCALL_DEFINE(1208, host_setsockopt) {
    uint64_t optlen = IREG_V(a4);
    int64_t ret = setsockopt(
        CURT->fdtable_trans(IREG_V(a0)), IREG_V(a1), IREG_V(a2), optlen?(HOST_ADDR_OF_IREG(a3)):((void*)IREG_V(a3)), optlen
    );
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_5("host_setsockopt", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", IREG_V(a4), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1209, host_getsockopt) {
    int64_t ret = getsockopt(
        CURT->fdtable_trans(IREG_V(a0)), IREG_V(a1), IREG_V(a2), HOST_ADDR_OF_IREG(a3), (uint32_t*)(HOST_ADDR_OF_IREG(a4))
    );
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_5("host_getsockopt", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%d", *(uint32_t*)(HOST_ADDR_OF_IREG(a4)), "%ld", ret);
    IREG_V(a0) = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1212, host_recvmsg) {
    int32_t simfd = IREG_V(a0);
    int32_t hostfd = CURT->fdtable_trans(simfd);
    if(hostfd < 0) {
        IREG_V(a0) = -EBADF;
        return pc + 4;
    }
    SockRecvWaitThread wt;
    wt.thread = CURT;
    wt.sys = this;
    wt.cpuid = cpu_id;
    wt.simfd = simfd;
    wt.hostfd = hostfd;
    wt.hbuf_msg = ((struct msghdr*)HOST_ADDR_OF_IREG(a1));
    wt.msg = *(wt.hbuf_msg);
    wt.flags = IREG_V(a2);

    if(wt.msg.msg_name) wt.msg.msg_name = syscall_memory->get_host_addr((uint64_t)wt.msg.msg_name);
    if(wt.msg.msg_control) wt.msg.msg_control = syscall_memory->get_host_addr((uint64_t)wt.msg.msg_control);
    struct iovec * host_iovecs = (struct iovec *)syscall_memory->get_host_addr((uint64_t)wt.msg.msg_iov);
    wt.iovecs.resize(wt.msg.msg_iovlen);
    for(uint64_t i = 0; i < wt.msg.msg_iovlen; i++) {
        wt.iovecs[i].iov_len = host_iovecs[i].iov_len;
        wt.iovecs[i].iov_base = syscall_memory->get_host_addr((uint64_t)host_iovecs[i].iov_base);
    }
    wt.msg.msg_iov = wt.iovecs.data();
    
    LOG_SYSCALL_3("host_recvmsg", "%ld", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "%s", "blocked");

    sch_lock.lock();
    auto iter = sockrecv_wait_threads.emplace(CURT, wt).first;
    iter->second.msg.msg_iov = iter->second.iovecs.data();
    sch_lock.unlock();
    pthread_create(&wt.th, nullptr, sockrecv_wait_thread_function, &(iter->second));

    CURT->save_context_stack(pc + 4, iregs, true);
    sch_lock.lock();
    bool ret = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
    sch_lock.unlock();
    if(ret) {
        simroot_assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
        cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
        return nextpc;
    }
    cpu_devs[cpu_id].cpu->halt();
    return 0;
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
    newregs[isa::ireg_index_of("tp")] = (tls?tls:IREG_V(tp));
    newregs[isa::ireg_index_of("sp")] = (newsp?newsp:IREG_V(sp));
    newregs[RV_REG_a0] = 0;
    newthread->save_context_stack(pc + 4, newregs, true);
    for(auto &c : newthread->context_stack) {
        c.regs[isa::ireg_index_of("tp")] = newregs[isa::ireg_index_of("tp")];
        c.regs[isa::ireg_index_of("sp")] = newregs[isa::ireg_index_of("sp")];
    }

    insert_ready_thread_nolock(newthread, cpu_id);
    
    sch_lock.unlock();

    LOG_SYSCALL_5("host_clone", "0x%lx", IREG_V(a0), "0x%lx", IREG_V(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "0x%lx", IREG_V(a4), "%ld", ret);
    
    iregs[RV_REG_a0] = ret;
    return pc + 4;
}
// unsigned long, clone_flags, unsigned long, newsp,
// 		 int __user *, parent_tidptr,
// 		 unsigned long, tls,
// 		 int __user *, child_tidptr

MP_SYSCALL_DEFINE(1221, host_execve) {
    char *pathname = (char*)HOST_ADDR_OF_IREG(a0);
    char **argv = (char**)HOST_ADDR_OF_IREG(a1);
    char **envp = (char**)HOST_ADDR_OF_IREG(a2);

    SimWorkload workload;
    workload.file_path = string(pathname);
    workload.ldpaths = def_ldpaths;
    workload.stack_size = def_stacksz;
    workload.syscall_proxy_lib_path = def_syscall_proxy_path;

    for(char **p = argv; *p != nullptr; p++) {
        workload.argv.emplace_back(string((char*)syscall_memory->get_host_addr((uint64_t)(*p))));
    }
    for(char **p = envp; *p != nullptr; p++) {
        workload.envs.emplace_back(string((char*)syscall_memory->get_host_addr((uint64_t)(*p))));
    }

    if(log_syscall) {
        sprintf(log_bufs[cpu_id].data(), "CPU %d Syscall host_execve: ", cpu_id);
        string fullargv = string(log_bufs[cpu_id].data());
        for(auto &s : workload.argv) {
            fullargv += (", " + s);
        }
        simroot::print_log_info(fullargv);
    }

    uint64_t entry = 0, sp = 0;
    list<MemPagesToLoad> ldpg;
    CURT->elf_exec(workload, &ldpg, &entry, &sp);

    auto &context = CURT->context_stack.back();
    context.recover_a0 = true;
    isa::zero_regs(context.regs);
    context.regs[isa::ireg_index_of("sp")] = sp;
    context.regs[0] = entry;

    CURT->save_context_stack(pc + 4, iregs, true);

    simroot_assertf(!ldpg.empty(), "No Segment Loaded in execve");

    DMAWaitThread waitthread;
    waitthread.thread = CURT;
    waitthread.last_cpu_id = cpu_id;
    list<DMARequestUnit> reqs;
    for(MemPagesToLoad &pg : ldpg) {
        simroot_assert(pg.data.size() >= PAGE_LEN_BYTE * pg.vpcnt);
        for(uint64_t i = 0; i < pg.vpcnt; i++) {
            uint8_t *buf = new uint8_t[PAGE_LEN_BYTE];
            waitthread.to_free.push_back(buf);
            memcpy(buf, pg.data.data() + (PAGE_LEN_BYTE * i), PAGE_LEN_BYTE);
            PhysAddrT paddr = 0;
            simroot_assert(CURT->va2pa((pg.vpi + i) << PAGE_ADDR_OFFSET, &paddr, 0) == SimError::success);
            reqs.emplace_back(DMARequestUnit{
                .src = (PhysAddrT)buf,
                .dst = paddr,
                .size = PAGE_LEN_BYTE,
                .flag = DMAFLG_SRC_HOST,
                .callback = (uint64_t)(CURT)
            });
        }
    }
    waitthread.ref_cnt = reqs.size();
    dma->push_dma_requests(reqs);

    sch_lock.lock();
    dma_wait_threads.emplace(CURT, waitthread);
    bool nextthread = switch_next_thread_nolock(cpu_id, SWFLAG_WAIT);
    sch_lock.unlock();
    if(nextthread) {
        simroot_assert(cpu_devs[cpu_id].exec_thread);
        RVRegArray regs;
        VirtAddrT nextpc = cpu_devs[cpu_id].exec_thread->recover_context_stack(regs);
        cpu_devs[cpu_id].cpu->redirect(nextpc, regs);
        return nextpc;
    }
    cpu_devs[cpu_id].cpu->halt();
    return 0;

}

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

MP_SYSCALL_DEFINE(1435, host_clone3) {
    struct clone_args * args = (struct clone_args *)HOST_ADDR_OF_IREG(a0);

    uint64_t clone_flags = args->flags;
    VirtAddrT newsp = args->stack + args->stack_size;
    VirtAddrT parent_tidptr = args->parent_tid;
    VirtAddrT tls = args->tls;
    VirtAddrT child_tidptr = args->child_tid;
    RVThread *thread = cpu_devs[cpu_id].exec_thread;

    sch_lock.lock();

    VirtAddrT ret = alloc_tid();
    RVThread *newthread = new RVThread(thread, ret, clone_flags);
    newthread->clear_child_tid = child_tidptr;

    RVRegArray newregs;
    memcpy(newregs.data(), iregs.data(), iregs.size() * sizeof(uint64_t));
    newregs[isa::ireg_index_of("tp")] = (tls?tls:IREG_V(tp));
    newregs[isa::ireg_index_of("sp")] = (newsp?newsp:IREG_V(sp));
    newregs[RV_REG_a0] = 0;
    newthread->save_context_stack(pc + 4, newregs, true);
    for(auto &c : newthread->context_stack) {
        c.regs[isa::ireg_index_of("tp")] = newregs[isa::ireg_index_of("tp")];
        c.regs[isa::ireg_index_of("sp")] = newregs[isa::ireg_index_of("sp")];
    }


    insert_ready_thread_nolock(newthread, cpu_id);
    
    sch_lock.unlock();

    LOG_SYSCALL_5("host_clone3", "0x%lx", clone_flags, "0x%lx", newsp, "0x%lx", parent_tidptr, "0x%lx", tls, "0x%lx", child_tidptr, "%ld", ret);
    
    iregs[RV_REG_a0] = ret;
    return pc + 4;
}

MP_SYSCALL_DEFINE(1439, host_faccessat2) {
    int64_t ret = faccessat(CURT->fdtable_trans(IREG_V(a0)), (char*)HOST_ADDR_OF_IREG(a1), IREG_V(a2), IREG_V(a3));
    if(ret < 0) ret = -errno;
    LOG_SYSCALL_4("host_faccessat", "%ld", IREG_V(a0), "%s", (char*)HOST_ADDR_OF_IREG(a1), "0x%lx", IREG_V(a2), "0x%lx", IREG_V(a3), "%ld", ret);
    IREG_V(a0) = ret;
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

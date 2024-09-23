
#include "rvthread.h"
#include "simroot.h"
#include "configuration.h"

#include <elfio/elfio.hpp>

#include <assert.h>

#include <filesystem>

#include <sys/auxv.h>
#include <sys/mman.h>
#include <fcntl.h>

RVThread::RVThread(SimWorkload workload, PhysPageAllocator *pmman, VirtAddrT *out_entry, VirtAddrT *out_sp): pmman(pmman) {

    this->pgtable = std::make_shared<ThreadPageTable>(pmman);
    this->sig_actions = std::make_shared<std::unordered_map<int32_t, RVKernelSigaction>>();
    this->sig_proc_mask = std::make_shared<sigset_t>();
    memset(sig_proc_mask.get(), 0, sizeof(sigset_t));
    this->shared_lock = std::make_shared<SpinRWLock>();
    this->shared_lock->wait_interval = 32;
    this->fdtable = std::make_shared<FDTable>();
    this->fdtable->tb.emplace(0, 0);
    this->fdtable->tb.emplace(1, 1);
    this->fdtable->tb.emplace(2, 2);
    this->fdtable->alloc_fd = 3;
    
    // Init attribution
    pid = tid = DEFAULT_PID;
    memset(rlimit_values, 0, sizeof(rlimit_values));
    rlimit_values[RLIMIT_STACK].rlim_cur = workload.stack_size;
    rlimit_values[RLIMIT_STACK].rlim_max = ALIGN(workload.stack_size, PAGE_LEN_BYTE);
    
    std::list<MemPagesToLoad> vpgs_list;
    elf_exec(workload, &vpgs_list, out_entry, out_sp);

    for(auto &vpgs : vpgs_list) {
        pgtable->pgcpy_hostmem_to_va(vpgs.vpi, vpgs.vpcnt, vpgs.data.data());
    }
}


RVThread::RVThread(RVThread *parent_thread, uint64_t newtid, uint64_t fork_flag, list<simcache::DMARequestUnit> *out_dma) {
    // if(fork_flag == CLONEFLG_DEFAULT_THREAD) { // 0x3d0f00
    //     parent = parent_thread;
    //     parent->childs.push_back(this);

    //     // shared_ptr
    //     this->shared_lock = parent->shared_lock;
    //     this->pgtable = parent->pgtable; 
    //     this->sig_actions = parent->sig_actions; 
    //     this->sig_proc_mask = parent->sig_proc_mask; 
    //     this->fdtable = parent->fdtable; 

    //     // context
    //     this->tid = newtid;
    //     this->pid = parent_thread->pid;
    //     memcpy(this->context_host_ecall, parent->context_host_ecall, sizeof(this->context_host_ecall));
    //     memcpy(this->context_switch, parent->context_switch, sizeof(this->context_switch));

    //     this->set_child_tid = parent->set_child_tid;
    //     this->clear_child_tid = parent->clear_child_tid;
    //     this->robust_list_head = parent->robust_list_head;
    //     this->robust_list_len = parent->robust_list_len;
    // }
    // else {
    //     LOG(ERROR) << "Unknown fork flag " << fork_flag;
    //     assert(0);
    // }
    // 1200011

    
    this->tid = newtid;
    this->pid = parent_thread->pid;
    memcpy(this->context_host_ecall, parent_thread->context_host_ecall, sizeof(this->context_host_ecall));
    memcpy(this->context_switch, parent_thread->context_switch, sizeof(this->context_switch));

    this->pmman = parent_thread->pmman;

    this->set_child_tid = parent_thread->set_child_tid;
    this->clear_child_tid = parent_thread->clear_child_tid;
    this->robust_list_head = parent_thread->robust_list_head;
    this->robust_list_len = parent_thread->robust_list_len;

    if(fork_flag & CLONE_PARENT) {
        parent = parent_thread->parent;
        if(parent) parent->childs.push_back(this);
    }
    else {
        parent = parent_thread;
        parent->childs.push_back(this);
    }

    do_child_cleartid = (fork_flag & CLONE_CHILD_CLEARTID);

    simroot_assertf(!(fork_flag & CLONE_IO), "Unimplemented Fork Flag: CLONE_IO");
    simroot_assertf(!(fork_flag & CLONE_NEWCGROUP), "Unimplemented Fork Flag: CLONE_NEWCGROUP");
    simroot_assertf(!(fork_flag & CLONE_NEWIPC), "Unimplemented Fork Flag: CLONE_NEWIPC");
    simroot_assertf(!(fork_flag & CLONE_NEWNET), "Unimplemented Fork Flag: CLONE_NEWNET");
    simroot_assertf(!(fork_flag & CLONE_NEWNS), "Unimplemented Fork Flag: CLONE_NEWNS");
    simroot_assertf(!(fork_flag & CLONE_NEWPID), "Unimplemented Fork Flag: CLONE_NEWPID");
    simroot_assertf(!(fork_flag & CLONE_NEWUSER), "Unimplemented Fork Flag: CLONE_NEWUSER");
    simroot_assertf(!(fork_flag & CLONE_NEWUTS), "Unimplemented Fork Flag: CLONE_NEWUTS");
    simroot_assertf(!(fork_flag & CLONE_PIDFD), "Unimplemented Fork Flag: CLONE_PIDFD");

    // simroot_assertf((fork_flag & CLONE_FS), "Unimplemented Fork Flag: CLONE_FS");

    if(fork_flag & CLONE_SIGHAND) {
        this->sig_actions = parent_thread->sig_actions;
        this->sig_proc_mask = parent_thread->sig_proc_mask;
    }
    else {
        this->sig_actions = std::make_shared<std::unordered_map<int32_t, RVKernelSigaction>>();
        this->sig_proc_mask = std::make_shared<sigset_t>();
    }

    if(fork_flag & CLONE_FILES) {
        this->fdtable = parent_thread->fdtable; 
    }
    else {
        this->fdtable = std::make_shared<FDTable>();
        this->fdtable->tb.emplace(0, 0);
        this->fdtable->tb.emplace(1, 1);
        this->fdtable->tb.emplace(2, 2);
        for(auto &entry : parent_thread->fdtable->tb) {
            int32_t sim_fd = entry.first;
            int32_t host_fd = entry.second;
            if(sim_fd < 3) continue;
            this->fdtable->tb.emplace(sim_fd, dup(host_fd));
        }
        this->fdtable->alloc_fd = parent_thread->fdtable->alloc_fd;
    }

    if(fork_flag & CLONE_VM) {
        this->pgtable = parent_thread->pgtable; 
    }
    else {
        this->pgtable = std::make_shared<ThreadPageTable>(pmman);
        auto parent_pgtable = parent_thread->pgtable;

        this->pgtable->brk_va = parent_pgtable->brk_va;
        this->pgtable->dyn_lib_brk = parent_pgtable->dyn_lib_brk;
        this->pgtable->mmap_segments = parent_pgtable->mmap_segments;
        this->pgtable->pgtable = parent_pgtable->pgtable;

        for(auto &entry : this->pgtable->pgtable) {
            VPageIndexT vpi = entry.first;
            PPageEntry &pg = entry.second;
            PageIndexT ppi = pg.ppi;
            if((pg.flg & PGFLAG_W) && !(pg.flg & PGFLAG_SHARE)) {
                PageIndexT newppi = this->pgtable->ppman->alloc();
                pg.ppi = newppi;
                out_dma->emplace_back(simcache::DMARequestUnit{
                    .src = (ppi << PAGE_ADDR_OFFSET),
                    .dst = (newppi << PAGE_ADDR_OFFSET),
                    .size = PAGE_LEN_BYTE,
                    .flag = 0,
                    .callback = 0
                });
            }
            else {
                this->pgtable->ppman->reuse(pg.ppi);
            }
        }

    }

    if((fork_flag & CLONE_SIGHAND) || (fork_flag & CLONE_FILES) || (fork_flag & CLONE_VM)) {
        this->shared_lock = parent_thread->shared_lock;
    }
    else {
        this->shared_lock = std::make_shared<SpinRWLock>();
        this->shared_lock->wait_interval = 32;
    }

}


PageFlagT elf_pf_to_pgflg(uint32_t psfg) {
    PageFlagT flag = 0;
    if(psfg & PF_R) flag |= PGFLAG_R;
    if(psfg & PF_W) flag |= PGFLAG_W;
    if(psfg & PF_X) flag |= PGFLAG_X;
    return flag;
}

VirtAddrT RVThread::elf_load_dyn_lib(string elfpath, std::list<MemPagesToLoad> *output, VirtAddrT *out_entry) {
    char log_buf[256];
    ELFIO::elfio reader;
    if(!reader.load(elfpath)) {
        LOG(ERROR) << "Fail to load ELF file: " << elfpath;
        exit(0);
    }
    if (reader.get_class() == ELFCLASS32 || reader.get_encoding() != ELFDATA2LSB || reader.get_machine() != EM_RISCV) {
        LOG(ERROR) << "Only Support RV64 Little Endian: " << elfpath;
        exit(0);
    }
    VirtAddrT entry = reader.get_entry();

    int64_t max_addr = 0UL, min_addr = 0x7fffffffffffffffUL;
    ELFIO::Elf_Half seg_num = reader.segments.size();
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];
        if(pseg->get_type() == PT_LOAD) {
            int64_t memsz = pseg->get_memory_size();
            int64_t seg_addr = pseg->get_virtual_address();
            min_addr = std::min(min_addr, seg_addr);
            max_addr = std::max(max_addr, seg_addr + memsz);
        }
    }
    if(max_addr == 0) {
        LOG(ERROR) << "No Segment Loaded: " << elfpath;
        exit(0);
    }
    if(min_addr != 0) {
        LOG(ERROR) << "Dynamic lib must start with 0 VA: " << elfpath;
        exit(0);
    }

    VirtAddrT load_addr = pgtable->dyn_lib_brk;
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];
        if(pseg->get_type() != PT_LOAD || pseg->get_memory_size() == 0) continue;

        uint64_t filesz = pseg->get_file_size();
        uint64_t memsz = pseg->get_memory_size();
        VirtAddrT seg_addr = pseg->get_virtual_address() + load_addr;
        uint8_t *seg_data = (uint8_t*)(pseg->get_data());
        PageFlagT flag = elf_pf_to_pgflg(pseg->get_flags());

        sprintf(log_buf, "Load %s ELF Segment @0x%lx, len 0x%lx / 0x%lx", elfpath.c_str(), seg_addr, filesz, memsz);
        simroot::print_log_info(log_buf);

        pgtable->init_dynlib_seg(seg_addr, memsz, flag, elfpath);
        output->emplace_back();
        auto &vpgs = output->back();
        vpgs.vpi = (seg_addr >> PAGE_ADDR_OFFSET);
        vpgs.vpcnt = CEIL_DIV(seg_addr + memsz, PAGE_LEN_BYTE) - vpgs.vpi;
        vpgs.pgflg = flag;
        vpgs.data.assign(vpgs.vpcnt << PAGE_ADDR_OFFSET, 0);
        memcpy(vpgs.data.data() + (seg_addr & (PAGE_LEN_BYTE - 1)), pseg->get_data(), filesz);
    }

    if(out_entry) *out_entry = entry + load_addr;
    return load_addr;
}

void RVThread::elf_exec(SimWorkload &param, std::list<MemPagesToLoad> *out_vpgs, VirtAddrT *out_entry, VirtAddrT *out_sp) {
    char log_buf[256];
    ThreadPageTable *raw_pgtable = pgtable.get();

    // 清空线程页表
    for(auto &entry : raw_pgtable->pgtable) {
        raw_pgtable->ppman->free(entry.second.ppi);
    }
    raw_pgtable->pgtable.clear();
    raw_pgtable->brk_va = 0;
    raw_pgtable->mmap_segments.clear();
    raw_pgtable->dyn_lib_brk = MAX_MMAP_VADDR;

    // 加载elf文件
    ELFIO::elfio reader;
    if(!reader.load(param.file_path)) {
        LOG(ERROR) << "Fail to load ELF file: " << param.file_path;
        assert(0);
    }
    if(reader.get_class() == ELFCLASS32 || reader.get_encoding() != ELFDATA2LSB || reader.get_machine() != EM_RISCV) {
        LOG(ERROR) << "Only Support RV64 Little Endian: " << param.file_path;
        assert(0);
    }
    if(reader.get_type() != ET_DYN && reader.get_type() != ET_EXEC) {
        LOG(ERROR) << "Un-executable ELF file: " << param.file_path;
        assert(0);
    }

    uint32_t seg_num = reader.segments.size();
    VirtAddrT elf_entry = reader.get_entry();
    VirtAddrT interp_entry = 0;
    VirtAddrT phdr = 0;
    VirtAddrT elf_load_addr = 0;
    VirtAddrT interp_load_addr = 0;
    const char *interp_str = nullptr;

    // 设置PIT ELF的默认加载位置
    if(reader.get_type() == ET_DYN) {
        raw_pgtable->brk_va = elf_load_addr = MIN_VADDR;
        elf_entry += elf_load_addr;
    }

    // 查找有没有interp和代码段，设置phdr的虚拟地址
    bool has_text_seg = false;
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];
        if(pseg->get_type() == PT_INTERP) {
            interp_str = pseg->get_data();
        }
        if(pseg->get_type() == PT_LOAD && (pseg->get_flags() & PF_X)) {
            has_text_seg = true;
        }
        if(pseg->get_type() == PT_PHDR) {
            phdr = pseg->get_virtual_address() + elf_load_addr;
        }
    }
    if(!has_text_seg) {
        LOG(ERROR) << "No .TEXT Segment Loaded in ELF File: " << param.file_path;
        assert(0);
    }

    // 加载elf的PT_LOAD段
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];
        if(pseg->get_type() != PT_LOAD || pseg->get_memory_size() == 0) continue;

        uint64_t filesz = pseg->get_file_size();
        uint64_t memsz = pseg->get_memory_size();
        VirtAddrT seg_addr = pseg->get_virtual_address() + elf_load_addr;
        uint8_t *seg_data = (uint8_t*)(pseg->get_data());
        PageFlagT flag = elf_pf_to_pgflg(pseg->get_flags());

        sprintf(log_buf, "Load %s ELF Segment @0x%lx, len 0x%lx / 0x%lx", param.file_path.c_str(), seg_addr, filesz, memsz);
        simroot::print_log_info(log_buf);

        raw_pgtable->init_elf_seg(seg_addr, memsz, flag, param.file_path);
        out_vpgs->emplace_back();
        auto &vpgs = out_vpgs->back();
        vpgs.vpi = (seg_addr >> PAGE_ADDR_OFFSET);
        vpgs.vpcnt = CEIL_DIV(seg_addr + memsz, PAGE_LEN_BYTE) - vpgs.vpi;
        vpgs.pgflg = flag;
        vpgs.data.assign(vpgs.vpcnt << PAGE_ADDR_OFFSET, 0);
        memcpy(vpgs.data.data() + (seg_addr & (PAGE_LEN_BYTE - 1)), pseg->get_data(), filesz);
    }

    // 加载interp elf
    if(interp_str) {
        // if(reader.get_type() != ET_DYN) {
        //     LOG(ERROR) << "Un-supposed INTERP segment in a STATIC ELF file: " << param.file_path;
        //     assert(0);
        // }

        std::filesystem::path interp_path(interp_str);
        std::string interp_filename = interp_path.filename();
        std::string interp_realpath_str = "";
        bool interp_found = false;
        for(auto &s : param.ldpaths) {
            std::filesystem::path tmp(s);
            tmp /= interp_filename;
            if(std::filesystem::exists(tmp)) {
                interp_found = true;
                interp_realpath_str = tmp.string();
                break;
            }
        }
        if(!interp_found) {
            sprintf(log_buf, "Cannot find INTERP:%s", interp_filename.c_str());
            LOG(ERROR) << log_buf;
            assert(0);
        }
        sprintf(log_buf, "Find INTERP:%s -> %s", interp_str, interp_realpath_str.c_str());
        simroot::print_log_info(log_buf);

        interp_load_addr = elf_load_dyn_lib(interp_realpath_str, out_vpgs, &interp_entry);

        // param.argv.insert(param.argv.begin(), interp_realpath_str);
    }

    // 构造初始程序栈
    bool log_stack = conf::get_int("sys", "log_print_init_stack_layout", 0);
    uint64_t stsz = ALIGN(param.stack_size, PAGE_LEN_BYTE);
    VirtAddrT stva = raw_pgtable->alloc_mmap(stsz, PGFLAG_R | PGFLAG_W | PGFLAG_STACK, -1, 0, "stack");
    assert((stva & (PAGE_LEN_BYTE - 1)) == 0);
    VirtAddrT sttopva = stva + stsz;
    uint64_t sp = stsz;
    uint8_t *stbuf = new uint8_t[stsz];

    auto alloc_stack_align_8 = [&](uint64_t size) -> uint64_t {
        size = ALIGN(size, 8);
        if(sp <= size) {
            LOG(ERROR) << "Stack overflow: " << param.file_path;
            assert(0);
        }
        sp = sp - size;
        return stva + sp;
    };

    
    // Init stack bottom
    alloc_stack_align_8(16);
    memset(stbuf + sp, 0, 16);

    if(log_stack) printf("0x%lx: Stack bottom\n", stva + sp);

    // Init aux_vecs
    std::list<std::pair<uint64_t, uint64_t>> aux_vecs;
    aux_vecs.emplace_back(std::make_pair(AT_PHDR, phdr));
    aux_vecs.emplace_back(std::make_pair(AT_PHENT, reader.get_segment_entry_size()));
    aux_vecs.emplace_back(std::make_pair(AT_PHNUM, reader.segments.size()));
    aux_vecs.emplace_back(std::make_pair(AT_PAGESZ, PAGE_LEN_BYTE));
    aux_vecs.emplace_back(std::make_pair(AT_BASE, interp_str?interp_load_addr:elf_load_addr));
    aux_vecs.emplace_back(std::make_pair(AT_ENTRY, elf_entry));
    aux_vecs.emplace_back(std::make_pair(AT_UID, (uint64_t)getuid()));
    aux_vecs.emplace_back(std::make_pair(AT_EUID, (uint64_t)geteuid()));
    aux_vecs.emplace_back(std::make_pair(AT_GID, (uint64_t)getgid()));
    aux_vecs.emplace_back(std::make_pair(AT_EGID, (uint64_t)getegid()));
    aux_vecs.emplace_back(std::make_pair(AT_HWCAP, 0));
    aux_vecs.emplace_back(std::make_pair(AT_CLKTCK, simroot::get_wall_time_freq()));
    aux_vecs.emplace_back(std::make_pair(AT_RANDOM, alloc_stack_align_8(16)));
    for(int i = 0; i < 16; i++) stbuf[sp + i] = RAND(0,256);
    aux_vecs.emplace_back(std::make_pair(AT_SECURE, getauxval(AT_SECURE)));
    char * execfn = realpath(param.file_path.c_str(), nullptr);
    uint64_t execfn_len = strlen(execfn);
    aux_vecs.emplace_back(std::make_pair(AT_EXECFN, alloc_stack_align_8(execfn_len + 1)));
    strcpy((char*)stbuf + sp, execfn);
    free(execfn);
    // Init envs
    std::list<uint64_t> env_vaddrs;
    for(auto &s : param.envs) {
        env_vaddrs.push_back(alloc_stack_align_8(s.length() + 1));
        strcpy((char*)stbuf + sp, s.c_str());
    }
    // Init argvs
    std::list<uint64_t> argv_vaddrs;
    for(auto &s : param.argv) {
        argv_vaddrs.push_back(alloc_stack_align_8(s.length() + 1));
        strcpy((char*)stbuf + sp, s.c_str());
    }

    // Padding
    sp = (sp >> 4) << 4;

    // Init aux bottom
    if(log_stack) printf("0x%lx: Aux-vec Bottom\n", stva + sp);
    alloc_stack_align_8(16);
    memset(stbuf + sp, 0, 16);
    // Init aux
    while(!aux_vecs.empty()) {
        auto aux_entry = aux_vecs.back();
        aux_vecs.pop_back();
        alloc_stack_align_8(8);
        memcpy(stbuf + sp, &(aux_entry.second), 8);
        alloc_stack_align_8(8);
        memcpy(stbuf + sp, &(aux_entry.first), 8);
        if(log_stack) printf("0x%lx: Aux-vec %ld: 0x%lx\n", stva + sp, aux_entry.first, aux_entry.second);
    }
    if(log_stack) printf("0x%lx: Aux-vec Top\n", stva + sp);

    // Init env bottom
    if(log_stack) printf("0x%lx: Envs Bottom\n", stva + sp);
    alloc_stack_align_8(8);
    memset(stbuf + sp, 0, 8);
    // Init env
    while(!env_vaddrs.empty()) {
        uint64_t tmp = env_vaddrs.back();
        env_vaddrs.pop_back();
        alloc_stack_align_8(8);
        memcpy(stbuf + sp, &(tmp), 8);
        if(log_stack) printf("0x%lx: Env 0x%lx\n", stva + sp, tmp);
    }
    if(log_stack) printf("0x%lx: Envs Top\n", stva + sp);

    // Init argv_bottom
    if(log_stack) printf("0x%lx: Argvs Bottom\n", stva + sp);
    alloc_stack_align_8(8);
    memset(stbuf + sp, 0, 8);
    // Init argv
    uint64_t argc = argv_vaddrs.size();
    while(!argv_vaddrs.empty()) {
        uint64_t tmp = argv_vaddrs.back();
        argv_vaddrs.pop_back();
        alloc_stack_align_8(8);
        memcpy(stbuf + sp, &(tmp), 8);
        if(log_stack) printf("0x%lx: Argv 0x%lx\n", stva + sp, tmp);
    }
    if(log_stack) printf("0x%lx: Argvs Top\n", stva + sp);
    // Init argc
    alloc_stack_align_8(8);
    memcpy(stbuf + sp, &(argc), 8);

    if(log_stack) printf("0x%lx: Stack Point\n", stva + sp);

    assert(stsz > sp);

    out_vpgs->emplace_back();
    {
        auto &vpgs = out_vpgs->back();
        vpgs.vpi = (stva >> PAGE_ADDR_OFFSET) + (sp >> PAGE_ADDR_OFFSET);
        vpgs.vpcnt = (sttopva >> PAGE_ADDR_OFFSET) - vpgs.vpi;
        vpgs.pgflg = (PGFLAG_R | PGFLAG_W | PGFLAG_STACK);
        vpgs.data.assign(vpgs.vpcnt << PAGE_ADDR_OFFSET, 0);
        memcpy(vpgs.data.data(), stbuf + ((sp >> PAGE_ADDR_OFFSET) << PAGE_ADDR_OFFSET), vpgs.data.size());
    }
    delete[] stbuf;
    
    sprintf(log_buf, "Init Stack @0x%lx", stva + sp);
    simroot::print_log_info(log_buf);

    // 设置程序入口与初始sp
    if(out_entry) {
        *out_entry = (interp_str?interp_entry:elf_entry);
    }
    else {
        LOG(WARNING) << "Unused elf entry: " << param.file_path;
    }
    if(out_sp) {
        *out_sp = stva + sp;
    }
    else {
        LOG(WARNING) << "Unused init sp: " << param.file_path;
    }
}

int32_t RVThread::fdtable_trans(int32_t user_fd) {
    int32_t ret = user_fd;
    shared_lock->read_lock();
    auto res = fdtable->tb.find(user_fd);
    if(res != fdtable->tb.end()) ret = res->second;
    else if(user_fd > 0) ret = -1;
    shared_lock->read_unlock();
    return ret;
}

int32_t RVThread::fdtable_insert(int32_t sys_fd) {
    if(sys_fd < 3) return sys_fd;
    int32_t ret = 0;
    shared_lock->write_lock();
    if(fdtable->tb.size() > 0x1000000) {
        LOG(ERROR) << "FD Table Run Out !!!"; assert(0);
    }
    while(1) {
        ret = fdtable->alloc_fd;
        fdtable->alloc_fd = ((fdtable->alloc_fd + 1) & 0x3fffffff);
        if(fdtable->tb.find(ret) == fdtable->tb.end()) break;
    }
    fdtable->tb.emplace(ret, sys_fd);
    shared_lock->write_unlock();
    return ret;
}

int32_t RVThread::fdtable_pop(int32_t user_fd) {
    int32_t ret = user_fd;
    shared_lock->write_lock();
    auto res = fdtable->tb.find(user_fd);
    if(res != fdtable->tb.end()) {
        ret = res->second;
        fdtable->tb.erase(res);
    }
    else if(user_fd > 0) ret = -1;
    shared_lock->write_unlock();
    return ret;
}


VirtAddrT RVThread::sys_brk(VirtAddrT newbrk) {
    shared_lock->write_lock();
    VirtAddrT ret = pgtable->alloc_brk(newbrk);
    shared_lock->write_unlock();
    return ret;
}

VirtAddrT RVThread::sys_mmap(uint64_t length, uint64_t pgflag, int32_t fd, uint64_t offset, string info) {
    shared_lock->write_lock();
    VirtAddrT ret = pgtable->alloc_mmap(length, pgflag, fd, offset, info);
    shared_lock->write_unlock();
    return ret;
}

VirtAddrT RVThread::sys_mmap_fixed(VirtAddrT addr, uint64_t length, uint64_t pgflag, int32_t fd, uint64_t offset, string info) {
    shared_lock->write_lock();
    VirtAddrT ret = pgtable->alloc_mmap_fixed(addr, length, pgflag, fd, offset, info);
    shared_lock->write_unlock();
    return ret;
}


void RVThread::sys_munmap(VirtAddrT vaddr, uint64_t length) {
    // TODO
    shared_lock->write_lock();
    pgtable->free_mmap(vaddr, length);
    shared_lock->write_unlock();
}

void RVThread::sys_mprotect(VirtAddrT vaddr, uint64_t length, PageFlagT flag) {
    uint32_t set_mask = (PGFLAG_R | PGFLAG_W | PGFLAG_X);
    flag &= set_mask;
    VPageIndexT vpi1 = (vaddr >> PAGE_ADDR_OFFSET);
    VPageIndexT vpi2 = (ALIGN(vaddr + length, PAGE_LEN_BYTE) >> PAGE_ADDR_OFFSET);
    shared_lock->write_lock();
    for(VPageIndexT vpi = vpi1; vpi < vpi2; vpi++) {
        auto res = pgtable->pgtable.find(vpi);
        if(res != pgtable->pgtable.end()) {
            res->second.flg &= (~set_mask);
            res->second.flg |= flag;
        }
    }
    shared_lock->write_unlock();
}

void RVThread::sys_sigaction(int32_t signum, RVKernelSigaction *act, RVKernelSigaction *oldact) {
    shared_lock->write_lock();
    auto res = sig_actions->find(signum);
    if(oldact) {
        if(res == sig_actions->end()) {
            memset(oldact, 0, sizeof(RVKernelSigaction));
        }
        else {
            memcpy(oldact, &(res->second), sizeof(RVKernelSigaction));
        }
    }
    if(act) {
        if(res == sig_actions->end()) {
            sig_actions->emplace(signum, *act);
        }
        else {
            memcpy(&(res->second), act, sizeof(RVKernelSigaction));
        }
    }
    shared_lock->write_unlock();
}

void RVThread::sys_sigprocmask(uint32_t how, sigset_t *set, sigset_t *oldset) {
    shared_lock->write_lock();
    if(oldset) {
        memcpy(oldset, sig_proc_mask.get(), sizeof(sigset_t));
    }
    if(set) {
        if(how == SIG_BLOCK) {
            for(int sn = 0; sn < 64; sn++) {
                if(sigismember(set, sn) > 0) sigaddset(sig_proc_mask.get(), sn);
            }
        }
        else if(how == SIG_UNBLOCK) {
            for(int sn = 0; sn < 64; sn++) {
                if(sigismember(set, sn) > 0) sigdelset(sig_proc_mask.get(), sn);
            }
        }
        else if(how == SIG_SETMASK) {
            memcpy(sig_proc_mask.get(), set, sizeof(sigset_t));
        }
        else {
            LOG(ERROR) << "Unsupported args for syscall sigprocmask";
            assert(0);
        }
    }
    shared_lock->write_unlock();
}

void RVThread::sys_prlimit(uint64_t resource, rlimit* p_new, rlimit* p_old) {
    if(resource = RLIMIT_STACK) {
        if(p_old) {
            p_old->rlim_cur = rlimit_values[RLIMIT_STACK].rlim_cur;
            p_old->rlim_max = rlimit_values[RLIMIT_STACK].rlim_max;
        }
        if(p_new) {
            rlimit_values[RLIMIT_STACK].rlim_cur = p_new->rlim_cur;
            rlimit_values[RLIMIT_STACK].rlim_max = p_new->rlim_max;
            // pgtable->append_stack(newlimit);
        }
    }
}

void RVThread::save_context_host_ecall(VirtAddrT nextpc, RVRegArray &iregs) {
    context_host_ecall[0] = nextpc;
    uint64_t cnt = std::min<uint64_t>(RV_REG_CNT_INT, iregs.size());
    for(uint64_t i = 1; i < cnt; i++) {
        context_host_ecall[i] = iregs[i];
    }
}

VirtAddrT RVThread::recover_context_host_ecall(RVRegArray &out_iregs) {
    for(uint64_t i = 1; i < RV_REG_CNT_INT; i++) {
        if(i == isa::ireg_index_of("a0")) continue;
        out_iregs[i] =  context_host_ecall[i];
    }
    return context_host_ecall[0];
}

void RVThread::save_context_switch(VirtAddrT nextpc, RVRegArray &regs) {
    context_switch[0] = nextpc;
    uint64_t cnt = std::min<uint64_t>(RV_REG_CNT_INT + RV_REG_CNT_FP, regs.size());
    memcpy(context_switch + 1, regs.data() + 1, (cnt - 1) * sizeof(uint64_t));
}

VirtAddrT RVThread::recover_context_switch(RVRegArray &out_regs) {
    isa::zero_regs(out_regs);
    uint64_t cnt = std::min<uint64_t>(RV_REG_CNT_INT + RV_REG_CNT_FP, out_regs.size());
    memcpy(out_regs.data() + 1, context_switch + 1, (cnt - 1) * sizeof(uint64_t));
    return context_switch[0];
}



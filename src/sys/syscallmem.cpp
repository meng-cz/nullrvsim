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


#include <assert.h>

#include <elfio/elfio.hpp>

#include "syscallmem.h"

SyscallMemory::SyscallMemory(VirtAddrT base_addr, uint64_t size) {
    base = base_addr;
    sz = size;
    mem = new uint8_t[size];
    syscall_proxy_entrys = new uint64_t[max_syscall_index];
    memset(syscall_proxy_entrys, 0, max_syscall_index * sizeof(uint64_t));
    free_segs.emplace_back(std::make_pair(base_addr + 4096UL, size));
    memset(mem, 0, 4096);
}

SyscallMemory::~SyscallMemory() {
    if(mem) delete[] mem;
    if(syscall_proxy_entrys) delete[] syscall_proxy_entrys;
}


uint64_t SyscallMemory::load_syscall_proxy_lib(string path) {

    ELFIO::elfio reader;
    if(!reader.load(path)) {
        LOG(ERROR) << "Fail to load syscall proxy lib";
        exit(0);
    }
    if (reader.get_class() == ELFIO::ELFCLASS32 || reader.get_encoding() != ELFIO::ELFDATA2LSB || reader.get_machine() != ELFIO::EM_RISCV) {
        LOG(ERROR) << "Only Support RV64 Little Endian";
        exit(0);
    }
    
    uint64_t max_addr = 0UL, min_addr = 0x7fffffffffffffffUL;
    ELFIO::Elf_Half seg_num = reader.segments.size();
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];

        if(pseg->get_type() == ELFIO::PT_LOAD) {
            uint64_t filesz = pseg->get_file_size();
            uint64_t memsz = pseg->get_memory_size();
            uint64_t seg_addr = pseg->get_virtual_address();
            uint8_t *seg_data = (uint8_t*)(pseg->get_data());

            min_addr = std::min(min_addr, seg_addr);
            max_addr = std::max(max_addr, seg_addr + memsz);
        }
    }
    if(max_addr == 0) {
        sprintf(log_buf, "No Segment Loaded in Syscall Proxy Library");
        LOG(ERROR) << log_buf;
        exit(0);
    }
    assert(min_addr == 0);

    int64_t load_addr = smem_alloc(ALIGN(max_addr - min_addr, 4096));
    int64_t addr_offset = load_addr - min_addr;
    for (int i = 0; i < seg_num; ++i) {
        const ELFIO::segment *pseg = reader.segments[i];

        if(pseg->get_type() == ELFIO::PT_LOAD) {
            int64_t filesz = pseg->get_file_size();
            int64_t memsz = pseg->get_memory_size();
            int64_t seg_addr = pseg->get_virtual_address();

            memcpy(mem + (addr_offset + seg_addr - base), pseg->get_data(), filesz);
            memset(mem + (addr_offset + seg_addr + filesz - base), 0, memsz - filesz);
        }
    }

    ELFIO::Elf64_Rela *p_rela_dyn = nullptr;
    uint32_t num_rela_dyn = 0;
    ELFIO::Elf64_Rela *p_rela_plt = nullptr;
    uint32_t num_rela_plt = 0;
    ELFIO::Elf64_Sym *p_dynsym = nullptr;
    uint32_t num_dynsym = 0;
    char *p_dynstr = nullptr;

    ELFIO::Elf_Half sec_num = reader.sections.size();
    for(int i = 0; i < sec_num; i++) {
        const ELFIO::section *psec = reader.sections[i];
        if(psec->get_name().compare(".dynsym") == 0) {
            p_dynsym = (ELFIO::Elf64_Sym *)(mem + (psec->get_address() + addr_offset - base));
            assert(psec->get_entry_size() == sizeof(ELFIO::Elf64_Sym));
            num_dynsym = psec->get_size() / sizeof(ELFIO::Elf64_Sym);
        }
        else if(psec->get_name().compare(".dynstr") == 0) {
            p_dynstr = (char *)(mem + (psec->get_address() + addr_offset - base));
        }
        else if(psec->get_name().compare(".rela.dyn") == 0) {
            p_rela_dyn = (ELFIO::Elf64_Rela *)(mem + (psec->get_address() + addr_offset - base));
            assert(psec->get_entry_size() == sizeof(ELFIO::Elf64_Rela));
            num_rela_dyn = psec->get_size() / sizeof(ELFIO::Elf64_Rela);
        }
        else if(psec->get_name().compare(".rela.plt") == 0) {
            p_rela_plt = (ELFIO::Elf64_Rela *)(mem + (psec->get_address() + addr_offset - base));
            assert(psec->get_entry_size() == sizeof(ELFIO::Elf64_Rela));
            num_rela_plt = psec->get_size() / sizeof(ELFIO::Elf64_Rela);
        }
    }

    if(p_dynstr == nullptr || p_dynsym == nullptr) {
        sprintf(log_buf, "No Dynamic Symbols Found in Syscall Proxy Library");
        LOG(ERROR) << log_buf;
        exit(0);
    }

    typedef struct {
        string name;
        uint64_t addr = 0;
    } DynSym;

    std::vector<DynSym> syms;
    for(int i = 0; i < num_dynsym;i++) {
        syms.emplace_back();
        syms.back().name = string(p_dynstr + p_dynsym[i].st_name);
        syms.back().addr = p_dynsym[i].st_value + addr_offset;
    }
    for(int i = 0; i < num_rela_dyn; i++) {
        uint64_t sym_index = (p_rela_dyn[i].r_info) >> 32;
        uint64_t *got_entry = (uint64_t *)(mem + (p_rela_dyn[i].r_offset + addr_offset - base));
        *got_entry = syms[sym_index].addr;
    }
    for(int i = 0; i < num_rela_plt; i++) {
        uint64_t sym_index = (p_rela_plt[i].r_info) >> 32;
        uint64_t *got_entry = (uint64_t *)(mem + (p_rela_plt[i].r_offset + addr_offset - base));
        *got_entry = syms[sym_index].addr;
    }

    uint64_t proxy_func_cnt = 0;
    for(int i = 0; i < num_dynsym; i++) {
        uint64_t pos = syms[i].name.find("_");
        if(pos != string::npos && syms[i].name.substr(0, pos).compare("proxy") == 0) {
            uint64_t pos2 = syms[i].name.find("_", pos+1);
            if(pos != string::npos) {
                string sindex = syms[i].name.substr(pos+1, pos2 - pos - 1);
                if(std::all_of(begin(sindex), end(sindex), isdigit)) {
                    uint32_t index = atoi(sindex.c_str());
                    syscall_proxy_entrys[index] = syms[i].addr;
                    proxy_func_cnt++;
                }
            }
        }
    }

    return proxy_func_cnt;
}

VirtAddrT SyscallMemory::smem_alloc(uint64_t len) {
    uint64_t offset = 0;
    len = ALIGN(len, 64);
    bool succ = false;
    alloc_lock.lock();
    auto iter = free_segs.begin();
    for(; iter != free_segs.end(); iter++) {
        if(iter->second >= len) {
            offset = iter->first;
            alloced_segs.insert(std::make_pair(offset, len));
            if(iter->second == len) {
                free_segs.erase(iter);
            }
            else {
                iter->first += len;
                iter->second -= len;
            }
            succ = true;
            break;
        }
    }
    alloc_lock.unlock();
    if(!succ) {
        sprintf(log_buf, "Syscall Memory Run out");
        LOG(ERROR) << log_buf;
        exit(0);
    }
    return offset;
}

void SyscallMemory::smem_free(VirtAddrT _offset) {
    alloc_lock.lock();
    auto res = alloced_segs.find(_offset);
    if(res != alloced_segs.end()) {
        uint64_t len = res->second;
        uint64_t offset = res->first;
        alloced_segs.erase(res);
        auto iter = free_segs.begin();
        while(iter != free_segs.end()) {
            if(iter->first > offset) break;
            iter++;
        }
        free_segs.insert(iter, std::make_pair(offset, len));
        // debug_print_free_segs();
        std::list<std::pair<uint64_t, uint64_t>> tmp;
        while(! free_segs.empty()) {
            std::pair<uint64_t, uint64_t> item = free_segs.front();
            free_segs.pop_front();
            if(!tmp.empty() && tmp.back().first + tmp.back().second >= item.first) {
                tmp.back().second = std::max(tmp.back().second, item.first + item.second - tmp.back().first);
            }
            else {
                tmp.emplace_back(item);
            }
        }
        free_segs.swap(tmp);
        // debug_print_free_segs();
    }
    alloc_lock.unlock();
}

void SyscallMemory::debug_print_free_segs() {
    printf("Alloced: ");
    for(auto &s : alloced_segs) {
        printf("(0x%lx, 0x%lx) ", s.first, s.second);
    }
    printf("\n");
    printf("FreeSegs: ");
    for(auto &s : free_segs) {
        printf("(0x%lx, 0x%lx) ", s.first, s.second);
    }
    printf("\n");
}

namespace test {

bool test_syscall_memory() {
    SyscallMemory *smem = new SyscallMemory(0x10000000UL, 0x10000000UL);
    printf("Load %ld proxy functions\n", smem->load_syscall_proxy_lib("rvsrc/ecallproxy/libecallproxy.so"));
    for(int i = 0; i < 300; i++) {
        if(smem->get_syscall_proxy_entry(i)) {
            printf("    %3d -> %lx\n", i, smem->get_syscall_proxy_entry(i));
        }
    }

    std::vector<uint64_t> alloced;
    for(int i = 0; i < 20; i++) {
        if(RAND(0,2) == 1) {
            uint64_t sz = 4096 * RAND(1,10);
            uint64_t tmp = smem->smem_alloc(sz);
            alloced.push_back(tmp);
            printf("ALLOC: 0x%lx @0x%lx\n", sz, tmp);
        }
        else if(!alloced.empty()) {
            uint64_t sz = alloced.size();
            uint64_t index = RAND(0, sz);
            uint64_t tmp = alloced[index];
            printf(" FREE: @0x%lx\n", tmp);
            alloced.erase(alloced.begin() + index);
            smem->smem_free(tmp);
        }
        smem->debug_print_free_segs();
    }

    return true;
}

}


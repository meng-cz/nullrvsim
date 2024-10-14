#ifndef RVSIM_MEM_PAGE_MMAP_H
#define RVSIM_MEM_PAGE_MMAP_H

#include "common.h"
#include "spinlocks.h"
#include "simroot.h"

class PhysPageAllocator {

public:

    PhysPageAllocator(PhysAddrT start, uint64_t size, uint8_t *p_mem) : lock(64) {
        init_segment(start, size, p_mem);
    }

    inline void init_segment(PhysAddrT start, uint64_t size, uint8_t *p_mem) {
        simroot_assert((start & (PAGE_LEN_BYTE - 1)) == 0);
        simroot_assert((size & (PAGE_LEN_BYTE - 1)) == 0);
        for(PageAddrT ppa = start; ppa + PAGE_LEN_BYTE <= (start + size); ppa += PAGE_LEN_BYTE) {
            valid_pages.push_back(ppa >> PAGE_ADDR_OFFSET);
            page_real_addr.insert(std::make_pair(ppa >> PAGE_ADDR_OFFSET, p_mem + (ppa - start)));
        }
    }

    inline uint8_t * real_addr_of(PageIndexT ppindex) {
        auto res = page_real_addr.find(ppindex);
        simroot_assert(res != page_real_addr.end());
        return res->second;
    }

    inline PageIndexT alloc() {
        lock.lock();
        if(valid_pages.empty()) {
            sprintf(log_buf, "Physical memory run out!!!");
            LOG(ERROR) << log_buf;
            simroot_assert(0);
        }
        PageIndexT ret = valid_pages.front();
        valid_pages.pop_front();
        alloced_pages.insert(std::make_pair(ret, 1));
        lock.unlock();
        return ret;
    }

    inline void reuse(PageIndexT ppindex) {
        lock.lock();
        auto res = alloced_pages.find(ppindex);
        simroot_assert(res != alloced_pages.end());
        res->second ++;
        lock.unlock();
    }

    inline bool free(PageIndexT ppindex) {
        bool ret = true;
        lock.lock();
        auto res = alloced_pages.find(ppindex);
        simroot_assert(res != alloced_pages.end());
        if(res->second <= 1) {
            alloced_pages.erase(res);
        }
        else {
            res->second --;
            ret = false;
        }
        lock.unlock();
        return ret;
    }

    inline bool is_shared(PageIndexT ppindex) {
        lock.lock();
        auto res = alloced_pages.find(ppindex);
        bool ret = (res != alloced_pages.end() && (res->second) > 1);
        lock.unlock();
        return ret;
    }

    inline void debug_print_alloc_pages() {
        printf("Phys page allocator:\n");
        for(auto &p : alloced_pages) {
            printf("(0x%lx, %d), ", p.first, p.second);
        }
        printf("\n");
    }

protected:

    std::unordered_map<PageIndexT, uint8_t*> page_real_addr;

    SpinLock lock;
    std::list<PageIndexT> valid_pages;
    std::unordered_map<PageIndexT, uint32_t> alloced_pages;

    char log_buf[128];
};


// typedef struct {
//     VPageIndexT vpindex;
//     uint64_t    vpcnt;
//     VirtAddrT   start;
//     uint64_t    size;
//     PageFlagT   flag;
//     int32_t     fd;
//     uint64_t    offset;
//     string      info;
// } VaddrSegment;

typedef struct {
    PageIndexT  ppi;
    PageFlagT   flg;
    int32_t     fd;
    uint64_t    offset;
} PPageEntry;

typedef struct {
    VPageIndexT vpindex;
    uint64_t    vpcnt;
    string      info;
} VAddrSeg;

/**
 * For static elf
 * 0                                                                        MAX_MMAP_VADDR
 * | -- .text .rodata -- | -- .bss ... -- | ... heap ... --->  <---  ... mmap ... |
 * | ---------- Load from ELF ----------- | - alloc by brk ->  <- alloc by mmap - |
*/

/**
 * For dynamic elf
 * MIN_VADDR                                                            MAX_MMAP_VADDR    MAX_VADDR
 * | -- .text .rodata -- | -- .bss ... -- | ... heap ... --->  <---  ... mmap ... |   ld.so   |
 * | ---------- Load from ELF ----------- | - alloc by brk ->  <- alloc by mmap - |           |
*/

#define MIN_VADDR (0x100000000000UL)
#define MAX_MMAP_VADDR (0xb00000000000UL)
#define MAX_VADDR (0xc00000000000UL)

class ThreadPageTable {

public:

    ThreadPageTable(PhysPageAllocator *ppman) : ppman(ppman) {} ;
    ~ThreadPageTable() {
        for(auto &entry : pgtable) {
            ppman->free(entry.second.ppi);
        }
    }

    PhysPageAllocator *ppman;

    VirtAddrT brk_va = 0;
    std::list<VAddrSeg> mmap_segments; // Ordered from high addr to low addr

    std::unordered_map<VPageIndexT, PPageEntry> pgtable;

    inline void __alloc_multi_page(VPageIndexT vpindex, uint64_t vpcnt, PageFlagT flag, int32_t fd, uint64_t offset) {
        for(VPageIndexT vpi = vpindex; vpi < vpindex + vpcnt; vpi++) {
            if(pgtable.find(vpi) != pgtable.end()) {
                LOG(ERROR) << "Virt Addr Space Re-Allocated";
                simroot_assert(0);
            }
            PageIndexT ppi = ppman->alloc();
            pgtable.emplace(vpi, PPageEntry{
                .ppi = ppi, .flg = flag, .fd = fd, .offset = offset
            });
            if(fd > 0) {
                offset += PAGE_LEN_BYTE;
            }
        }
    }

    inline void debug_print_alloc_pages() {
        printf("Thread page map:\n");
        for(auto &p : pgtable) {
            printf("(0x%lx -> 0x%lx, 0x%x), ", p.first, p.second.ppi, p.second.flg);
        }
        printf("\n");
    }

    inline VirtAddrT alloc_mmap_fixed(VirtAddrT addr, uint64_t size, PageFlagT flag, int32_t fd, uint64_t offset, string info) {
        VPageIndexT vpi = (addr >> PAGE_ADDR_OFFSET);
        VPageIndexT vpi2 = (ALIGN(addr + size, PAGE_LEN_BYTE) >> PAGE_ADDR_OFFSET);
        if((vpi << PAGE_ADDR_OFFSET) <= brk_va) {
            return 0;
        }
        free_mmap(addr, size);
        auto iter = mmap_segments.begin();
        for(; iter != mmap_segments.end(); iter++) {
            if(iter->vpindex + iter->vpcnt <= vpi) break;
        }
        mmap_segments.emplace(iter,VAddrSeg{
                .vpindex = vpi, .vpcnt = vpi2 - vpi, .info = info
            }
        );
        __alloc_multi_page(vpi, vpi2 - vpi, flag, fd, offset);
        return addr;
    }

    inline VirtAddrT alloc_mmap(uint64_t size, PageFlagT flag, int32_t fd, uint64_t offset, string info) {
        uint64_t vpcnt = (ALIGN(size, PAGE_LEN_BYTE) >> PAGE_ADDR_OFFSET);
        VPageIndexT top = (MAX_MMAP_VADDR >> PAGE_ADDR_OFFSET);
        auto iter = mmap_segments.begin();
        for(; iter != mmap_segments.end(); iter++) {
            if(iter->vpindex + iter->vpcnt + vpcnt <= top) {
                break;
            }
            top = iter->vpindex;
        }
        VPageIndexT vpindex = top - vpcnt;
        if((vpindex << PAGE_ADDR_OFFSET) <= brk_va) {
            LOG(ERROR) << "Virt Addr Space Run out";
            simroot_assert(0);
        }
        mmap_segments.emplace(iter,VAddrSeg{
                .vpindex = vpindex, .vpcnt = vpcnt, .info = info
            }
        );
        __alloc_multi_page(vpindex, vpcnt, flag, fd, offset);
        return (vpindex << PAGE_ADDR_OFFSET);
    }

    inline void free_mmap(VirtAddrT va, uint64_t size) {
        VPageIndexT vpi = (va >> PAGE_ADDR_OFFSET);
        VPageIndexT vpi2 = (ALIGN(va + size, PAGE_LEN_BYTE) >> PAGE_ADDR_OFFSET);
        for(auto iter = mmap_segments.begin(); iter != mmap_segments.end(); ) {
            VPageIndexT i = iter->vpindex, i2 = iter->vpindex + iter->vpcnt;
            if(i >= vpi2 || i2 <= vpi) {
                iter++;
                continue;
            }
            if(vpi > i && vpi2 < i2) {
                VAddrSeg tmp = *iter;
                iter->vpcnt = vpi - i;
                tmp.vpindex = vpi2;
                tmp.vpcnt = i2 - vpi2;
                iter = mmap_segments.insert(iter, tmp);
                iter++;
                iter++;
                continue;
            }
            if(vpi <= i && vpi2 >= i2) {
                iter = mmap_segments.erase(iter);
                continue;
            }
            if(vpi2 < i2) {
                iter->vpindex = vpi2;
                iter->vpcnt = i2 - vpi2;
            }
            else if(vpi > i) {
                iter->vpcnt = vpi - i;
            }
            iter++;
            continue;
        }
        for(VPageIndexT i = vpi; i < vpi2; i++) {
            auto res = pgtable.find(i);
            if(res != pgtable.end()) {
                ppman->free(res->second.ppi);
                pgtable.erase(res);
            }
        }
    }

    inline void init_elf_seg(VirtAddrT start, uint64_t size, PageFlagT flag, string info) {
        flag &= (PGFLAG_R | PGFLAG_W | PGFLAG_X);
        VPageIndexT vpindex = (start >> PAGE_ADDR_OFFSET);
        uint64_t vpcnt = (ALIGN(start + size, PAGE_LEN_BYTE) >> PAGE_ADDR_OFFSET) - vpindex;
        __alloc_multi_page(vpindex, vpcnt, flag, -1, 0);
        brk_va = std::max(brk_va, ALIGN(start + size, PAGE_LEN_BYTE));
    }

    inline void init_dynlib_seg(VirtAddrT start, uint64_t size, PageFlagT flag, string info) {
        flag &= (PGFLAG_R | PGFLAG_W | PGFLAG_X);
        VPageIndexT vpindex = (start >> PAGE_ADDR_OFFSET);
        uint64_t vpcnt = (ALIGN(start + size, PAGE_LEN_BYTE) >> PAGE_ADDR_OFFSET) - vpindex;
        __alloc_multi_page(vpindex, vpcnt, flag, -1, 0);
        dyn_lib_brk = std::max(dyn_lib_brk, ALIGN(start + size, PAGE_LEN_BYTE));
    }

    inline VirtAddrT alloc_brk(VirtAddrT brk) {
        VirtAddrT ret = 0;
        if(brk <= brk_va) {
            ret = brk_va;
        }
        else {
            VPageIndexT vpindex = (ALIGN(brk_va, PAGE_LEN_BYTE) >> PAGE_ADDR_OFFSET);
            uint64_t vpcnt = (ALIGN(brk, PAGE_LEN_BYTE) >> PAGE_ADDR_OFFSET) - vpindex;
            __alloc_multi_page(vpindex, vpcnt, (PGFLAG_R | PGFLAG_W), -1, 0);
            ret = brk_va = brk;
        }
        return ret;
    }

    inline bool pgcpy_hostmem_to_va(VPageIndexT vpi, uint64_t vpcnt, void *hostmem) {
        for(uint64_t i = 0; i < vpcnt; i++) {
            auto res = pgtable.find(vpi + i);
            if(res == pgtable.end()) {
                return false;
            }
            uint8_t *hostpg = ppman->real_addr_of(res->second.ppi);
            memcpy(hostpg, ((uint8_t*)hostmem) + (i * PAGE_LEN_BYTE), PAGE_LEN_BYTE);
        }
        return true;
    }

    // std::unordered_map<string, VirtAddrT> dynamic_symbols;
    // std::unordered_map<string, VirtAddrT> lib_loaded;
    VirtAddrT dyn_lib_brk = MAX_MMAP_VADDR;
};


#endif

#ifndef RVSIM_CACHE_COMMON_H
#define RVSIM_CACHE_COMMON_H

#include "coherence.h"

#include "common.h"
#include "spinlocks.h"
#include "simroot.h"

namespace simcache {

typedef struct {
    uint64_t        data[CACHE_LINE_LEN_I64];
    CacheLineState  state;
} DefaultCacheLine;

template <typename PayloadT>
class GenericLRUCacheBlock {
public:
    GenericLRUCacheBlock(uint32_t set_addr_offset, uint32_t line_per_set) {
        this->set_addr_offset = set_addr_offset;
        this->line_per_set = line_per_set;
        this->set_count = 1 << set_addr_offset;
        p_sets = new std::unordered_map<LineIndexT, PayloadT>[set_count];
        p_lrus = new std::list<LineIndexT>[set_count];
    };
    ~GenericLRUCacheBlock() {
        if(p_sets) delete[] p_sets;
        if(p_lrus) delete[] p_lrus;
    };

    // return true if hit
    bool get_line(LineIndexT lindex, PayloadT **out, bool lru_tag = false) {
        bool hit = false;
        std::unordered_map<LineIndexT, PayloadT> &set = p_sets[line_index_to_set_index(lindex)];
        std::list<LineIndexT> &lru = p_lrus[line_index_to_set_index(lindex)];
        auto res = set.find(lindex);
        if(res != set.end()) {
            if(out) *out = &(res->second);
            hit = true;
            if(lru_tag) {
                tag_lru(lindex);
            }
        }
        return hit;
    }
    // return true if hit
    bool update_line(LineIndexT lindex, PayloadT *buf, bool lru_tag = false) {
        bool hit = false;
        std::unordered_map<LineIndexT, PayloadT> &set = p_sets[line_index_to_set_index(lindex)];
        auto res = set.find(lindex);
        if(res != set.end()) {
            res->second = *buf;
            hit = true;
            if(lru_tag) {
                tag_lru(lindex);
            }
        }
        return hit;
    }
    // return true if replaced
    bool insert_line(LineIndexT lindex, PayloadT *buf, LineIndexT *replaced, PayloadT *replaced_buf) {
        bool ret = false;
        std::unordered_map<LineIndexT, PayloadT> &set = p_sets[line_index_to_set_index(lindex)];
        std::list<LineIndexT> &lru = p_lrus[line_index_to_set_index(lindex)];
        auto res = set.find(lindex);
        if(res != set.end()) {
            res->second = *buf;
            tag_lru(lindex);
        }
        else if(set.size() < line_per_set) {
            set.insert(std::make_pair(lindex, *buf));
            tag_lru(lindex);
        }
        else {
            ret = true;
            if(replaced) *replaced = lru.back();
            PayloadT *tmp = nullptr;
            assert(get_line(lru.back(), &tmp, false));
            if(replaced_buf) *replaced_buf = *tmp;
            remove_line(lru.back());
            set.insert(std::make_pair(lindex, *buf));
            tag_lru(lindex);
        }
        return ret;
    }
    void remove_line(LineIndexT lindex) {
        bool hit = false;
        std::unordered_map<LineIndexT, PayloadT> &set = p_sets[line_index_to_set_index(lindex)];
        std::list<LineIndexT> &lru = p_lrus[line_index_to_set_index(lindex)];
        set.erase(lindex);
        lru.remove(lindex);
        pinned_line.erase(lindex);
    }
    void clear() {
        for(int i = 0; i < set_count; i++) {
            p_sets[i].clear();
            p_lrus[i].clear();
        }
    }
    void pin(LineIndexT lindex) {
        std::list<LineIndexT> &lru = p_lrus[line_index_to_set_index(lindex)];
        auto iter = find_iteratable(lru.begin(), lru.end(), lindex);
        if(iter != lru.end()) {
            lru.erase(iter);
            pinned_line.insert(lindex);
        }
    }
    void unpin(LineIndexT lindex) {
        std::list<LineIndexT> &lru = p_lrus[line_index_to_set_index(lindex)];
        auto iter = pinned_line.find(lindex);
        if(iter != pinned_line.end()) {
            pinned_line.erase(lindex);
            lru.push_front(lindex);
        }
    }

    uint32_t set_addr_offset;
    uint32_t set_count;
    uint32_t line_per_set;

    std::unordered_map<LineIndexT, PayloadT> *p_sets = nullptr;
    std::list<LineIndexT> *p_lrus = nullptr;
    std::set<LineIndexT> pinned_line;

    inline void tag_lru(LineIndexT lindex) {
        std::list<LineIndexT> &lru = p_lrus[line_index_to_set_index(lindex)];
        lru.remove(lindex);
        lru.push_front(lindex);
    }
    inline uint32_t line_index_to_set_index(LineIndexT lindex) {
        return (lindex & (set_count - 1));
    }
};

typedef struct {
    uint64_t line_buf[CACHE_LINE_LEN_I64];
    
    CacheMSHRState state = CacheMSHRState::invalid;

    uint8_t get_data_ready = 0;
    uint8_t get_ack_cnt_ready = 0;
    uint16_t need_invalid_ack = 0;
    uint16_t invalid_ack = 0;

    uint64_t log_start_cycle = 0;
} MSHREntry;

class MSHRArray {
public:
    MSHRArray(uint16_t num) : max_sz(num) {};
    ~MSHRArray() {};

    MSHREntry *get(LineIndexT lindex) {
        MSHREntry * ret = nullptr;
        lock.read_lock();
        auto res = hashmap.find(lindex);
        if(res != hashmap.end()) {
            ret = &(res->second);
        }
        lock.read_unlock();
        return ret;
    };

    MSHREntry *alloc(LineIndexT lindex) {
        MSHREntry * ret = nullptr;
        lock.write_lock();
        auto res = hashmap.find(lindex);
        if(res != hashmap.end()) {
            // ret = &(res->second);
        }
        else if(hashmap.size() < max_sz) {
            hashmap.insert(std::make_pair(lindex, MSHREntry()));
            ret = &(hashmap[lindex]);
        }
        lock.write_unlock();
        if(ret) {
            memset(ret, 0, sizeof(MSHREntry));
        }
        return ret;
    }

    void remove(LineIndexT lindex) {
        lock.write_lock();
        hashmap.erase(lindex);
        lock.write_unlock();
    }

    void clear() {
        lock.write_lock();
        hashmap.clear();
        lock.write_unlock();
    }

    uint16_t max_sz = 0;
    std::unordered_map<LineIndexT, MSHREntry> hashmap;
    SpinRWLock lock;
};

}

#endif

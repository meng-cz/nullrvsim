
#include <assert.h>

#include "cacheblock.h"


namespace simcache {

BaseCacheBlock::BaseCacheBlock(uint32_t set_addr_offset, uint32_t line_per_set) {
    this->set_addr_offset = set_addr_offset;
    this->line_per_set = line_per_set;
    this->set_count = 1 << set_addr_offset;

    p_sets = new CacheSet[set_count];
    p_all_line = new uint8_t[set_count * line_per_set * CACHE_LINE_LEN_BYTE];

    uint32_t index = 0;
    for(int i = 0; i < set_count; i++) {
        for(int j = 0; j < line_per_set; j++) {
            p_sets[i].free_line.emplace_back(CacheIndexItem{.line = 0, .index = index});
            index++;
        }
    }
}

BaseCacheBlock::~BaseCacheBlock() {
    if(p_all_line) delete[] p_all_line;
    if(p_sets) delete[] p_sets;
}

bool BaseCacheBlock::get_line(LineIndexT lindex, uint8_t **out, CacheLineState *out_state) {
    LineTag line = line_index_to_line_tag(lindex);
    CacheSet &set = p_sets[line_index_to_set_index(lindex)];
    bool hit = false;
    for(auto &item : set.used_line) {
        if(item.line == line) {
            uint8_t * ptr = p_all_line + (item.index * CACHE_LINE_LEN_BYTE);
            hit = true;
            if(out) {
                *out = ptr;
            }
            if(out_state) {
                *out_state = item.attr;
            }
            break;
        }
    }
    return hit;
}

bool BaseCacheBlock::update_line(LineIndexT lindex, uint8_t *buf, CacheLineState init_state) {
    LineTag line = line_index_to_line_tag(lindex);
    CacheSet &set = p_sets[line_index_to_set_index(lindex)];
    bool hit = false;
    for(auto &item : set.used_line) {
        if(item.line == line) {
            uint8_t * ptr = p_all_line + (item.index * CACHE_LINE_LEN_BYTE);
            memcpy(ptr, buf, CACHE_LINE_LEN_BYTE);
            hit = true;
            break;
        }
    }
    if(!hit && !set.free_line.empty()) {
        set.used_line.emplace_back(set.free_line.front());
        set.free_line.pop_front();
        set.used_line.back().line = line;
        set.used_line.back().attr = init_state;
        uint8_t * ptr = p_all_line + (set.used_line.back().index * CACHE_LINE_LEN_BYTE);
        memcpy(ptr, buf, CACHE_LINE_LEN_BYTE);
        hit = true;
    }
    return hit;
}

bool BaseCacheBlock::update_line_state(LineIndexT lindex, CacheLineState state) {
    LineTag line = line_index_to_line_tag(lindex);
    CacheSet &set = p_sets[line_index_to_set_index(lindex)];
    bool hit = false;
    for(auto &item : set.used_line) {
        if(item.line == line) {
            item.attr = state;
            hit = true;
            break;
        }
    }
    return hit;
}

void BaseCacheBlock::remove_line(LineIndexT lindex) {
    LineTag line = line_index_to_line_tag(lindex);
    CacheSet &set = p_sets[line_index_to_set_index(lindex)];
    for(auto iter = set.used_line.begin(); iter != set.used_line.end(); iter++) {
        if(iter->line == line) {
            set.free_line.emplace_back(*iter);
            set.used_line.erase(iter);
            break;
        }
    }
}

void BaseCacheBlock::clear() {
    for(int i = 0; i < set_count; i++) {
        CacheSet &set = p_sets[i];
        set.free_line.insert(set.free_line.end(), set.used_line.begin(), set.used_line.end());
        set.used_line.clear();
    }
}

void BaseCacheBlock::debug_print_lines() {
    for(uint32_t i = 0; i < set_count; i++) {
        printf("Set %d: ", i);
        for(auto &item: p_sets[i].used_line) {
            printf("(0x%lx -> %d, %d), ", item.line, item.index, (int)(item.attr));
        }
        printf(" Free %ld\n", p_sets[i].free_line.size());
    }
}

}

namespace test {

bool test_base_cache_block() {
    simcache::BaseCacheBlock *cb = new simcache::BaseCacheBlock(2, 4);
    uint8_t buf[CACHE_LINE_LEN_BYTE];
    for(uint32_t i = 0; i < 16; i++) {
        memset(buf, i, CACHE_LINE_LEN_BYTE);
        assert(cb->update_line(i << CACHE_LINE_ADDR_OFFSET, buf));
    }
    printf("----- 1 -----\n");
    cb->debug_print_lines();
    for(uint32_t i = 16; i < 24; i++) {
        memset(buf, i, CACHE_LINE_LEN_BYTE);
        assert(!(cb->update_line(i << CACHE_LINE_ADDR_OFFSET, buf)));
    }
    printf("----- 2 -----\n");
    cb->debug_print_lines();
    for(uint32_t i = 0; i < 16; i++) {
        uint8_t *p = nullptr;
        assert(cb->get_line(i << CACHE_LINE_ADDR_OFFSET, &p, nullptr));
        for(uint32_t j = 0; j < CACHE_LINE_LEN_BYTE; j++) {
            assert(p[j] == i);
        }
    }
    printf("----- 3 -----\n");
    cb->debug_print_lines();
    for(uint32_t i = 0; i < 4; i++) {
        cb->remove_line(i << (CACHE_LINE_ADDR_OFFSET + 1));
    }
    printf("----- 4 -----\n");
    cb->debug_print_lines();
    for(uint32_t i = 16; i < 24; i+=2) {
        memset(buf, i, CACHE_LINE_LEN_BYTE);
        assert(cb->update_line(i << CACHE_LINE_ADDR_OFFSET, buf));
        assert(!(cb->update_line((i+1) << CACHE_LINE_ADDR_OFFSET, buf)));
    }
    printf("----- 5 -----\n");
    cb->debug_print_lines();
    printf("Pass!!!\n");
    return true;
}

}

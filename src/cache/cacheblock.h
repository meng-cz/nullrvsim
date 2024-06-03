#ifndef RVSIM_CPU_CACHEBLOCK_H
#define RVSIM_CPU_CACHEBLOCK_H

#include "common.h"

#include "cacheinterface.h"
#include "coherence.h"

namespace simcache {

typedef uint64_t LineTag;
typedef uint32_t LineSetIndex;

class BaseCacheBlock {

public:
    BaseCacheBlock(uint32_t set_addr_offset, uint32_t line_per_set);
    ~BaseCacheBlock();

    bool get_line(LineIndexT lindex, uint8_t **out, CacheLineState *out_state);
    bool update_line(LineIndexT lindex, uint8_t *buf, CacheLineState init_state = CacheLineState::invalid);
    bool update_line_state(LineIndexT lindex, CacheLineState state);
    void remove_line(LineIndexT lindex);
    void clear();

    void debug_print_lines();

    uint32_t set_addr_offset;
    uint32_t set_count;
    uint32_t line_per_set;

    inline LineTag line_index_to_line_tag(LineIndexT lindex) {
        return (lindex >> set_addr_offset);
    }

    inline LineSetIndex line_index_to_set_index(LineIndexT lindex) {
        return (lindex & (set_count - 1));
    }

    inline LineIndexT get_line_index_of(LineTag ltag, LineSetIndex sindex) {
        return (((LineIndexT)ltag) << ((LineIndexT)set_addr_offset)) | ((LineIndexT)sindex);
    }

    typedef struct {
        LineTag line = 0;
        CacheLineState attr = CacheLineState::invalid;
        uint32_t index;
    } CacheIndexItem;

    typedef struct {
        std::list<CacheIndexItem> free_line;
        std::list<CacheIndexItem> used_line;
    } CacheSet;
    
    CacheSet *p_sets = nullptr;

    uint8_t *p_all_line = nullptr;
};

}

namespace test {

bool test_base_cache_block();

}


#endif

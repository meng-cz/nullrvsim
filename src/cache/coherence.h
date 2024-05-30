#ifndef RVSIM_CACHE_COHENRENCE_H
#define RVSIM_CACHE_COHENRENCE_H

#include "common.h"

namespace simcache {

enum class CCProtocal {
    mesi = 0,
    moesi,
};

enum class CacheLineState {
    invalid = 0,
    exclusive,
    shared,
    modified,
    owned,
};

string get_cache_state_name_str(CacheLineState state);

enum class CacheMSHRState {
    invalid = 0,
    itoi,
    itos,
    itom,
    stom,
    mtoi,
    stoi,
    etoi,
    otom,
    otoi,
};

string get_cache_mshr_state_name_str(CacheMSHRState state);

enum class CCMsgType {
    // For MESI
    invalid,        // 2->1: Invalid a cache line. arg: to which port the inv_ack should be send.
    invalid_ack,    // 1->1 or 1->2: Acknowledge a invalid message
    gets,           // 1->2: Request a cache line as read-only. arg: bus port id
    gets_forward,   // 2->1: Forward a cache request to its owner. arg: to which port the get_data should be send.
    getm,           // 1->2: Request a cache line as read-write. arg: bus port id
    getm_forward,   // 2->1: Forward a cache request to its owner. arg: to which port the get_data should be send.
    getm_ack,       // 2->1: Acknowledge a getm, arg: Acknowledgement count
    gets_resp,      // 2->1 or 1->1: Fetch success, arg: share cnt, lina data followed.
    getm_resp,      // 2->1 or 1->1: Fetch success, arg: no need ack, lina data followed.
    get_resp_mem,   // 1->1: Fetch success from memory, lina data followed.
    get_ack,        // 1->2: Send to L2 when (1) get from memory (2) get from other L1.
    puts,           // 1->2: Write-back a shared line. arg: bus port id.
    putm,           // 1->2: Write-back a dirty line, lina data followed. arg: bus port id.
    pute,           // 1->2: Write-back a clean line. arg: bus port id.
    put_ack,        // 2->1: Acknowledge a put
    // For MOESI Only
    puto,           // 1->2: Write-back a dirty line, lina data followed. arg: bus port id.
};

string get_cc_msg_type_name_str(CCMsgType type);

typedef struct {
    CCMsgType   type;
    uint32_t    arg = 0;
    LineIndexT  line = 0;
    std::vector<uint8_t>    data;
} CacheCohenrenceMsg;



}

#endif

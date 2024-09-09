#ifndef RVSIM_CACHE_MOESI_PROTOCAL_H
#define RVSIM_CACHE_MOESI_PROTOCAL_H

#include "common.h"

namespace simcache {
namespace moesi {

const uint32_t CC_INVALID = 0;
const uint32_t CC_EXCLUSIVE = 1;
const uint32_t CC_SHARED = 2;
const uint32_t CC_MODIFIED = 3;
const uint32_t CC_OWNED = 4;

string get_cache_state_name_str(uint32_t state);

const uint32_t MSHR_INVALID = 0;
const uint32_t MSHR_ITOI = 1;
const uint32_t MSHR_ITOS = 2;
const uint32_t MSHR_ITOM = 3;
const uint32_t MSHR_STOM = 4;
const uint32_t MSHR_MTOI = 5;
const uint32_t MSHR_STOI = 6;
const uint32_t MSHR_ETOI = 7;
const uint32_t MSHR_OTOM = 8;
const uint32_t MSHR_OTOI = 9;
// 以下状态只对作为req node的L2有效
// const uint32_t MSHR_MTOF = 10;  // M收到GETM_FORWARD时进入,等待L1的INVALID_ACK，离开时发送GETM_RESP，目的端口为arg
// const uint32_t MSHR_OTOF = 11;  // O收到GETM_FORWARD时进入,等待L1的INVALID_ACK，离开时发送GETM_RESP，目的端口为arg
// const uint32_t MSHR_ATOIA = 12; // 所有状态收到INVALID时进入,等待L1的INVALID_ACK，离开时发送INVALID_ACK，目的端口为arg

typedef struct {
    uint64_t line_buf[CACHE_LINE_LEN_I64];
    
    uint32_t state = 0;

    uint8_t get_data_ready = 0;
    uint8_t get_ack_cnt_ready = 0;
    uint16_t need_invalid_ack = 0;
    uint16_t invalid_ack = 0;

    uint64_t log_start_cycle = 0;
} DefMSHREntry;

string get_cache_mshr_state_name_str(uint32_t state);

const uint32_t MSG_INVALID = 0;         // 2->1: Invalid a cache line. arg: to which port the inv_ack should be send.
const uint32_t MSG_INVALID_ACK = 1;     // 1->1 or 1->2: Acknowledge a invalid message
const uint32_t MSG_GETS = 2;            // 1->2: Request a cache line as read-only. arg: bus port id
const uint32_t MSG_GETS_FORWARD = 3;    // 2->1: Forward a cache request to its owner. arg: to which port the get_data should be send.
const uint32_t MSG_GETM = 4;            // 1->2: Request a cache line as read-write. arg: bus port id
const uint32_t MSG_GETM_FORWARD = 5;    // 2->1: Forward a cache request to its owner. arg: to which port the get_data should be send.
const uint32_t MSG_GETM_ACK = 6;        // 2->1: Acknowledge a getm, arg: Acknowledgement count
const uint32_t MSG_GETS_RESP = 7;       // 2->1 or 1->1: Fetch success, arg: share cnt, lina data followed.
const uint32_t MSG_GETM_RESP = 8;       // 2->1 or 1->1: Fetch success, arg: no need ack, lina data followed.
const uint32_t MSG_GET_RESP_MEM = 9;    // 1->1: Fetch success from memory, lina data followed.
const uint32_t MSG_GET_ACK = 10;        // 1->2: Send to L2 when (1) get from memory (2) get from other L1.
const uint32_t MSG_PUTS = 11;           // 1->2: Write-back a shared line. arg: bus port id.
const uint32_t MSG_PUTM = 12;           // 1->2: Write-back a dirty line, lina data followed. arg: bus port id.
const uint32_t MSG_PUTE = 13;           // 1->2: Write-back a clean line. arg: bus port id.
const uint32_t MSG_PUTO = 14;           // 1->2: Write-back a dirty line, lina data followed. arg: bus port id.
const uint32_t MSG_PUT_ACK = 15;        // 2->1: Acknowledge a put

string get_cc_msg_type_name_str(uint32_t type);

const uint32_t CHANNEL_CNT = 3;

const uint32_t CHANNEL_ACK = 0;
const uint32_t CHANNEL_RESP = 1;
const uint32_t CHANNEL_REQ = 2;

const uint32_t CHANNEL_WIDTH_ACK = 32;
const uint32_t CHANNEL_WIDTH_RESP = 64;
const uint32_t CHANNEL_WIDTH_REQ = 96;

typedef struct {
    uint32_t    type;
    uint32_t    arg = 0;
    uint32_t    transid = 0;
    LineIndexT  line = 0;
    std::vector<uint8_t>    data;
} CacheCohenrenceMsg;

void construct_msg_pack(CacheCohenrenceMsg &msg, vector<uint8_t> &buf);
void parse_msg_pack(vector<uint8_t> &buf, CacheCohenrenceMsg &msg);


}}

#endif

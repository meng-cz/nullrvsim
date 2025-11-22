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


#include "protocal.h"

namespace simcache {
namespace moesi {


string get_cache_state_name_str(uint32_t state) {
    switch (state)
    {
    case CC_INVALID: return "I";
    case CC_EXCLUSIVE: return "E";
    case CC_SHARED: return "S";
    case CC_MODIFIED: return "M";
    case CC_OWNED: return "O";
    }
    return string("unknown") + to_string((int)state);
}

string get_cache_mshr_state_name_str(uint32_t state) {
    switch (state)
    {
    case MSHR_INVALID: return "invalid";
    case MSHR_ITOI: return "itoi";
    case MSHR_ITOS: return "itos";
    case MSHR_ITOM: return "itom";
    case MSHR_STOM: return "stom";
    case MSHR_MTOI: return "mtoi";
    case MSHR_STOI: return "stoi";
    case MSHR_ETOI: return "etoi";
    case MSHR_OTOM: return "otom";
    case MSHR_OTOI: return "otoi";
    }
    return string("unknown") + to_string((int)state);
}

string get_cc_msg_type_name_str(uint32_t type) {
    switch (type)
    {
    case MSG_INVALID: return "invalid";
    case MSG_INVALID_ACK: return "invalid_ack";
    case MSG_GETS: return "gets";
    case MSG_GETS_FORWARD: return "gets_forward";
    case MSG_GETM: return "getm";
    case MSG_GETM_FORWARD: return "getm_forward";
    case MSG_GETM_ACK: return "getm_ack";
    case MSG_GETS_RESP: return "gets_resp";
    case MSG_GETM_RESP: return "getm_resp";
    case MSG_GET_RESP_MEM: return "get_resp_mem";
    case MSG_GET_ACK: return "get_ack";
    case MSG_PUTS: return "puts";
    case MSG_PUTM: return "putm";
    case MSG_PUTE: return "pute";
    case MSG_PUTO: return "put_ack";
    case MSG_PUT_ACK: return "puto";
    }
    return string("unknown") + to_string((int)type);
};

const uint32_t head_size = 20;

void construct_msg_pack(CacheCohenrenceMsg &msg, vector<uint8_t> &buf) {
    buf.resize(msg.data.size() + head_size);
    *((uint32_t*)(buf.data() + 0)) = msg.type;
    *((uint32_t*)(buf.data() + 4)) = msg.arg;
    *((uint32_t*)(buf.data() + 8)) = msg.transid;
    *((uint64_t*)(buf.data() + 12)) = msg.line;
    if(!msg.data.empty()) memcpy(buf.data() + head_size, msg.data.data(), msg.data.size());
}

void parse_msg_pack(vector<uint8_t> &buf, CacheCohenrenceMsg &msg) {
    msg.type = *((uint32_t*)(buf.data() + 0));
    msg.arg = *((uint32_t*)(buf.data() + 4));
    msg.transid = *((uint32_t*)(buf.data() + 8));
    msg.line = *((uint64_t*)(buf.data() + 12));
    if(buf.size() > head_size) {
        msg.data.resize(buf.size() - head_size);
        memcpy(msg.data.data(), buf.data() + head_size, msg.data.size());
    }
}



}}

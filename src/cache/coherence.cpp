
#include "coherence.h"

namespace simcache {

string get_cache_state_name_str(CacheLineState state) {
    switch (state)
    {
    case CacheLineState::invalid: return "I";
    case CacheLineState::exclusive: return "E";
    case CacheLineState::shared: return "S";
    case CacheLineState::modified: return "M";
    case CacheLineState::owned: return "O";
    }
    return string("unknown") + to_string((int)state);
}

string get_cache_mshr_state_name_str(CacheMSHRState state) {
    switch (state)
    {
    case CacheMSHRState::invalid: return "invalid";
    case CacheMSHRState::itoi: return "itoi";
    case CacheMSHRState::itos: return "itos";
    case CacheMSHRState::itom: return "itom";
    case CacheMSHRState::stom: return "stom";
    case CacheMSHRState::mtoi: return "mtoi";
    case CacheMSHRState::stoi: return "stoi";
    case CacheMSHRState::etoi: return "etoi";
    case CacheMSHRState::otom: return "otom";
    case CacheMSHRState::otoi: return "otoi";
    }
    return string("unknown") + to_string((int)state);
}

string get_cc_msg_type_name_str(CCMsgType type) {
    switch (type)
    {
    case CCMsgType::invalid: return "invalid";
    case CCMsgType::invalid_ack: return "invalid_ack";
    case CCMsgType::gets: return "gets";
    case CCMsgType::gets_forward: return "gets_forward";
    case CCMsgType::getm: return "getm";
    case CCMsgType::getm_forward: return "getm_forward";
    case CCMsgType::getm_ack: return "getm_ack";
    case CCMsgType::gets_resp: return "gets_resp";
    case CCMsgType::getm_resp: return "getm_resp";
    case CCMsgType::get_resp_mem: return "get_resp_mem";
    case CCMsgType::get_ack: return "get_ack";
    case CCMsgType::puts: return "puts";
    case CCMsgType::putm: return "putm";
    case CCMsgType::pute: return "pute";
    case CCMsgType::put_ack: return "put_ack";
    case CCMsgType::puto: return "puto";
    }
    return string("unknown") + to_string((int)type);
};



}

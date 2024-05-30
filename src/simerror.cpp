
#include "simerror.h"

std::unordered_map<SimError, string> simerror_name_table;
bool simerror_name_table_init = false;

#define ADD_SIMERROR(name) simerror_name_table.emplace(SimError::name, string(#name))

void init_simerror_name_table() {
    if(simerror_name_table_init) return;
    ADD_SIMERROR(success);
    ADD_SIMERROR(illegalinst);
    ADD_SIMERROR(unsupported);
    ADD_SIMERROR(devidebyzero);
    ADD_SIMERROR(invalidaddr);
    ADD_SIMERROR(unaligned);
    ADD_SIMERROR(unaccessable);
    ADD_SIMERROR(invalidpc);
    ADD_SIMERROR(unexecutable);
    ADD_SIMERROR(miss);
    ADD_SIMERROR(processing);
    ADD_SIMERROR(busy);
    ADD_SIMERROR(coherence);
    ADD_SIMERROR(unconditional);
    ADD_SIMERROR(slreorder);
    ADD_SIMERROR(llreorder);
    simerror_name_table_init = true;
}

#undef ADD_SIMERROR

string error_name(SimError err) {
    if(!simerror_name_table_init) init_simerror_name_table();
    auto res = simerror_name_table.find(err);
    if(res != simerror_name_table.end()) {
        return res->second;
    }
    else {
        return string("unknown#") + to_string((int)err);
    }
}


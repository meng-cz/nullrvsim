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
    ADD_SIMERROR(pagefault);
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


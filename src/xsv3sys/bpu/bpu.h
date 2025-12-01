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

#pragma once

#include "common.h"

#include "xsv3sys/bundles/bpu.h"

namespace xsv3sys {



class BPU {
public:

typedef struct {
    void * __top;
    string __instance_name;
    bool (*_request_ftqEnqueue)(void *, BPUToFTQResult &);
} ConstructorParams;

    BPU(ConstructorParams &params);

    void on_current_tick();
    void apply_next_tick();

    void redirect(BPURedirect &redirect);
    void train(BPUTrain &train);
    void commit(BPUCommit &commit);

protected:

    inline bool ftqEnqueue(BPUToFTQResult &toFtq) {
        return params._request_ftqEnqueue(params.__top, toFtq);
    }

private:
    ConstructorParams params;

};

} // namespace xsv3sys

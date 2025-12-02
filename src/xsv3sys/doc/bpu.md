
# NullRVSim XSV3SYS 模块软件代码设计文档


## BPU

分支预测模块，负责向FTQ持续提供FetchBlock信息

一个FetchBlock满足以下条件：
1. 总字节数不超过FetchBlockSize
2. 最多包含两条带跳转指令，如果有两条，则第一条一定是branch
3. 如果有一条jal或jalr指令，则该指令一定是FetchBlock最后一条指令

取值模块会根据这一原则取值，并重定向或训练BPU

BPU包含uBTB、BTB、TAGE-SC、ITTAGE、RAS预测器，其中：
1. uBTB为一周期延迟，在s1根据当前FetchBlock的开始地址预测下一个FetchBlock的开始地址
2. BTB为两周期延迟，在s2根据当前FetchBlock的开始地址预测当前FetchBlock中的分支指令位置和类型
3. TAGE为两周期延迟，在s1根据当前FetchBlock的开始地址和分支历史查表，在s2根据BTB结果匹配tag，对branch指令进行Taken预测
4. SC为三周期延迟，在s1根据当前FetchBlock的开始地址和分支历史查表，在s3根据BTB结果匹配tag，修正TAGE的预测
5. ITTAGE为三周期延迟，在s1根据当前FetchBlock的开始地址和分支历史查表，在s3根据BTB结果匹配tag，对jal/jalr的跳转目标进行预测
6. RAS为单周期栈结构，在s3根据BTB结果查表，预测ret指令的跳转目标

Request:
----------
1. ftqEnqueue, 向FTQ握手并将一个FetchBlock加入FTQ

Service:
----------
1. redirect, 整个BPU重定向到目标地址，同时恢复BPU分支历史现场
2. train, 当一个FetchBlock完成执行时，根据真实分支信息训练各种预测器
3. commit，当一个FetchBlock被提交时，根据真实分支信息更新BPU状态

Layout:
----------
```cpp
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
```

## MicroBTB

TODO

## MainBTB

TODO

## TAGE

TODO


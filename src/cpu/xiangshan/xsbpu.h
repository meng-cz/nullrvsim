#ifndef RVSIM_CPU_XS_BPU_H
#define RVSIM_CPU_XS_BPU_H

#include "xstypes.h"

#include "cache/cachecommon.h"

namespace simcpu {

namespace xs {

using simcache::GenericLRUCacheBlock;

#define XS_TAGE_NUM (4)
#define XS_SC_NUM (4)
#define XS_ITTAGE_NUM (5)

typedef struct {
    uint16_t                ft_len = 0;
    FetchPackJmp            jmpinfo;
    ValidData<VirtAddrT>    jmptarget;
    vector<FTQBranch>       branchs;
    bool                    always_taken;
} FTBEntry;

class BPU {
public:

    BPU(XiangShanParam *param, list<FTQEntry*> *ftq, VirtAddrT pc);

    void on_current_tick();
    void apply_next_tick();

    /**
     * 在IFU获取到一个FetchPackage的完整预解码信息后，如果实际FetchPack信息与FTB给的不同，则调用该函数更新FTB与uBTB
     * 如果FTB的内容错误，还需要在当前周期结束时调用apl_redirect对前端进行重定向
     * 更新fetch长度，jmp信息(如果不是jalr就更新jmp目标)，br信息(不更新预测的br taken，新ftb项会被标为always taken)
    */
    void cur_update_fetch_result(FTQEntry *fetch);
    void cur_commit_jalr_target(FTQEntry *fetch); // 更新jalr的跳转目标
    void cur_commit_branch(FTQEntry *fetch, vector<bool> &is_taken);

    void apl_redirect(VirtAddrT pc, BrHist &bhr, RASSnapshot &ras);
    void apl_redirect(VirtAddrT pc);

    simroot::LogFileT debug_ofile = nullptr;
    char log_buf[256];

    void clear_statistic() {};
    void print_statistic(std::ofstream &ofile) {};
    void print_setup_info(std::ofstream &ofile) {};
    void dump_core(std::ofstream &ofile);

protected:

    XiangShanParam *param;
    list<FTQEntry*> *ftq;

    VirtAddrT pc;

    unique_ptr<SimpleTickQueue<VirtAddrT>> p1_to_p2;
    unique_ptr<SimpleTickQueue<FTQEntry*>> p2_to_p3;
    ValidData<VirtAddrT> p3_expected_pc;

    unique_ptr<GenericLRUCacheBlock<FTBEntry>> ftb;
    unique_ptr<GenericLRUCacheBlock<FTBEntry>> ubtb;

    BrHist bhr;

    ValidData<VirtAddrT> ubtb_nextpc(VirtAddrT startpc);

    void do_pred();


// ---------- predict -----------

    // ---- Tage ----

    typedef struct {
        int8_t  pred[XSIFU_BRANCH_CNT];
        uint8_t u = 0;
    } TageEntry;

    vector<std::array<int8_t, XSIFU_BRANCH_CNT>> tage_t0;
    vector<int8_t> use_alt_cnt;
    const int8_t use_alt_cnt_threshold = 0;
    vector<unordered_map<uint64_t, TageEntry>> tage[XS_TAGE_NUM];
    int32_t tage_bank_tick_ctr = 0;
    const int32_t tage_bank_tick_ctr_max_value = (1<<7);
    
    typedef struct {
        uint64_t                index0;
        uint64_t                index[XS_TAGE_NUM];
        uint64_t                tag[XS_TAGE_NUM];
        TageEntry               *res[XS_TAGE_NUM];
        int8_t                  *use_alt;
        std::array<int8_t, XSIFU_BRANCH_CNT>   *res0;
    } TageIndexRes;
    void _tage_indexing(BrHist &brhist, VirtAddrT startpc, TageIndexRes* res);
    std::array<int8_t, XSIFU_BRANCH_CNT> _tage_result_of(const TageIndexRes &res);
    void _alloc_tage_entry(uint32_t tn, TageIndexRes* res, vector<bool> &taken);

    std::array<int8_t, XSIFU_BRANCH_CNT> pred_tage(VirtAddrT startpc);


    // ---- SC ----

    vector<std::array<int8_t, XSIFU_BRANCH_CNT>> sc[XS_SC_NUM];
    std::array<int16_t, XSIFU_BRANCH_CNT> sc_bank_thres;
    std::array<int16_t, XSIFU_BRANCH_CNT> sc_bank_ctr;

    typedef struct {
        uint64_t                                index[XS_SC_NUM];
        std::array<int8_t, XSIFU_BRANCH_CNT>    *res[XS_SC_NUM];
    } SCIndexRes;
    void _sc_indexing(BrHist &brhist, VirtAddrT startpc, SCIndexRes* res);
    std::array<int16_t, XSIFU_BRANCH_CNT> _sc_result_of(const SCIndexRes &res);
    std::array<int16_t, XSIFU_BRANCH_CNT> pred_sc(VirtAddrT startpc);


    // ---- ITTage ----

    typedef struct {
        VirtAddrT   jmptarget = 0;
        uint8_t     u = 0;
    } ITTageEntry;

    vector<unordered_map<uint64_t, ITTageEntry>> ittage[XS_ITTAGE_NUM];
    int32_t ittage_tick_ctr = 0;
    const int32_t ittage_tick_ctr_max_value = (1<<8);

    typedef struct {
        uint64_t        index[XS_ITTAGE_NUM];
        uint64_t        tag[XS_ITTAGE_NUM];
        ITTageEntry     *res[XS_ITTAGE_NUM];
    } ITTageIndexRes;
    void _ittage_indexing(BrHist &brhist, VirtAddrT startpc, ITTageIndexRes* res);
    ITTageEntry _ittage_result_of(const ITTageIndexRes &res);
    void _alloc_ittage_entry(uint32_t tn, ITTageIndexRes* res, VirtAddrT target);
    
    ITTageEntry pred_ittage(VirtAddrT startpc);


    // ---- RAS ----

    typedef struct {
        VirtAddrT   jmptarget = 0;
        uint16_t    ctr = 0;
        uint32_t    next = 0;
    } RASPSEntry;
    typedef struct {
        VirtAddrT   jmptarget = 0;
        uint16_t    ctr = 0;
    } RASCSEntry;
    vector<RASPSEntry> ras_ps;
    vector<RASCSEntry> ras_cs;
    RASSnapshot ras;

    void pred_ras_call(VirtAddrT nextpc);
    VirtAddrT pred_ras_ret();
    void commit_ras_call(RASSnapshot &ptrs);
    void commit_ras_ret(RASSnapshot &ptrs, VirtAddrT nextpc);


// ---------- update -----------

    void do_update_ftb();
    void do_update_jalr();
    void do_update_br();
    enum class UpdateBPUField {
        ftb, jalr, br
    };
    typedef struct {
        UpdateBPUField  type;
        VirtAddrT       startpc;
        uint16_t        ft_len;
        FetchPackJmp    jmpinfo;
        VirtAddrT       jmptarget;
        vector<FTQBranch> branchs;
        vector<bool>    is_taken;
        BrHist          bhrbak;
        RASSnapshot     ras;
        bool            commit_ras;
    } UpdateBPUReq;
    list<UpdateBPUReq> toapply_update_reqs;
    list<UpdateBPUReq> update_reqs;

    std::array<int8_t, XSIFU_BRANCH_CNT> update_tage();
    std::array<int16_t, XSIFU_BRANCH_CNT> update_sc();

    void update_ittage();
};


}}

#endif

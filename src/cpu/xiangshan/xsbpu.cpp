
#include "simroot.h"

#include "xsbpu.h"

namespace simcpu {

namespace xs {

const uint32_t ubtb_tag_len = 16;

static_assert(XS_TAGE_NUM == 4);
const uint32_t tage_fh0_len[XS_TAGE_NUM] = {8, 11, 11, 11};
const uint32_t tage_fh1_len[XS_TAGE_NUM] = {8, 8, 8, 8};
const uint32_t tage_fh2_len[XS_TAGE_NUM] = {7, 7, 7, 7};
const uint32_t tage_hist_len[XS_TAGE_NUM] = {8, 13, 32, 119};
const uint32_t tage_set_bits[XS_TAGE_NUM] = {11, 11, 11, 11}; // nRowsPerBr = 2048
const uint32_t tage_tag_len[XS_TAGE_NUM] = {8, 8, 8, 8};
const uint32_t tage_banks = 8;
const uint32_t tage0_set_bit = 11;

static_assert(XS_SC_NUM == 4);
const uint32_t sc_fh_len[XS_SC_NUM] = {0, 4, 8, 8};
const uint32_t sc_set_bits[XS_SC_NUM] = {9, 9, 9, 9}; // 512

static_assert(XS_ITTAGE_NUM == 5);
const uint32_t ittage_fh0_len[XS_ITTAGE_NUM] = {4, 8, 9, 9, 9};
const uint32_t ittage_fh1_len[XS_ITTAGE_NUM] = {4, 8, 9, 9, 9};
const uint32_t ittage_fh2_len[XS_ITTAGE_NUM] = {4, 8, 8, 8, 8};
const uint32_t ittage_hist_len[XS_ITTAGE_NUM] = {4, 8, 13, 16, 32};
const uint32_t ittage_set_bits[XS_ITTAGE_NUM] = {8, 8, 9, 9, 9};
const uint32_t ittage_tag_len[XS_ITTAGE_NUM] = {9, 9, 9, 9, 9};
const uint32_t ittage_banks = 2;

const uint32_t ras_ps_size = 32;
const uint32_t ras_cs_size = 16;

inline uint64_t ftb_idx(VirtAddrT startpc) {
    return (startpc >> 1);
}
inline uint64_t ubtb_idx(VirtAddrT startpc) {
    return ((startpc >> 1) & ((1UL << ubtb_tag_len) - 1UL));
}

void BPU::dump_core(std::ofstream &ofile) {
    #define LOGTOFILE(fmt, ...) do{sprintf(log_buf, fmt, ##__VA_ARGS__); ofile << log_buf;}while(0)
    LOGTOFILE("#BPU-S0-PC: 0x%ld\n", pc);
    {
        vector<VirtAddrT> tmp;
        p1_to_p2->dbg_get_all(&tmp);
        LOGTOFILE("#BPU-S1-S2: ");
        for(auto pc : tmp) LOGTOFILE("0x%lx ", pc);
        LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    {
        vector<FTQEntry*> tmp;
        p2_to_p3->dbg_get_all(&tmp);
        LOGTOFILE("#BPU-S2-S3: ");
        for(auto p : tmp) LOGTOFILE("0x%lx-0x%lx %ldinsts %ldbr %djmp 0x%lx | ", p->startpc, p->endpc, p->insts.size(), p->branchs.size(), (int)(p->jmpinfo), p->jmptarget);
        LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#BPU-FTB:\n");
        for(uint32_t s = 0; s < ftb->set_count; s++) {
            LOGTOFILE("set %d: ", s);
            for(auto &e : ftb->p_sets[s]) {
                FTBEntry &f = e.second;
                LOGTOFILE("0x%lx:len%d %ldbr(%d) %djmp 0x%lx | ", e.first, f.ft_len, f.branchs.size(), f.always_taken, (int)(f.jmpinfo), f.jaltarget);
            }
            LOGTOFILE("\n");
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#BPU-uBTB:\n");
        for(uint32_t s = 0; s < ubtb->set_count; s++) {
            LOGTOFILE("set %d: ", s);
            for(auto &e : ubtb->p_sets[s]) {
                FTBEntry &f = e.second;
                LOGTOFILE("0x%lx:len%d %ldbr %djmp 0x%lx | ", e.first, f.ft_len, f.branchs.size(), (int)(f.jmpinfo), f.jaltarget);
            }
            LOGTOFILE("\n");
        }
    }
    LOGTOFILE("\n");
    {
        LOGTOFILE("#BPU-BHR: ");
        uint64_t tmp = bhr.get(64, 64);
        for(uint64_t i = 0; i < 64; i++) {
            if(1 & (tmp >> i)) LOGTOFILE("1");
            else LOGTOFILE("0");
        }
        LOGTOFILE("\n");
    }
    LOGTOFILE("\n");
    #undef LOGTOFILE
}

BPU::BPU(XiangShanParam *param, list<FTQEntry*> *ftq, VirtAddrT pc)
: param(param), ftq(ftq), pc(pc)
{
    p1_to_p2 = make_unique<SimpleTickQueue<VirtAddrT>>(1, 1, 0);
    p2_to_p3 = make_unique<SimpleTickQueue<FTQEntry*>>(1, 1, 0);

    ftb = make_unique<GenericLRUCacheBlock<FTBEntry>>(param->ftb_set_bits, param->ftb_ways);
    ubtb = make_unique<GenericLRUCacheBlock<FTBEntry>>(0, param->ubtb_ways);

    uint32_t tage0_set_size = (1 << tage0_set_bit);
    tage_t0.assign(tage0_set_size, std::array<int8_t, XSIFU_BRANCH_CNT>());
    use_alt_cnt.assign(tage0_set_size, 0);
    for(int i = 0; i < XS_TAGE_NUM; i++) {
        tage[i].assign((1 << tage_set_bits[i]), unordered_map<uint64_t, TageEntry>());
        for(int n = 0; n < (1 << tage_set_bits[i]); n++) {
            while(tage[i][n].size() < tage_banks) {
                uint64_t tag = rand_long();
                if(tage[i][n].find(tag) == tage[i][n].end()) tage[i][n].emplace(tag, TageEntry());
            }
        }
    }

    std::array<int8_t, XSIFU_BRANCH_CNT> sc_zero_entry;
    sc_zero_entry.fill(0);
    for(int i = 0; i < XS_SC_NUM; i++) {
        sc[i].assign(1 << sc_set_bits[i], sc_zero_entry);
    }

    for(int i = 0; i < XS_ITTAGE_NUM; i++) {
        ittage[i].assign(1 << ittage_set_bits[i], unordered_map<uint64_t, ITTageEntry>());
        for(int n = 0; n < (1 << ittage_set_bits[i]); n++) {
            while(ittage[i][n].size() < ittage_banks) {
                uint64_t tag = rand_long();
                if(ittage[i][n].find(tag) == ittage[i][n].end()) ittage[i][n].emplace(tag, ITTageEntry());
            }
        }
    }

}

void BPU::on_current_tick() {

    if(!update_reqs.empty()) {
        switch (update_reqs.front().type)
        {
        case UpdateBPUField::ftb : do_update_ftb(); break;
        case UpdateBPUField::jalr : do_update_jalr(); break;
        case UpdateBPUField::br : do_update_br(); break;
        default: simroot_assert(0);
        }
        update_reqs.pop_front();
        return;
    }

    // P1 由ubtb生成下一个pc
    if(p1_to_p2->can_push()) {
        VirtAddrT oldpc = pc;
        p1_to_p2->push(pc);
        auto res = ubtb_nextpc(pc);
        if(res.valid) pc = res.data;
        else pc += param->fetch_width_bytes;
        if(debug_ofile) {
            sprintf(log_buf, "%ld:P1: PC:@0x%lx, uBTB:%d, NextPC:0x%lx", simroot::get_current_tick(), oldpc, res.valid, pc);
            simroot::log_line(debug_ofile, log_buf);
        }
    }

    // P2 分支预测
    do_pred();

    // P3 输出，如果预测结果跟uBTB生成的nextpc不同，则需要flush整个BPU
    if(p2_to_p3->can_pop() && ftq->size() <= param->ftq_size) {
        FTQEntry *fetch = p2_to_p3->top();
        p2_to_p3->pop();
        if(p3_expected_pc.valid && p3_expected_pc.data != fetch->startpc) {
            apl_redirect(p3_expected_pc.data, fetch->bhr, fetch->ras);
            if(debug_ofile) {
                sprintf(log_buf, "%ld:P3: Redirect to 0x%lx", simroot::get_current_tick(), p3_expected_pc.data);
                simroot::log_line(debug_ofile, log_buf);
            }
            delete fetch;
        }
        else {
            ftq->push_back(fetch);
            if(debug_ofile) {
                sprintf(log_buf, "%ld:P3: Output @0x%lx, len %ld", simroot::get_current_tick(), fetch->startpc, fetch->endpc - fetch->startpc);
                simroot::log_line(debug_ofile, log_buf);
            }
            p3_expected_pc.valid = true;
            p3_expected_pc.data = nextpc(fetch);
        }
    }
}

void BPU::apply_next_tick() {
    p1_to_p2->apply_next_tick();
    p2_to_p3->apply_next_tick();
    update_reqs.splice(update_reqs.end(), toapply_update_reqs);
}

void BPU::cur_update_fetch_result(FTQEntry *fetch) {
    simroot_assert(fetch->endpc > fetch->startpc);
    simroot_assert(fetch->branchs.size() <= XSIFU_BRANCH_CNT);
    UpdateBPUReq req;
    req.type = UpdateBPUField::ftb;
    req.startpc = fetch->startpc;
    req.ft_len = fetch->endpc - fetch->startpc;
    req.jmpinfo = fetch->jmpinfo;
    req.jmptarget = fetch->jmptarget;
    req.branchs = fetch->branchs;
    toapply_update_reqs.emplace_back(req);
}

void BPU::cur_commit_jalr_target(FTQEntry *fetch) {
    if((fetch->jmpinfo & FETCH_FLAG_JALR) == 0) return;
    UpdateBPUReq req;
    req.type = UpdateBPUField::jalr;
    req.startpc = fetch->startpc;
    req.jmpinfo = fetch->jmpinfo;
    req.jmptarget = fetch->jmptarget;
    req.bhrbak = fetch->bhr;
    req.ras = fetch->ras;
    req.commit_ras = fetch->commit_ras;
    toapply_update_reqs.emplace_back(req);
}

void BPU::cur_commit_branch(FTQEntry *fetch, vector<bool> &is_taken) {
    simroot_assert(is_taken.size() <= XSIFU_BRANCH_CNT);
    simroot_assert(is_taken.size() <= fetch->branchs.size());
    if(fetch->branchs.empty() || is_taken.empty()) return;
    UpdateBPUReq req;
    req.type = UpdateBPUField::br;
    req.startpc = fetch->startpc;
    req.bhrbak = fetch->bhr;
    req.branchs = fetch->branchs;
    req.is_taken = is_taken;
    req.ft_len = fetch->endpc - fetch->startpc;
    toapply_update_reqs.emplace_back(req);
}

void BPU::apl_redirect(VirtAddrT pc, BrHist &bhr, RASSnapShot &ras) {
    apl_redirect(pc);
    this->bhr = bhr;
    this->ras = ras;
}
void BPU::apl_redirect(VirtAddrT pc) {
    this->pc = pc;
    // Clean up
    list<FTQEntry*> tofree;
    p1_to_p2->clear();
    p2_to_p3->clear(&tofree);
    for(auto p : tofree) {
        delete p;
    }
    p3_expected_pc.valid = false;
    bhr.clear();
    ras.clear();
}


ValidData<VirtAddrT> BPU::ubtb_nextpc(VirtAddrT startpc) {
    FTBEntry *p = nullptr;
    ValidData<VirtAddrT> ret;
    if(ret.valid = ubtb->get_line(ubtb_idx(startpc), &p, true)) {
        if((p->jmpinfo & FETCH_FLAG_JAL) && p->jaltarget) {
            ret.data = p->jaltarget;
        }
        else {
            ret.data = startpc + p->ft_len;
        }
        for(auto &br : p->branchs) {
            if(br.pred_taken >= 0) {
                ret.data = startpc + (int64_t)(br.inst_offset) + (int64_t)(br.jmp_offset);
                break;
            }
        }
    }
    return ret;
}

void BPU::do_pred() {
    if(p1_to_p2->can_pop() == 0) return;
    if(p2_to_p3->can_push() == 0) return;

    VirtAddrT pc = p1_to_p2->top();
    p1_to_p2->pop();
    FTQEntry * fetch = new FTQEntry();
    fetch->startpc = pc;
    simroot_assert(p2_to_p3->push(fetch));

    fetch->bhr = bhr;
    fetch->ras = ras;
    fetch->endpc = fetch->startpc + param->fetch_width_bytes;
    fetch->commit_ras = false;
    fetch->jmpinfo = 0;
    fetch->jmptarget = 0;

    FTBEntry * ftb_entry = nullptr;
    bool ftb_hit = ftb->get_line(ftb_idx(fetch->startpc), &ftb_entry, true);
    if(!ftb_hit) {
        return;
    }

    fetch->endpc = fetch->startpc + ftb_entry->ft_len;
    fetch->jmpinfo = ftb_entry->jmpinfo;
    FetchFlagT jmp = ftb_entry->jmpinfo;
    if(jmp & FETCH_FLAG_JAL) {
        fetch->jmptarget = ftb_entry->jaltarget;
    }
    fetch->branchs = ftb_entry->branchs;

    if(!ftb_entry->branchs.empty() && !ftb_entry->always_taken) {
        // TAGE-SC
        // 根据 totalSum 以及 sc_bank_thres 决定最终预测方向
        // totalSum > 0 且绝对值超过阈值则跳转： 如果 scCtrSum > sc_bank_thres - tageCtrCentered
        // totalSum < 0 且绝对值超过阈值则不跳转： 如果 scCtrSum < -sc_bank_thres - tageCtrCentered 
        std::array<int8_t, XSIFU_BRANCH_CNT> tage_ctr = pred_tage(fetch->startpc);
        std::array<int16_t, XSIFU_BRANCH_CNT> sc_ctr = pred_sc(fetch->startpc);
        std::array<int32_t, XSIFU_BRANCH_CNT> sum_ctr;
        for(int i = 0; i < XSIFU_BRANCH_CNT; i++) {
            sum_ctr[i] = 8 * (2 * (int32_t)(tage_ctr[i]) + 1) + ((int32_t)(sc_ctr[i]));
        }
        for(int i = 0; i < ftb_entry->branchs.size(); i++) {
            if(sum_ctr[i] >= 0 && sum_ctr[i] > sc_bank_thres[i]) {
                fetch->branchs[i].pred_taken = sum_ctr[i];
            }
            else if(sum_ctr[i] < 0 && sum_ctr[i] < -sc_bank_thres[i]) {
                fetch->branchs[i].pred_taken = sum_ctr[i];
            }
            else {
                fetch->branchs[i].pred_taken = ((int16_t)(tage_ctr[i]) * 2 + 1);
            }
        }
    }   

    for(auto &br : fetch->branchs) {
        bhr.push(br.pred_taken >= 0);
        if(br.pred_taken >= 0) {
            break;
        }
    }

    if(jmp & FETCH_FLAG_CALL) {
        fetch->commit_ras = true;
        pred_ras_call(fetch->endpc);
    }
    if(jmp & FETCH_FLAG_JALR) {
        if(jmp & FETCH_FLAG_RET) {
            if(fetch->jmptarget = pred_ras_ret()) fetch->commit_ras = true;
        }
        if(fetch->jmptarget == 0) {
            auto ittage_res = pred_ittage(fetch->startpc);
            if(ittage_res.u > 0) {
                fetch->jmptarget = ittage_res.jmptarget;
            }
        }
    }

    if(debug_ofile) {
        sprintf(log_buf ,"%ld:P2: Pred @0x%lx, %ld brs, jmp:0x%x", simroot::get_current_tick(), fetch->startpc, fetch->branchs.size(), fetch->jmpinfo);
        string str(log_buf);
        if(!fetch->branchs.empty()) {
            str += ", brpred:";
            for(auto &br : fetch->branchs) {
                str += ((br.pred_taken)?("1 "):("0 "));
            }
        }
        if(fetch->jmpinfo) {
            sprintf(log_buf, ", jmppc 0x%lx", fetch->jmptarget);
        }
        simroot::log_line(debug_ofile, str);
    }
}

void BPU::do_update_br() {
    simroot_assert(update_reqs.front().type == UpdateBPUField::br);

    auto &p = update_reqs.front();
    uint32_t brcnt = std::min(p.branchs.size(), p.is_taken.size());

    if(debug_ofile) {
        sprintf(log_buf, "%ld:UPDATE-BR: @0x%lx len %d, pred ", simroot::get_current_tick(), p.startpc, p.ft_len);
        string str(log_buf);
        for(auto &br : p.branchs) {
            str += ((br.pred_taken)?("1 "):("0 "));
        }
        str += ", commit ";
        for(auto t : p.is_taken) {
            str += (t?("1 "):("0 "));
        }
        simroot::log_line(debug_ofile, str);
    }

    FTBEntry *fetch = nullptr;
    if(ftb->get_line(p.startpc >> 1, &fetch, true)) {
        fetch->branchs = p.branchs;
        for(int i = 0; i < brcnt; i++) {
            if(p.is_taken[i]) {
                INC(fetch->branchs[i].pred_taken, 1);
            }
            else {
                DEC(fetch->branchs[i].pred_taken, -2);
            }
        }
        fetch->always_taken = false;
    }
    if(ubtb->get_line((p.startpc >> 1) & ((1UL << ubtb_tag_len) - 1UL), &fetch, true)) {
        fetch->branchs = p.branchs;
        for(int i = 0; i < brcnt; i++) {
            if(p.is_taken[i]) {
                INC(fetch->branchs[i].pred_taken, 1);
            }
            else {
                DEC(fetch->branchs[i].pred_taken, -2);
            }
        }
        fetch->always_taken = false;
    }

    std::array<int8_t, XSIFU_BRANCH_CNT> tage_ctr = update_tage();
    std::array<int16_t, XSIFU_BRANCH_CNT> sc_ctr = update_sc();
    std::array<int32_t, XSIFU_BRANCH_CNT> sum_ctr;
    for(int i = 0; i < XSIFU_BRANCH_CNT; i++) {
        sum_ctr[i] = 8 * (2 * (int32_t)(tage_ctr[i]) + 1) + ((int32_t)(sc_ctr[i]));
    }

    for(int i = 0; i < brcnt; i++) {
        bool sum_taken = (sum_ctr[i] > 0 && sum_ctr[i] > sc_bank_thres[i]);
        bool sum_notaken = (sum_ctr[i] < 0 && sum_ctr[i] < -sc_bank_thres[i]);
        bool update = (std::abs(sum_ctr[i]) >= sc_bank_thres[i] - 4 && std::abs(sum_ctr[i]) <= sc_bank_thres[i] - 2);
        // TAGE-SC采用了预测结果P（即TAGE+SC后的预测结果），如果 |totalSum| 在 [sc_bank_thres -4, sc_bank_thres -2] 的范围内，则对阈值相关寄存器组进行更新
        // 更新 sc_bank_ctr，饱和计数 若预测正确，则 sc_bank_ctr +=1 若预测错误，则 sc_bank_ctr -=1
        // 更新 sc_bank_thres ，受限制的饱和运算， 若 sc_bank_ctr 更新后的值已达 0b11111 且 sc_bank_thres <= 31，则 sc_bank_thres +=2 若 sc_bank_ctr 更新后的值为 0 且 sc_bank_thres >=6，则 sc_bank_thres -=2 其余情况thres不变。
        // sc_bank_thres 更新判断结束后，会对 sc_bank_ctr 再做一次判断 若更新后的sc_bank_ctr若为0b11111或0，则thres_ctr会被置回初始值0b10000。
        if(update) {
            if((sum_taken && p.is_taken[i]) || (sum_notaken && !p.is_taken[i])) {
                INC(sc_bank_ctr[i], 31);
            }
            else {
                DEC(sc_bank_ctr[i], 0);
            }
            if(sc_bank_ctr[i] >= 31) { INC(sc_bank_thres[i], 31);INC(sc_bank_thres[i], 31);sc_bank_ctr[i]=16; }
            else if(sc_bank_ctr[i] <= 0) { DEC(sc_bank_thres[i], 0);DEC(sc_bank_thres[i], 0);sc_bank_ctr[i]=16; }
        }
    }
}

void BPU::do_update_ftb() {
    simroot_assert(update_reqs.front().type == UpdateBPUField::ftb);
    auto &p = update_reqs.front();

    if(debug_ofile) {
        sprintf(log_buf, "%ld:UPDATE-FTB: @0x%lx len %d, %ld brs, jmp type %d target 0x%lx",
            simroot::get_current_tick(),
            p.startpc, p.ft_len, p.branchs.size(), (int)(p.jmpinfo), p.jmptarget
        );
        simroot::log_line(debug_ofile, log_buf);
    }

    FTBEntry entry;
    entry.ft_len = p.ft_len;
    entry.jmpinfo = p.jmpinfo;
    entry.jaltarget = ((p.jmpinfo & FETCH_FLAG_JAL)?p.jmptarget:0);
    entry.always_taken = true;
    entry.branchs = p.branchs;
    ftb->insert_line(ftb_idx(p.startpc), &entry, nullptr, nullptr);
    ubtb->insert_line(ubtb_idx(p.startpc), &entry, nullptr, nullptr);
}

void BPU::do_update_jalr() {
    simroot_assert(update_reqs.front().type == UpdateBPUField::jalr);
    FTBEntry *entry = nullptr;
    VirtAddrT pc = update_reqs.front().startpc;
    auto &p = update_reqs.front();

    if(debug_ofile) {
        sprintf(log_buf, "%ld:UPDATE-JALR: @0x%lx len %d, jmp type %d target 0x%ld",
            simroot::get_current_tick(),
            p.startpc, p.ft_len, (int)(p.jmpinfo), p.jmptarget
        );
        simroot::log_line(debug_ofile, log_buf);
    }

    // if(ftb->get_line(pc >> 1, &entry, true)) {
    //     entry->jmpinfo = p.jmpinfo;
    //     entry->jaltarget = p.jmptarget;
    // }
    if(p.commit_ras) {
        if(p.jmpinfo & FETCH_FLAG_CALL) {
            commit_ras_call(p.ras);
        }
        else if(p.jmpinfo & FETCH_FLAG_RET) {
            commit_ras_ret(p.ras, p.jmptarget);
        }
    }
    update_ittage();
}


// ------------ TAGE --------------

inline bool _tage_weak_pred(int8_t v) {
    return (v == 0 || v == -1);
}

void BPU::_tage_indexing(BrHist &brhist, VirtAddrT startpc, TageIndexRes* res) {
    VirtAddrT branchpc = (startpc >> 1);
    memset(res, 0, sizeof(TageIndexRes));

#define LOWBIT(num, len) ((num) & ((1UL<<(len))-1UL))
    res->index0 = LOWBIT(branchpc, tage0_set_bit);
    for(int i = 0; i < XS_TAGE_NUM; i++) {
        res->index[i] = LOWBIT(branchpc ^ brhist.get(tage_hist_len[i], tage_fh0_len[i]), tage_set_bits[i]);
        res->tag[0] = LOWBIT(branchpc ^ brhist.get(tage_hist_len[i], tage_fh1_len[i]) ^ (brhist.get(tage_hist_len[i], tage_fh2_len[i]) << 1), tage_tag_len[i]);
    }
    // res->index[0] = LOWBIT(branchpc ^ brhist.get(8, 8), param->tage_set_bits);
    // res->index[1] = LOWBIT(branchpc ^ brhist.get(13, 11), param->tage_set_bits);
    // res->index[2] = LOWBIT(branchpc ^ brhist.get(32, 11), param->tage_set_bits);
    // res->index[3] = LOWBIT(branchpc ^ brhist.get(119, 11), param->tage_set_bits);
    // res->tag[0] = LOWBIT(branchpc ^ brhist.get(8, 8) ^ (brhist.get(8, 7) << 1), 8);
    // res->tag[1] = LOWBIT(branchpc ^ brhist.get(13, 8) ^ (brhist.get(13, 7) << 1), 8);
    // res->tag[2] = LOWBIT(branchpc ^ brhist.get(32, 8) ^ (brhist.get(32, 7) << 1), 8);
    // res->tag[3] = LOWBIT(branchpc ^ brhist.get(119, 8) ^ (brhist.get(119, 7) << 1), 8);
#undef LOWBIT
    res->res0 = &tage_t0[res->index0];
    res->use_alt = &use_alt_cnt[res->index0];
    for(int i = 0; i < XS_TAGE_NUM; i++) {
        auto iter = tage[i][res->index[i]].find(res->tag[i]);
        if(iter != tage[i][res->index[i]].end()) res->res[i] = &(iter->second);
    }
}

void BPU::_alloc_tage_entry(uint32_t tn, TageIndexRes* res, vector<bool> &taken) {
    simroot_assert(tn < XS_TAGE_NUM);
    // 在每次需要分配表时，进行动态重置usefulness标志位 
    // 使用 7bit 的 bankTickCtrs 寄存器，并计算 
    // 可分配的表数 a（历史长度比当前更长，且对应索引的useful为 0 ）
    // 不可分配的表数 b（历史长度比当前更长，且对应索引的useful 不为 0 ）
    vector<std::pair<uint32_t, uint64_t>> can_free; // <tn, tag>
    int32_t cannot_free_cnt = 0;
    for(int i = tn; i < XS_TAGE_NUM; i++) {
        unordered_map<uint64_t, TageEntry>* row = &(tage[tn][res->index[tn]]);
        for(auto &entry : *row) {
            if(entry.second.u == 0) can_free.push_back(make_pair(tn, entry.first));
            else cannot_free_cnt++;
        }
    }
    int32_t delta = can_free.size() - cannot_free_cnt;
    tage_bank_tick_ctr += delta;
    if(tage_bank_tick_ctr < 0) tage_bank_tick_ctr = 0;
    else if(tage_bank_tick_ctr >= tage_bank_tick_ctr_max_value) {
        tage_bank_tick_ctr = 0;
        for(int i = 0; i < XS_TAGE_NUM; i++) {
            for(auto &row : tage[i]) {
                for(auto &entry : row) {
                    entry.second.u = 0;
                }
            }
        }
    }

    if(can_free.empty()) return ;

    uint32_t select = RAND(0, can_free.size());
    tn = can_free[select].first;
    auto &row = tage[tn][res->index[tn]];
    row.erase(can_free[select].second);
    TageEntry tmp;
    memset(&tmp, 0, sizeof(TageEntry));
    tmp.u = 0;
    for(int i = 0; i < std::min<int>(taken.size(), XSIFU_BRANCH_CNT); i++) {
        tmp.pred[i] = ((taken[i])?0:-1);
    }
    auto iter = row.emplace(res->tag[tn], tmp);
    return ;
}

std::array<int8_t, XSIFU_BRANCH_CNT> BPU::_tage_result_of(const TageIndexRes &res) {
    std::array<int8_t, XSIFU_BRANCH_CNT> ret;
    ret.fill(0);

    bool hit = false;
    int tn = 0;
    for(int i = 0; i < XSIFU_BRANCH_CNT; i++) if(res.res[i]) {
        hit = true;
        tn = i;
    }

    bool uset0 = !hit;
    if(hit && *res.use_alt >= use_alt_cnt_threshold) {
        for(int i = 0; i < XSIFU_BRANCH_CNT; i++)
            if(_tage_weak_pred(res.res[tn]->pred[i])) uset0 = true;
    }

    for(int i = 0; i < XSIFU_BRANCH_CNT; i++) {
        ret[i] = (uset0?(res.res0->at(i)):(res.res[tn]->pred[i]));
    }

    return ret;
}

std::array<int8_t, XSIFU_BRANCH_CNT> BPU::pred_tage(VirtAddrT startpc) {
    TageIndexRes res;
    _tage_indexing(bhr, startpc, &res);
    return _tage_result_of(res);
}

std::array<int8_t, XSIFU_BRANCH_CNT> BPU::update_tage() {
    simroot_assert(update_reqs.front().type == UpdateBPUField::br);
    TageIndexRes res;
    auto &p = update_reqs.front();
    _tage_indexing(p.bhrbak, update_reqs.front().startpc, &res);
    simroot_assert(res.res0 && res.use_alt);
    auto ret = _tage_result_of(res);

    // T0始终更新：发生跳转（即taken）则 pc 索引的 ctr 饱和计数器 +1，否则 -1
    for(int i = 0; i < p.is_taken.size(); i++) {
        if(p.is_taken[i]) {
            INC(res.res0->at(i), 1);
        }
        else {
            DEC(res.res0->at(i), -2);
        }
    }

    if(!res.res[0] && !res.res[1] && !res.res[2] && !res.res[3]) {
        // 在只有T0命中时，进行如下操作
        //  T0预测正确则不额外更新
        //  T0预测错误则尝试随机在Tn中的某个表申请一个新表项 申请新表项时需要对应index位置的原表项useful为0 新表项默认是弱预测，useful为0，并设置其tag为计算出来的新tag
        for(int i = 0; i < p.is_taken.size(); i++) {
            if(BOEQ(p.is_taken[i], res.res0->at(i) >= 0)) continue;
            _alloc_tage_entry(0, &res, p.is_taken);
        }
    }
    else {
        // 在T0和Tn同时命中时，进行如下操作
        // Tn 始终更新：taken 则 pc 索引的 ctr 饱和计数器 +1，否则 -1 需要注意，”命中“ 表示 index 索引到的表项的 tag 要和计算出的 tag 匹配
        int tn = 0;
        for(int t = 0; t < XS_TAGE_NUM; t++) {
            if(res.res[t]) tn = t;
            else continue;
            for(int i = 0; i < p.is_taken.size(); i++) {
                if(p.is_taken[i]) {
                    INC(res.res[t]->pred[i], 7);
                }
                else {
                    DEC(res.res[t]->pred[i], -8);
                }
            }
        }
        // 若T0和Tn结果相同
        //  预测错误则尝试在比Tn对应历史更长的表中随机申请一个新表项 申请新表项时需要对应index位置的原表项useful为0 新表项默认是弱预测，useful为0，tag设置为用新历史信息计算出来的tag
        bool t0_eq_tn = true, t0_eq_taken = true, tn_eq_taken = true, tn_weak = true;
        for(int i = 0; i < p.is_taken.size(); i++) {
            t0_eq_tn = (t0_eq_tn && BOEQ(res.res0->at(i) >= 0, res.res[tn]->pred[i] >= 0));
            t0_eq_taken = (t0_eq_taken && BOEQ(res.res0->at(i) >= 0, p.is_taken[i]));
            tn_eq_taken = (tn_eq_taken && BOEQ(p.is_taken[i], res.res[tn]->pred[i] >= 0));
            tn_weak = (tn_weak && !_tage_weak_pred(res.res[tn]->pred[i]));
        }
        if(t0_eq_tn && !t0_eq_taken && tn < (XS_TAGE_NUM - 1)) {
            _alloc_tage_entry(tn+1, &res, p.is_taken);
        }
        // 若T0和Tn结果不同
        //  若Tn正确则表项 useful +1  若结果还同时为弱预测，则选用T0的替代预测计数器 -1
        //  若Tn错误则表项 useful -1，同时如 3.b.ii 在更长历史表中申请新表项 
        //   若结果还同时为弱预测，则选用T0的替代预测计数器 +1
        if(!t0_eq_tn) {
            if(tn_eq_taken) {
                INC(res.res[tn]->u, 3);
                if(tn_weak) {
                    DEC(*res.use_alt, -8);
                }
            }
            else {
                DEC(res.res[tn]->u, 0);
                if(tn < (XS_TAGE_NUM - 1)) _alloc_tage_entry(tn+1, &res, p.is_taken);
                if(tn_weak) {
                    INC(*res.use_alt, 7);
                }
            }
        }
    }

    return ret;
}

// ---------- SC -------------

inline bool _sc_weak_pred(int16_t v) {
    return (v < 5 && v > -6);
}

void BPU::_sc_indexing(BrHist &brhist, VirtAddrT startpc, SCIndexRes* res) {
    VirtAddrT branchpc = (startpc >> 1);
    memset(res, 0, sizeof(SCIndexRes));

#define LOWBIT(num, len) ((num) & ((1UL<<(len))-1UL))
    for(int i = 0; i < XS_SC_NUM; i++) {
        res->index[i] = LOWBIT(branchpc ^ brhist.get(sc_fh_len[i], sc_set_bits[i]), sc_set_bits[i]);
        res->res[i] = &sc[i][res->index[i]];
    }
#undef LOWBIT
}

std::array<int16_t, XSIFU_BRANCH_CNT> BPU::_sc_result_of(const SCIndexRes &res) {
    std::array<int16_t, XSIFU_BRANCH_CNT> ret;
    ret.fill(0);
    for(int i = 0; i < XS_SC_NUM; i++) {
        for(int b = 0; b < XSIFU_BRANCH_CNT; b++) {
            ret[b] += ((int16_t)(res.res[i]->at(b)) * 2 + 1);
        }
    }
    return ret;
}

std::array<int16_t, XSIFU_BRANCH_CNT> BPU::pred_sc(VirtAddrT startpc) {
    SCIndexRes res;
    _sc_indexing(bhr, startpc, &res);
    return _sc_result_of(res);
}

std::array<int16_t, XSIFU_BRANCH_CNT> BPU::update_sc() {
    simroot_assert(update_reqs.front().type == UpdateBPUField::br);
    SCIndexRes res;
    auto &p = update_reqs.front();
    _sc_indexing(p.bhrbak, update_reqs.front().startpc, &res);
    auto ret = _sc_result_of(res);

    for(int n = 0; n < XS_SC_NUM; n++) {
        for(int i = 0; i < p.is_taken.size(); i++) {
            if(p.is_taken[i]) {
                INC(res.res[n]->at(0), 63);
            }
            else {
                DEC(res.res[n]->at(0), -64);
            }
        }
    }

    return ret;
}

// ------------- ITTAGE --------------

void BPU::_ittage_indexing(BrHist &brhist, VirtAddrT startpc, ITTageIndexRes* res) {
    VirtAddrT branchpc = (startpc >> 1);
    memset(res, 0, sizeof(ITTageIndexRes));

#define LOWBIT(num, len) ((num) & ((1UL<<(len))-1UL))
    for(int i = 0; i < XS_ITTAGE_NUM; i++) {
        res->index[i] = LOWBIT(branchpc ^ brhist.get(ittage_hist_len[i], ittage_fh0_len[i]), ittage_set_bits[i]);
        res->tag[0] = LOWBIT(branchpc ^ brhist.get(ittage_hist_len[i], ittage_fh1_len[i]) ^ (brhist.get(ittage_hist_len[i], ittage_fh2_len[i]) << 1), ittage_tag_len[i]);
    }
#undef LOWBIT
    for(int i = 0; i < XS_ITTAGE_NUM; i++) {
        auto iter = ittage[i][res->index[i]].find(res->tag[i]);
        if(iter != ittage[i][res->index[i]].end()) res->res[i] = &(iter->second);
    }
}

BPU::ITTageEntry BPU::_ittage_result_of(const ITTageIndexRes &res) {
    for(int i = XS_ITTAGE_NUM - 1; i >= 0; i--) {
        if(res.res[i] && res.res[i]->u) {
            return *(res.res[i]);
        }
    }
    return ITTageEntry();
}

void BPU::_alloc_ittage_entry(uint32_t tn, ITTageIndexRes* res, VirtAddrT target) {
    simroot_assert(tn < XS_ITTAGE_NUM);
    // 在每次需要分配表时，进行动态重置usefulness标志位 
    // 使用 7bit 的 bankTickCtrs 寄存器，并计算 
    // 可分配的表数 a（历史长度比当前更长，且对应索引的useful为 0 ）
    // 不可分配的表数 b（历史长度比当前更长，且对应索引的useful 不为 0 ）
    vector<std::pair<uint32_t, uint64_t>> can_free; // <tn, tag>
    int32_t cannot_free_cnt = 0;
    for(int i = tn; i < XS_ITTAGE_NUM; i++) {
        unordered_map<uint64_t, ITTageEntry>* row = &(ittage[tn][res->index[tn]]);
        for(auto &entry : *row) {
            if(entry.second.u == 0) can_free.push_back(make_pair(tn, entry.first));
            else cannot_free_cnt++;
        }
    }
    int32_t delta = can_free.size() - cannot_free_cnt;
    ittage_tick_ctr += delta;
    if(ittage_tick_ctr < 0) ittage_tick_ctr = 0;
    else if(ittage_tick_ctr >= ittage_tick_ctr_max_value) {
        ittage_tick_ctr = 0;
        for(int i = 0; i < XS_ITTAGE_NUM; i++) {
            for(auto &row : ittage[i]) {
                for(auto &entry : row) {
                    entry.second.u = 0;
                }
            }
        }
    }

    if(can_free.empty()) return ;

    uint32_t select = RAND(0, can_free.size());
    tn = can_free[select].first;
    auto &row = ittage[tn][res->index[tn]];
    row.erase(can_free[select].second);
    ITTageEntry tmp;
    memset(&tmp, 0, sizeof(ITTageEntry));
    tmp.u = 1;
    tmp.jmptarget = target;
    auto iter = row.emplace(res->tag[tn], tmp);
    return ;
}

BPU::ITTageEntry BPU::pred_ittage(VirtAddrT startpc) {
    ITTageIndexRes res;
    _ittage_indexing(bhr, startpc, &res);
    return _ittage_result_of(res);
}


void BPU::update_ittage() {
    simroot_assert(update_reqs.front().type == UpdateBPUField::jalr);
    ITTageIndexRes res;
    auto &p = update_reqs.front();
    _ittage_indexing(p.bhrbak, p.startpc, &res);
    
    list<int32_t> hit_tn;
    for(int i = 0; i < XS_ITTAGE_NUM; i++) {
        if(res.res[i]) hit_tn.push_front(i);
    }

    // 若预测地址与实际一致，则将对应provider（提供原始预测数据的表）表项的ctr计数器自增1
    // 若预测地址与实际不一致，则将对应provider表项的ctr计数器自减1
    // 如果多个表均命中，更新时始终更新历史最长的表，如果采用了替代预测，也同时更新替代预测表。
    // 当待更新表项在进行本次预测时的ctr为0时，直接将实际的最终跳转结果存入target，覆盖
    // 如果是在申请新表项，直接将实际的最终跳转结果存入target
    auto update_ittage_entry = [](ITTageEntry *p, VirtAddrT target) -> void {
        if(p->jmptarget == target) {
            INC(p->u, 3);
        }
        else {
            if(p->u == 0) {
                p->jmptarget = target;
                p->u = 1;
            }
            else {
                DEC(p->u, 0);
            }
        }
    };
    if(!hit_tn.empty()) {
        int tn = hit_tn.front();
        if(res.res[tn]->u == 0 && hit_tn.size() > 1) {
            for(auto n : hit_tn) {
                if(res.res[n]->u) {
                    update_ittage_entry(res.res[n], p.jmptarget);
                }
            }
        }
        bool correct = (res.res[tn]->jmptarget == p.jmptarget);
        update_ittage_entry(res.res[tn], p.jmptarget);
        if(!correct && tn < XS_ITTAGE_NUM - 1) {
            _alloc_ittage_entry(tn + 1, &res, p.jmptarget);
        }
    }

    // 当provider预测正确而替代预测错误时provider的usefulness置1
    // 如果替代预测是弱信心，并且预测正确，则provider的usefulness置1。 如果替代预测是弱信心，并且预测错误，则provider的usefulness置0。
}


// --------------- RAS --------------------

void BPU::pred_ras_call(VirtAddrT nextpc) {
    ras.push(nextpc);
}

VirtAddrT BPU::pred_ras_ret() {
    return ras.pop();
}

void BPU::commit_ras_call(RASSnapShot &ptrs) {
    
}

void BPU::commit_ras_ret(RASSnapShot &ptrs, VirtAddrT nextpc) {
    
}

// void BPU::pred_ras_call(VirtAddrT nextpc) {
//     // Push操作：当前被预测的块还没有commit，所以只能在预测栈上进行push，具体操作为链式栈Push过程
//     uint32_t newptr = ras.tosw;
//     ras.tosw = (ras.tosw + 1) % ras_ps_size;
//     auto &entry = ras_ps[newptr];
//     entry.jmptarget = nextpc;
//     entry.next = ras.tosr;
//     ras.tosr = newptr;
// }

// VirtAddrT BPU::pred_ras_ret() {
//     // Pop操作：首先通过“预测栈”是否为空，来判断当前栈顶是在预测栈上，还是在提交栈上。然后获取对应的栈顶元素，按照“栈”结构自己对应的方法进行pop。链式结构，TOSR按索引指向上一个元素，普通结构，ssp-=1
//     VirtAddrT ret = 0;
//     if(ras.bos == ras.tosr) {
//         ret = ras_cs[ras.ssp].jmptarget;
//         ras.ssp = ((ras.ssp == 0)?(ras_cs_size - 1):(ras.ssp - 1));
//     }
//     else {
//         ret = ras_ps[ras.tosr].jmptarget;
//         ras.tosr = ras_ps[ras.tosr].next;
//     }
//     return ret;
// }

// void BPU::commit_ras_call(RASSnapShot &ptrs) {
//     // 预测块为call正确：需要进行push操作，需要压栈的元素消息（预测地址）来自“预测栈”，具体位置信息POS由commit消息提供。完成提交栈的压栈操作后，对预测栈的栈低指针BOS进行更新，设置BOS=POS
//     uint32_t pos = ptrs.tosw; // Fetch中的ras快照是push前，因此那次push的位置是新元素指针tosw
//     ras.bos = pos;
//     ras.ssp = (ras.ssp + 1) % ras_cs_size;
//     ras_cs[ras.ssp].jmptarget = ras_ps[pos].jmptarget;
//     ras_ps[pos].next = pos; // 防止飞指针
// }

// void BPU::commit_ras_ret(RASSnapShot &ptrs, VirtAddrT nextpc) {
//     // 预测块为ret正确：需要进行pop操作。在call/ret序列中，肯定是先call后ret，所以此时ret对应的call时压栈的元素肯定在提交栈中，所以直接从commit消息中获取提交栈顶指针nsp，进行nsp -= 1操作
//     if(ras_cs[ras.ssp].jmptarget == nextpc) {
//         ras.ssp = ((ras.ssp == 0)?(ras_cs_size - 1):(ras.ssp - 1));
//     }
// }


}}

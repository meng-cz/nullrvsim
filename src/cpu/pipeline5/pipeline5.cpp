
#include <assert.h>

#include "pipeline5.h"

#include "cpu/isa.h"
#include "cpu/operation.h"
#include "cpu/csr.h"

#include "simroot.h"
#include "configuration.h"

using isa::RV64OPCode;

namespace simcpu {

namespace cpup5 {

void PipeLine5CPU::init_all() {

    if(conf::get_int("pipeline5", "log_pipeline_to_stdout", 0)) {
        log_info = true;
        sprintf(log_buf, "CPU %d Inited", cpu_id);
        simroot::print_log_info(log_buf);
    }
    else {
        log_info = false;
    }
    log_regs = conf::get_int("pipeline5", "log_register", 0);
    log_fregs = conf::get_int("pipeline5", "log_fp_register", 0);
    if(conf::get_int("pipeline5", "log_inst_to_file", 0)) {
        sprintf(log_buf, "CPU%d_log.txt", cpu_id);
        log_file_commited_inst = new std::ofstream(log_buf);
    }

    clear_pipeline();
}

PipeLine5CPU::~PipeLine5CPU() {
    if(log_file_commited_inst) {
        log_file_commited_inst->close();
        delete log_file_commited_inst;
    }
}

void PipeLine5CPU::process_pipeline_1_fetch() {
    
    if(p1_result.first || p1_wait_for_jump) {
        // Stalled
        return ;
    }

    bool icache_fetched = false;
    InstRaw &inst = p1_result.second;
    inst.pc = pc;
    p1_apply_next_pc = pc;

    io_icache_port->arrival_line(nullptr); // 用不到，清空icache的到达记录

    if (io_sys_port->is_dev_mem(cpu_id, pc)) {
        icache_fetched = true;
        io_sys_port->dev_input(cpu_id, pc, 4, &(inst.inst_raw));
    }
    else {
        VirtAddrT vpc = pc, vpc2 = pc+2;
        PhysAddrT ppc = 0, ppc2 = 0;
        SimError res1 = SimError::success, res2 = SimError::success;
        if((res1 = io_sys_port->v_to_p(cpu_id, vpc, &ppc, PGFLAG_X)) == SimError::success &&
            (res2 = io_sys_port->v_to_p(cpu_id, vpc2, &ppc2, PGFLAG_X)) == SimError::success
        ) {
            res1 = io_icache_port->load(ppc, 2, &(inst.inst_raw), false);
            cache_operation_result_check_error(res1, vpc, vpc, ppc, 2);
            if(res1 == SimError::success) {
                res2 = io_icache_port->load(ppc2, 2, ((RVCInstT*)(&(inst.inst_raw))) + 1, false);
                cache_operation_result_check_error(res2, vpc, vpc2, ppc2, 2);
                if(res2 == SimError::success) {
                    icache_fetched = true;
                }
            }
        }
        else {
            // Access Illeagle Address
            if(p2_workload.first || p3_workload.first || p4_workload.first || p5_workload.first) {
                // Maybe a jar in pipeline
                return;
            }
            else {
                if(res1 == SimError::unaccessable || res2 == SimError::unaccessable) 
                    sprintf(log_buf, "CPU%d ICache Access Unexecutable V-Page 0x%lx", cpu_id, pc);
                else
                    sprintf(log_buf, "CPU%d ICache Access Unknown V-Address 0x%lx", cpu_id, pc);
                LOG(ERROR) << log_buf;
                simroot_assert(0);
            }
        }
    }

    if(!icache_fetched) {
        if(log_info) {
            sprintf(log_buf, "CPU%d P1: ICache wait @0x%lx", cpu_id, pc);
            simroot::print_log_info(log_buf);
        }
    }
    else {
        LineAddrT prefetch_addr = addr_to_line_addr(pc) + CACHE_LINE_LEN_BYTE;
        PhysAddrT prefetch_paddr;
        // if(io_sys_port->v_to_p(cpu_id, prefetch_addr, &prefetch_paddr)) {
        //     io_icache_port->prefetch(prefetch_paddr);
        // }

        int32_t target = 0;
        bool isj = isa::pdec_isJ(inst.inst_raw);
        bool isji = isa::pdec_get_J_target(inst.inst_raw, &target);
        bool isc = isa::isRVC(inst.inst_raw);
        if(isj) {
            if(isji) {
                p1_apply_next_pc = pc + target;
                p1_result.first = true;
            }
            else {
                p1_result.first = true;
                p1_wait_for_jump = true;
            }
        }
        else if(isa::pdec_isifence(inst.inst_raw)) {
            assert(0);
            p1_apply_next_pc = pc + 4;
        }
        else {
            p1_result.first = true;
            // update pc
            if(isc) {
                p1_apply_next_pc = pc + 2;
                if(log_info) {
                    sprintf(log_buf, "CPU%d P1: Fetch Instruction @0x%lx 0x%4x", cpu_id, inst.pc, inst.inst_raw & 0xffff);
                    simroot::print_log_info(log_buf);
                }
            }
            else {
                p1_apply_next_pc = pc + 4;
                if(log_info) {
                    sprintf(log_buf, "CPU%d P1: Fetch Instruction @0x%lx 0x%8x", cpu_id, inst.pc, inst.inst_raw);
                    simroot::print_log_info(log_buf);
                }
            }
        }
    }
}

void PipeLine5CPU::process_pipeline_2_decode() {
    if(!(p2_workload.first)) {
        return;
    }
    if(p2_result.first) {
        // Stalled
        return;
    }
    {
        // Stalled when a unique inst is in pipeline
        if(p3_workload.first && p3_workload.second.inst.is_unique()) return;
        if(p4_workload.first && p4_workload.second.inst.is_unique()) return;
        if(p5_workload.first && p5_workload.second.inst.is_unique()) return;
    }

    InstRaw raw = p2_workload.second;
    P5InstDecoded &p5dec = p2_result.second;
    RV64InstDecoded &dec = p2_result.second.inst;
    if(!isa::decode_rv64(raw.inst_raw, &(dec))) {
        sprintf(log_buf, "UNKOWN INST @0x%lx : 0x%x", raw.pc, raw.inst_raw);
        LOG(ERROR) << string(log_buf);
        simroot_assert(0);
    }
    if(dec.is_unique()) {
        if(p3_workload.first || p4_workload.first || p5_workload.first) {
            // Flush Pipeline
            return;
        }
    }
    dec.pc = raw.pc;

    if(log_file_commited_inst || log_info) {
        isa::init_rv64_inst_name_str(&dec);
    }

    if(log_info) {
        sprintf(log_buf, "CPU%d P2: Decode Instruction %s", cpu_id, dec.debug_name_str.c_str());
        simroot::print_log_info(log_buf);
    }

    p5dec.passp3 = (
        dec.opcode == RV64OPCode::lui ||
        dec.opcode == RV64OPCode::miscmem ||
        dec.opcode == RV64OPCode::system
    );

    p5dec.passp4 = !(
        dec.opcode == RV64OPCode::storefp ||
        dec.opcode == RV64OPCode::store ||
        dec.opcode == RV64OPCode::loadfp ||
        dec.opcode == RV64OPCode::load ||
        dec.opcode == RV64OPCode::amo
    );

    p5dec.err = SimError::success;

    p5dec.mem_start_tick = p5dec.mem_finish_tick = 0;
    p5dec.cache_missed = false;

    p2_result.first = true;
};

void PipeLine5CPU::process_pipeline_3_execute() {
    
    if(!(p3_workload.first)) {
        return;
    }

    if(p3_result.first) {
        // Stalled 
        return;
    }

    p3_result.second = p3_workload.second;
    P5InstDecoded &p5inst = p3_result.second;
    RV64InstDecoded &inst = p3_result.second.inst;

    RVRegIndexT rd1 = inst.rd;
    RVRegIndexT rs1 = inst.rs1;
    RVRegIndexT rs2 = inst.rs2;
    RVRegIndexT rs3 = inst.rs3;

    if(log_info) {
        sprintf(log_buf, "CPU%d P3: @0x%lx, %s, rd-rs3: %d %d %d %d", cpu_id, inst.pc, inst.debug_name_str.c_str(), rd1, rs1, rs2, rs3);
        simroot::print_log_info(log_buf);
    }

    if(inst.opcode == RV64OPCode::auipc) {
        RAW_DATA_AS(p5inst.arg0).i64 = inst.pc + RAW_DATA_AS(inst.imm).i64;
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::lui) {
        p5inst.arg0 = inst.imm;
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::jal) {
        p5inst.arg1 = inst.pc + RAW_DATA_AS(inst.imm).i64;
        p5inst.arg0 = inst.pc + ((inst.is_rvc())?2:4);
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::jalr) {
        if(iregs.blocked(rs1)) return;
        p5inst.arg1 = iregs.getu(rs1) + RAW_DATA_AS(inst.imm).i64;
        p5inst.arg0 = inst.pc + ((inst.is_rvc())?2:4);
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::branch) {
        if(iregs.blocked(rs1) || iregs.blocked(rs2)) return;
        p5inst.arg1 = inst.pc + RAW_DATA_AS(inst.imm).i64;
        assert(SimError::success == isa::perform_branch_op_64(inst.param.branch, &p5inst.arg0, iregs.get(rs1), iregs.get(rs2)));
    }
    else if(inst.opcode == RV64OPCode::amo) {
        if(iregs.blocked(rs1)) return;
        if(inst.param.amo.op != RV64AMOOP5::LR && iregs.blocked(rs2)) return;
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::load || inst.opcode == RV64OPCode::loadfp) {
        if(iregs.blocked(rs1)) return;
        p5inst.arg0 = iregs.getu(rs1) + RAW_DATA_AS(inst.imm).i64;
        if(inst.opcode == RV64OPCode::load) iregs.block(rd1);
        else if(inst.opcode == RV64OPCode::loadfp) fregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::store) {
        if(iregs.blocked(rs1) || iregs.blocked(rs2)) return;
        p5inst.arg0 = iregs.getu(rs1) + RAW_DATA_AS(inst.imm).i64;
        p5inst.arg1 = iregs.getu(rs2);
    }
    else if(inst.opcode == RV64OPCode::storefp) {
        if(iregs.blocked(rs1) || fregs.blocked(rs2)) return;
        p5inst.arg0 = iregs.getu(rs1) + RAW_DATA_AS(inst.imm).i64;
        p5inst.arg1 = fregs.at(rs2).value;
    }
    else if(inst.opcode == RV64OPCode::opimm) {
        if(iregs.blocked(rs1)) return;
        assert(SimError::success == isa::perform_int_op_64(inst.param.intop, &p5inst.arg0, iregs.get(rs1), inst.imm));
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::opimm32) {
        if(iregs.blocked(rs1)) return;
        assert(SimError::success == isa::perform_int_op_32(inst.param.intop, &p5inst.arg0, iregs.get(rs1), inst.imm));
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::op) {
        if(iregs.blocked(rs1) || iregs.blocked(rs2)) return;
        assert(SimError::success == isa::perform_int_op_64(inst.param.intop, &p5inst.arg0, iregs.get(rs1), iregs.get(rs2)));
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::op32) {
        if(iregs.blocked(rs1) || iregs.blocked(rs2)) return;
        assert(SimError::success == isa::perform_int_op_32(inst.param.intop, &p5inst.arg0, iregs.get(rs1), iregs.get(rs2)));
        iregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::madd || inst.opcode == RV64OPCode::msub || inst.opcode == RV64OPCode::nmsub || inst.opcode == RV64OPCode::nmadd) {
        if(fregs.blocked(rs1) || fregs.blocked(rs2) || fregs.blocked(rs3)) return;
        assert(SimError::success == isa::perform_fmadd_op(inst.opcode, inst.param.fp.fwid, &p5inst.arg0, fregs.get(rs1), fregs.get(rs2), fregs.get(rs3), &csr_fcsr));
        fregs.block(rd1);
    }
    else if(inst.opcode == RV64OPCode::opfp) {
        RawDataT s1 = 0;
        switch(inst.param.fp.op) {
            case isa::RV64FPOP5::ADD :
            case isa::RV64FPOP5::SUB :
            case isa::RV64FPOP5::MUL :
            case isa::RV64FPOP5::DIV :
            case isa::RV64FPOP5::CMP :
            case isa::RV64FPOP5::MIN :
            case isa::RV64FPOP5::SGNJ:
                if(fregs.blocked(rs2)) return;
            case isa::RV64FPOP5::SQRT:
            case isa::RV64FPOP5::MVF2F:
            case isa::RV64FPOP5::CVTF2I:
            case isa::RV64FPOP5::MVF2I:
                if(fregs.blocked(rs1)) return;
                s1 = fregs.get(rs1);
                break;
            case isa::RV64FPOP5::CVTI2F:
            case isa::RV64FPOP5::MVI2F:
                if(iregs.blocked(rs1)) return;
                s1 = iregs.get(rs1);
                break;
        }
        assert(SimError::success == isa::perform_fp_op(inst.param.fp, &p5inst.arg0, s1, fregs.get(rs2), &csr_fcsr));
        if(isa::rv64_fpop_is_i_rd(inst.param.fp.op)) {
            iregs.block(rd1);
        }
        else {
            fregs.block(rd1);
        }
    }
    // TODO

    p3_result.first = true;
};


void PipeLine5CPU::process_pipeline_4_memory() {
    if(!(p4_workload.first)) {
        statistic.pipeline_2_stalled_nop_count++;
        return;
    }
    if(p4_result.first) {
        // Stalled
        statistic.pipeline_2_stalled_busy_count++;
        return;
    }

    P5InstDecoded &p5inst = p4_workload.second;
    RV64InstDecoded &inst = p4_workload.second.inst;

    if(log_info) {
        sprintf(log_buf, "CPU%d P4: @0x%lx, %s", cpu_id, inst.pc, inst.debug_name_str.c_str());
        simroot::print_log_info(log_buf);
    }

    io_dcache_port->arrival_line(nullptr); // 用不到，清空cache的到达记录

    if(inst.opcode == RV64OPCode::amo && inst.param.amo.op == isa::RV64AMOOP5::SC) {
        VirtAddrT vaddr = iregs.getu(inst.rs1);
        uint64_t data = iregs.getu(inst.rs2);
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            io_sys_port->dev_output(cpu_id, vaddr, isa::rv64_ls_width_to_length(inst.param.amo.wid), &data);
        }
        else {
            PhysAddrT paddr = mem_trans_check_error(vaddr, PGFLAG_R | PGFLAG_W, inst.pc);
            uint32_t len = isa::rv64_ls_width_to_length(inst.param.amo.wid);
            SimError res = io_dcache_port->store_conditional(paddr, len, &data);
            cache_operation_result_check_error(res, inst.pc, vaddr, paddr, len);
            if(res == SimError::unconditional) {
                p5inst.arg0 = 1;
            }
            else if(res != SimError::success) {
                p5inst.cache_missed = true;
                return;
            }
            else {
                p5inst.arg0 = 0;
            }
            dcache_prefetch(vaddr);
        }
    }
    else if(inst.opcode == RV64OPCode::amo && inst.param.amo.op == isa::RV64AMOOP5::LR) {
        VirtAddrT vaddr = iregs.getu(inst.rs1);
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            io_sys_port->dev_input(cpu_id, vaddr, isa::rv64_ls_width_to_length(inst.param.amo.wid), &(p5inst.arg0));
        }
        else {
            PhysAddrT paddr = mem_trans_check_error(vaddr, PGFLAG_R | PGFLAG_W, inst.pc);
            uint32_t len = isa::rv64_ls_width_to_length(inst.param.amo.wid);
            int64_t data = 0;
            SimError res = SimError::miss;
            if(inst.param.amo.wid == isa::RV64LSWidth::word) {
                int32_t tmp = 0;
                res = io_dcache_port->load_reserved(paddr, 4, &tmp);
                cache_operation_result_check_error(res, inst.pc, vaddr, paddr, 4);
                data = tmp;
            }
            else {
                res = io_dcache_port->load_reserved(paddr, 8, &data);
                cache_operation_result_check_error(res, inst.pc, vaddr, paddr, 8);
            }
            if(res != SimError::success) {
                p5inst.cache_missed = true;
                return;
            }
            dcache_prefetch(vaddr);
            RAW_DATA_AS(p5inst.arg0).i64 = data;
        }
    }
    else if(inst.opcode == RV64OPCode::amo) {
        VirtAddrT vaddr = iregs.getu(inst.rs1);
        PhysAddrT paddr = 0;
        IntDataT previous;
        IntDataT value = iregs.get(inst.rs2);
        IntDataT stvalue = 0;
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            io_sys_port->dev_amo(cpu_id, vaddr, isa::rv64_ls_width_to_length(inst.param.amo.wid), inst.param.amo.op, &value, &previous);
        }
        else {
            paddr = mem_trans_check_error(vaddr, PGFLAG_R | PGFLAG_W, inst.pc);
            SimError res = io_dcache_port->store(paddr, 0, &value, false);
            cache_operation_result_check_error(res, inst.pc, vaddr, paddr, 0);
            if(res != SimError::success) {
                p5inst.cache_missed = true;
                return;
            }
            uint32_t len = isa::rv64_ls_width_to_length(inst.param.amo.wid);
            res = SimError::miss;
            if(inst.param.amo.wid == isa::RV64LSWidth::word) {
                int32_t tmp = 0;
                res = io_dcache_port->load(paddr, 4, &tmp, false);
                cache_operation_result_check_error(res, inst.pc, vaddr, paddr, 4);
                RAW_DATA_AS(previous).i64 = tmp;
            }
            else {
                res = io_dcache_port->load(paddr, 8, &previous, false);
                cache_operation_result_check_error(res, inst.pc, vaddr, paddr, 8);
            }
            if(res != SimError::success) {
                p5inst.cache_missed = true;
                return;
            }
            assert(SimError::success == isa::perform_amo_op(inst.param.amo, &stvalue, previous, value));
            res = io_dcache_port->store(paddr, len, &stvalue, false);
            cache_operation_result_check_error(res, inst.pc, vaddr, paddr, len);
            if(res != SimError::success) {
                p5inst.cache_missed = true;
                return;
            }
            dcache_prefetch(vaddr);
        }
        p5inst.arg0 = previous;
    }
    else if(inst.opcode == RV64OPCode::load || inst.opcode == RV64OPCode::loadfp) {
        VirtAddrT vaddr = p5inst.arg0;
        uint32_t len = isa::rv64_ls_width_to_length(inst.param.loadstore);
        uint64_t buf = 0;
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            io_sys_port->dev_input(cpu_id, vaddr, isa::rv64_ls_width_to_length(inst.param.loadstore), &(buf));
            if(log_info) {
                sprintf(log_buf, "CPU%d Load from dev_mem 0x%lx: 0x%lx", cpu_id, vaddr, buf);
                simroot::print_log_info(log_buf);
            }
        }
        else {
            PhysAddrT paddr = 0;
            SimError res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_R);
            if(res != SimError::success) {
                p5inst.err = res;
                p4_result.second = p4_workload.second;
                p4_result.first = true;
                return;
            }
            // PhysAddrT paddr = mem_trans_check_error(vaddr, PGFLAG_R, inst.pc);
            res = io_dcache_port->load(paddr, len, &buf, false);
            cache_operation_result_check_error(res, inst.pc, vaddr, paddr, len);
            if(res != SimError::success) {
                p5inst.cache_missed = true;
                return;
            }
            if(log_info) {
                sprintf(log_buf, "CPU%d Load from MainMem 0x%lx->0x%lx: 0x%lx", cpu_id, vaddr, paddr, buf);
                simroot::print_log_info(log_buf);
            }
            dcache_prefetch(vaddr);
        }
        switch (inst.param.loadstore)
        {
        case isa::RV64LSWidth::byte : RAW_DATA_AS(p5inst.arg0).i64 = *((int8_t*)(&buf)); break;
        case isa::RV64LSWidth::harf : RAW_DATA_AS(p5inst.arg0).i64 = *((int16_t*)(&buf)); break;
        case isa::RV64LSWidth::word : RAW_DATA_AS(p5inst.arg0).i64 = *((int32_t*)(&buf)); break;
        case isa::RV64LSWidth::dword : RAW_DATA_AS(p5inst.arg0).i64 = *((int64_t*)(&buf)); break;
        case isa::RV64LSWidth::ubyte: p5inst.arg0 = *((uint8_t*)(&buf)); break;
        case isa::RV64LSWidth::uharf: p5inst.arg0 = *((uint16_t*)(&buf)); break;
        case isa::RV64LSWidth::uword: p5inst.arg0 = *((uint32_t*)(&buf)); break;
        default: assert(0);
        }
    }
    else if(inst.opcode == RV64OPCode::store || inst.opcode == RV64OPCode::storefp) {
        VirtAddrT vaddr = p5inst.arg0;
        uint32_t len = isa::rv64_ls_width_to_length(inst.param.loadstore);
        uint64_t buf = p5inst.arg1;
        if(io_sys_port->is_dev_mem(cpu_id, vaddr)) {
            io_sys_port->dev_output(cpu_id, vaddr, isa::rv64_ls_width_to_length(inst.param.loadstore), &(buf));
            if(log_info) {
                sprintf(log_buf, "CPU%d Store to dev_mem 0x%lx: 0x%lx", cpu_id, vaddr, buf);
                simroot::print_log_info(log_buf);
            }
        }
        else {
            PhysAddrT paddr = 0;
            SimError res = io_sys_port->v_to_p(cpu_id, vaddr, &paddr, PGFLAG_W);
            if(res != SimError::success) {
                p5inst.err = res;
                p4_result.second = p4_workload.second;
                p4_result.first = true;
                return;
            }
            res = io_dcache_port->store(paddr, len, &buf, false);
            cache_operation_result_check_error(res, inst.pc, vaddr, paddr, len);
            if(res != SimError::success) {
                p5inst.cache_missed = true;
                return;
            }
            if(log_info) {
                sprintf(log_buf, "CPU%d Store to MainMem 0x%lx->0x%lx: 0x%lx", cpu_id, vaddr, paddr, buf);
                simroot::print_log_info(log_buf);
            }
            dcache_prefetch(vaddr);
        }
    }
    // Todo
        
    p4_result.second = p4_workload.second;
    p4_result.first = true;
}


void PipeLine5CPU::process_pipeline_5_commit() {

    if(!(p5_workload.first)) {
        return;
    }

    if(p5_result.first) {
        return;
    }

    p5_result.second = p5_workload.second;
    P5InstDecoded &p5inst = p5_result.second;
    RV64InstDecoded &inst = p5_result.second.inst;
    if(log_info) {
        sprintf(log_buf, "CPU%d P5: %s", cpu_id, inst.debug_name_str.c_str());
        simroot::print_log_info(log_buf);
    }

    if(p5inst.err != SimError::success) {
        if(p5inst.err == SimError::invalidaddr) {
            sprintf(log_buf, "CPU%d Unknown Memory Access @0x%lx, V-Address: 0x%lx", cpu_id, inst.pc, p5inst.arg0);
            LOG(ERROR) << log_buf;
            simroot_assert(0);
        }
        else if(p5inst.err == SimError::unaccessable) {
            sprintf(log_buf, "CPU%d Unauthorized Memory Access @0x%lx, V-Address: 0x%lx", cpu_id, inst.pc, p5inst.arg0);
            LOG(ERROR) << log_buf;
            simroot_assert(0);
        }
        else if(p5inst.err == SimError::pagefault) {
            RVRegArray regs;
            for(int i = 0; i < RV_REG_CNT_INT; i++) {
                regs[i] = iregs.getu(i);
            }
            for(int i = 0; i < RV_REG_CNT_FP; i++) {
                regs[RV_REG_CNT_INT + i] = fregs.getb(i);
            }
            pc_redirect = io_sys_port->exception(cpu_id, inst.pc, SimError::pagefault, p5inst.arg0, 0, regs);
            apply_pc_redirect = true;
            for(int i = 0; i < RV_REG_CNT_INT; i++) {
                iregs.setu(i, regs[i]);
            }
            for(int i = 0; i < RV_REG_CNT_FP; i++) {
                fregs.setb(i, regs[RV_REG_CNT_INT + i]);
            }
            return;
        }
        else {
            sprintf(log_buf, "CPU%d Unknowm Error %d @0x%lx, arg: 0x%lx", cpu_id, (int32_t)(p5inst.err), inst.pc, p5inst.arg0);
            LOG(ERROR) << log_buf;
            simroot_assert(0);
        }
    }

    if(inst.opcode == RV64OPCode::lui) {
        iregs.sets(inst.rd, RAW_DATA_AS(inst.imm).i64);
    }
    else if( 
        inst.opcode == RV64OPCode::auipc ||
        inst.opcode == RV64OPCode::opimm ||
        inst.opcode == RV64OPCode::opimm32 ||
        inst.opcode == RV64OPCode::op ||
        inst.opcode == RV64OPCode::op32 ||
        inst.opcode == RV64OPCode::amo ||
        inst.opcode == RV64OPCode::load ||
        (inst.opcode == RV64OPCode::opfp && isa::rv64_fpop_is_i_rd(inst.param.fp.op))
    ) {
        iregs.setu(inst.rd, p5inst.arg0);
    }
    else if( 
        inst.opcode == RV64OPCode::madd ||
        inst.opcode == RV64OPCode::msub ||
        inst.opcode == RV64OPCode::nmsub ||
        inst.opcode == RV64OPCode::nmadd ||
        inst.opcode == RV64OPCode::loadfp ||
        inst.opcode == RV64OPCode::opfp
    ) {
        fregs.setb(inst.rd, p5inst.arg0);
    }
    else if (
        inst.opcode == RV64OPCode::jal || 
        inst.opcode == RV64OPCode::jalr
    ) {
        iregs.setu(inst.rd, p5inst.arg0);
        // JAL has been performed in pipeline stage 1
        if(inst.opcode == RV64OPCode::jalr) {
            pc_redirect = p5inst.arg1;
            apply_pc_redirect = true;
        }
    }
    else if (
        inst.opcode == RV64OPCode::branch
    ) {
        if(p5inst.arg0) {
            pc_redirect = p5inst.arg1;
            apply_pc_redirect = true;
        }
    }
    else if(
        inst.flag & RVINSTFLAG_ECALL
    ) {
        RVRegArray regs;
        for(int i = 0; i < RV_REG_CNT_INT; i++) {
            regs[i] = iregs.getu(i);
        }
        for(int i = 0; i < RV_REG_CNT_FP; i++) {
            regs[RV_REG_CNT_INT + i] = fregs.getb(i);
        }
        pc_redirect = io_sys_port->syscall(cpu_id, inst.pc, regs);
        apply_pc_redirect = true;
        for(int i = 0; i < RV_REG_CNT_INT; i++) {
            iregs.setu(i, regs[i]);
        }
        for(int i = 0; i < RV_REG_CNT_FP; i++) {
            fregs.setb(i, regs[RV_REG_CNT_INT + i]);
        }
    }
    else if(
        inst.flag & RVINSTFLAG_EBREAK
    ) {
        RVRegArray regs;
        for(int i = 0; i < RV_REG_CNT_INT; i++) {
            regs[i] = iregs.getu(i);
        }
        for(int i = 0; i < RV_REG_CNT_FP; i++) {
            regs[RV_REG_CNT_INT + i] = fregs.getb(i);
        }
        pc_redirect = io_sys_port->ebreak(cpu_id, inst.pc, regs);
        apply_pc_redirect = true;
        for(int i = 0; i < RV_REG_CNT_INT; i++) {
            iregs.setu(i, regs[i]);
        }
        for(int i = 0; i < RV_REG_CNT_FP; i++) {
            fregs.setb(i, regs[RV_REG_CNT_INT + i]);
        }
    }
    else if(
        inst.opcode == RV64OPCode::system
    ) {
        // CSR
        using isa::CSRNumber;
        using isa::RV64CSROP3;
        CSRNumber csr_num = (CSRNumber)(inst.param.csr.index & RV64_CSR_NUM_MASK);
        #define ERR_CSR_OP \
                sprintf(log_buf, "%lx: Unknown CSR op @0x%lx: %s\n", simroot::get_current_tick(), inst.pc, inst.debug_name_str.c_str()); \
                LOG(ERROR) << log_buf; \
                simroot_assert(0);
        
        if(csr_num == CSRNumber::fcsr) {
            switch (inst.param.csr.op)
            {
            case RV64CSROP3::CSRRW: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrw<0,32>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRS: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrs<0,32>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRC: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrc<0,32>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRWI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrw<0,32>(&csr_fcsr, inst.imm)); break;
            case RV64CSROP3::CSRRSI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrs<0,32>(&csr_fcsr, inst.imm)); break;
            case RV64CSROP3::CSRRCI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrc<0,32>(&csr_fcsr, inst.imm)); break;
            default: ERR_CSR_OP;
            }
        }
        else if(csr_num == CSRNumber::frm) {
            switch (inst.param.csr.op)
            {
            case RV64CSROP3::CSRRW: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrw<5,3>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRS: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrs<5,3>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRC: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrc<5,3>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRWI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrw<5,3>(&csr_fcsr, inst.imm)); break;
            case RV64CSROP3::CSRRSI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrs<5,3>(&csr_fcsr, inst.imm)); break;
            case RV64CSROP3::CSRRCI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrc<5,3>(&csr_fcsr, inst.imm)); break;
            default: ERR_CSR_OP;
            }
        }
        else if(csr_num == CSRNumber::fflags) {
            switch (inst.param.csr.op)
            {
            case RV64CSROP3::CSRRW: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrw<0,5>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRS: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrs<0,5>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRC: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrc<0,5>(&csr_fcsr, iregs.getu(inst.rs1))); break;
            case RV64CSROP3::CSRRWI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrw<0,5>(&csr_fcsr, inst.imm)); break;
            case RV64CSROP3::CSRRSI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrs<0,5>(&csr_fcsr, inst.imm)); break;
            case RV64CSROP3::CSRRCI: iregs.setu(inst.rd, csr_fcsr = isa::rv64_csrrc<0,5>(&csr_fcsr, inst.imm)); break;
            default: ERR_CSR_OP;
            }
        }
        else if(csr_num == CSRNumber::cycle) {
            switch (inst.param.csr.op)
            {
            case RV64CSROP3::CSRRW:
            case RV64CSROP3::CSRRS:
            case RV64CSROP3::CSRRC:
            case RV64CSROP3::CSRRWI:
            case RV64CSROP3::CSRRSI:
            case RV64CSROP3::CSRRCI: iregs.setu(inst.rd, simroot::get_current_tick()); break;
            default: ERR_CSR_OP;
            }
        }
        else if(csr_num == CSRNumber::time) {
            switch (inst.param.csr.op)
            {
            case RV64CSROP3::CSRRW:
            case RV64CSROP3::CSRRS:
            case RV64CSROP3::CSRRC:
            case RV64CSROP3::CSRRWI:
            case RV64CSROP3::CSRRSI:
            case RV64CSROP3::CSRRCI: iregs.setu(inst.rd, simroot::get_wall_time_tick()); break;
            default: ERR_CSR_OP;
            }
        }
        else if(csr_num == CSRNumber::instret) {
            switch (inst.param.csr.op)
            {
            case RV64CSROP3::CSRRW:
            case RV64CSROP3::CSRRS:
            case RV64CSROP3::CSRRC:
            case RV64CSROP3::CSRRWI:
            case RV64CSROP3::CSRRSI:
            case RV64CSROP3::CSRRCI: iregs.setu(inst.rd, statistic.finished_inst_count); break;
            default: ERR_CSR_OP;
            }
        }
        else {
            sprintf(log_buf, "%lx: Unknown CSR index @0x%lx: %d\n", simroot::get_current_tick(), inst.pc, (int)(inst.param.csr.index));
            LOG(ERROR) << log_buf;
            simroot_assert(0);
        }
    }
    // Todo
    if(log_file_commited_inst) {
        sprintf(log_buf, "%ld: Commit Inst @0x%lx: %s -> 0x%lx, 0x%lx\n", simroot::get_current_tick(), inst.pc, inst.debug_name_str.c_str(), p5inst.arg0, p5inst.arg1);
        log_file_commited_inst->write(log_buf, strlen(log_buf));
        if(log_regs) {
            string s = (string("Register List @CPU") + std::to_string(cpu_id) + "\n");
            for(int i = 0; i < 32; i+=2) {
                if(log_fregs) {
                    sprintf(log_buf, "    %s-%d: 0x%16lx, %s-%d: 0x%16lx,    %s-%d: 0x%16lx, %s-%d: 0x%16lx,\n",
                        isa::ireg_names()[i], iregs.reg[i].busy, iregs.getu(i),
                        isa::ireg_names()[i+1], iregs.reg[i].busy, iregs.getu(i+1),
                        isa::freg_names()[i], fregs.reg[i].busy, fregs.getb(i),
                        isa::freg_names()[i+1], fregs.reg[i].busy, fregs.getb(i+1)
                    );
                }
                else {
                    sprintf(log_buf, "    %s-%d: 0x%16lx, %s-%d: 0x%16lx,\n",
                        isa::ireg_names()[i], iregs.reg[i].busy, iregs.getu(i),
                        isa::ireg_names()[i+1], iregs.reg[i].busy, iregs.getu(i+1)
                    );
                }
                s += (string(log_buf));
            }
            log_file_commited_inst->write(s.c_str(), s.length());
        }
        log_file_commited_inst->flush();
    }

    if(inst.opcode == RV64OPCode::load || inst.opcode == RV64OPCode::loadfp) {
        statistic.ld_inst_cnt ++;
        if(p5inst.cache_missed) statistic.ld_cache_miss_count ++;
        else statistic.ld_cache_hit_count ++;
        simroot_assert(p5inst.mem_finish_tick >= p5inst.mem_start_tick);
        statistic.ld_mem_tick_sum += (p5inst.mem_finish_tick - p5inst.mem_start_tick);
    }
    else if(inst.opcode == RV64OPCode::store || inst.opcode == RV64OPCode::storefp) {
        statistic.st_inst_cnt ++;
        if(p5inst.cache_missed) statistic.st_cache_miss_count ++;
        else statistic.st_cache_hit_count ++;
        simroot_assert(p5inst.mem_finish_tick >= p5inst.mem_start_tick);
        statistic.st_mem_tick_sum += (p5inst.mem_finish_tick - p5inst.mem_start_tick);
    }

    p5_result.first = true;
};

void PipeLine5CPU::on_current_tick() {
    io_icache_port->on_current_tick();
    io_dcache_port->on_current_tick();
    if(is_halt) {
        return;
    }
    process_pipeline_1_fetch();
    process_pipeline_2_decode();
    process_pipeline_3_execute();
    process_pipeline_4_memory();
    process_pipeline_5_commit();
};

void PipeLine5CPU::apply_next_tick() {
    io_icache_port->apply_next_tick();
    io_dcache_port->apply_next_tick();
    if(is_halt && !apply_cpu_wakeup) {
        return;
    }
    if(apply_halt) {
        is_halt = true;
        apply_halt = false;
        clear_pipeline();
        return;
    }
    
    bool p5_finished = p5_workload.first && p5_result.first;
    bool p4_finished = p4_workload.first && p4_result.first;
    bool p3_finished = p3_workload.first && p3_result.first;
    bool p2_finished = p2_workload.first && p2_result.first;
    bool p1_finished = p1_result.first;

    if(log_info) {
        sprintf(log_buf, "CPU%d Pipeline: %d | %d %d | %d %d | %d %d | %d %d", cpu_id, p1_result.first,
            p2_workload.first, p2_result.first,
            p3_workload.first, p3_result.first,
            p4_workload.first, p4_result.first,
            p5_workload.first, p5_result.first
        );
        simroot::print_log_info(log_buf);
    }
    
    if(p5_finished) {
        // P5 commit
        p5_workload.first = false;
        p5_result.first = false;
        statistic.finished_inst_count++;
    }
    if(p4_finished && !p5_workload.first) {
        // P4 -> P5
        p4_workload.first = false;
        p4_result.first = false;
        p5_workload.first = true;
        p5_workload.second = p4_result.second;
        p5_workload.second.mem_finish_tick = statistic.total_tick_processed;
    }
    if(p3_finished && p3_result.second.passp4 && !p4_workload.first && !p5_workload.first) {
        // P3 -> P5
        p3_workload.first = false;
        p3_result.first = false;
        p5_workload.first = true;
        p5_workload.second = p3_result.second;
    }
    if(p3_finished && !p3_result.second.passp4 && !p4_workload.first) {
        // P3 -> P4
        p3_workload.first = false;
        p3_result.first = false;
        p4_workload.first = true;
        p4_workload.second = p3_result.second;
        p4_workload.second.mem_start_tick = statistic.total_tick_processed;
    }
    if(p2_finished && p2_result.second.passp3 && p2_result.second.passp4 && !p3_workload.first && !p4_workload.first && !p5_workload.first) {
        // P2 -> P5
        p2_workload.first = false;
        p2_result.first = false;
        p5_workload.first = true;
        p5_workload.second = p2_result.second;
    }
    if(p2_finished && p2_result.second.passp3 && !p2_result.second.passp4 && !p3_workload.first && !p4_workload.first) {
        // P2 -> P4
        p2_workload.first = false;
        p2_result.first = false;
        p4_workload.first = true;
        p4_workload.second = p2_result.second;
        p4_workload.second.mem_start_tick = statistic.total_tick_processed;
    }
    if(p2_finished && !p2_result.second.passp3 && !p3_workload.first) {
        // P2 -> P3
        p2_workload.first = false;
        p2_result.first = false;
        p3_workload.first = true;
        p3_workload.second = p2_result.second;
    }
    if(p1_finished && !p2_workload.first) {
        // P1 -> P2
        p1_result.first = false;
        p2_workload.first = true;
        p2_workload.second = p1_result.second;
    }

    current_tlb_occupy_dcache = false;

    statistic.total_tick_processed++;

    if(apply_pc_redirect) {
        apply_pc_redirect = false;
        pc = pc_redirect;
        p1_wait_for_jump = false;
        clear_pipeline();
        if(apply_iregs) {
            for(int i = 1; i < RV_REG_CNT_INT; i++) {
                iregs.setu(i, apply_ireg_buf[i]);
            }
            apply_iregs = false;
        }
        if(apply_fregs) {
            for(int i = 0; i < RV_REG_CNT_FP; i++) {
                fregs.setb(i, apply_freg_buf[i]);
            }
            apply_fregs = false;
        }
        apply_cpu_wakeup = false;
        is_halt = false;
    }
    else {
        pc = p1_apply_next_pc;
    }

    if(apply_new_pagetable) {
        apply_new_pagetable = false;
        satp = new_pgtable;
        asid = new_asid;
    }

    if(log_info && log_regs) {
        debug_print_regs();
    }
};

void PipeLine5CPU::clear_statistic() {
    memset(&statistic, 0, sizeof(statistic));
};

void PipeLine5CPU::print_statistic(std::ofstream &ofile) {
    #define PIPELINE_5_GENERATE_PRINTSTATISTIC(n) ofile << #n << ": " << statistic.n << "\n" ;
    PIPELINE_5_GENERATE_PRINTSTATISTIC(total_tick_processed)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(finished_inst_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_1_stalled_fetch_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_1_stalled_busy_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_2_stalled_nop_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_2_stalled_busy_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_2_stalled_flush_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_3_stalled_nop_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_3_stalled_busy_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_3_stalled_waitreg_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_4_stalled_nop_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_4_stalled_amo_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_4_stalled_fetch_count)
    // PIPELINE_5_GENERATE_PRINTSTATISTIC(pipeline_4_stalled_busy_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(ld_cache_miss_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(ld_cache_hit_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(ld_inst_cnt)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(ld_mem_tick_sum)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(st_cache_miss_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(st_cache_hit_count)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(st_inst_cnt)
    PIPELINE_5_GENERATE_PRINTSTATISTIC(st_mem_tick_sum)
    #undef PIPELINE_5_GENERATE_PRINTSTATISTIC
};

void PipeLine5CPU::print_setup_info(std::ofstream &ofile) {

};

void PipeLine5CPU::dump_core(std::ofstream &ofile) {
    io_icache_port->dump_core(ofile);
    io_dcache_port->dump_core(ofile);
    sprintf(log_buf, "CPU%d Pipeline: %d | %d %d | %d %d | %d %d | %d %d", cpu_id, p1_result.first,
        p2_workload.first, p2_result.first,
        p3_workload.first, p3_result.first,
        p4_workload.first, p4_result.first,
        p5_workload.first, p5_result.first
    );
    ofile << log_buf << "\n";
    if(p3_workload.first) {
        P5InstDecoded &p5inst = p3_workload.second;
        RV64InstDecoded &inst = p3_workload.second.inst;
        RVRegIndexT rd1 = inst.rd;
        RVRegIndexT rs1 = inst.rs1;
        RVRegIndexT rs2 = inst.rs2;
        RVRegIndexT rs3 = inst.rs3;
        isa::init_rv64_inst_name_str(&inst);
        sprintf(log_buf, "CPU%d P3Work: @0x%lx, %s, rd-rs3: %d %d %d %d", cpu_id, inst.pc, inst.debug_name_str.c_str(), rd1, rs1, rs2, rs3);
        ofile << log_buf << "\n";
    }
    if(p3_result.first) {
        P5InstDecoded &p5inst = p3_result.second;
        RV64InstDecoded &inst = p3_result.second.inst;
        RVRegIndexT rd1 = inst.rd;
        RVRegIndexT rs1 = inst.rs1;
        RVRegIndexT rs2 = inst.rs2;
        RVRegIndexT rs3 = inst.rs3;
        isa::init_rv64_inst_name_str(&inst);
        sprintf(log_buf, "CPU%d P3Res: @0x%lx, %s, rd-rs3: %d %d %d %d", cpu_id, inst.pc, inst.debug_name_str.c_str(), rd1, rs1, rs2, rs3);
        ofile << log_buf << "\n";
    }
    if(p4_workload.first) {
        P5InstDecoded &p5inst = p4_workload.second;
        RV64InstDecoded &inst = p4_workload.second.inst;
        isa::init_rv64_inst_name_str(&inst);
        sprintf(log_buf, "CPU%d P4Work: @0x%lx, %s", cpu_id, inst.pc, inst.debug_name_str.c_str());
        ofile << log_buf << "\n";
    }
    if(p4_result.first) {
        P5InstDecoded &p5inst = p4_result.second;
        RV64InstDecoded &inst = p4_result.second.inst;
        isa::init_rv64_inst_name_str(&inst);
        sprintf(log_buf, "CPU%d P4Res: @0x%lx, %s", cpu_id, inst.pc, inst.debug_name_str.c_str());
        ofile << log_buf << "\n";
    }
    string s = "\n";
    s += (string("CPU ID: ") + std::to_string(cpu_id) + "\n");
    for(int i = 0; i < 32; i+=2) {
        if(log_fregs) {
            sprintf(log_buf, "    %s-%d: 0x%16lx, %s-%d: 0x%16lx,    %s-%d: 0x%16lx, %s-%d: 0x%16lx,\n",
                isa::ireg_names()[i], iregs.reg[i].busy, iregs.getu(i),
                isa::ireg_names()[i+1], iregs.reg[i].busy, iregs.getu(i+1),
                isa::freg_names()[i], fregs.reg[i].busy, fregs.getb(i),
                isa::freg_names()[i+1], fregs.reg[i].busy, fregs.getb(i+1)
            );
        }
        else {
            sprintf(log_buf, "    %s-%d: 0x%16lx, %s-%d: 0x%16lx,\n",
                isa::ireg_names()[i], iregs.reg[i].busy, iregs.getu(i),
                isa::ireg_names()[i+1], iregs.reg[i].busy, iregs.getu(i+1)
            );
        }
        s += (string(log_buf) + "\n");
    }
    ofile << s << std::endl;
}

void PipeLine5CPU::debug_print_regs() {
    string s = "\n";
    s += (string("CPU ID: ") + std::to_string(cpu_id) + "\n");
    for(int i = 0; i < 32; i+=2) {
        if(log_fregs) {
            sprintf(log_buf, "    %s-%d: 0x%16lx, %s-%d: 0x%16lx,    %s-%d: 0x%16lx, %s-%d: 0x%16lx,\n",
                isa::ireg_names()[i], iregs.reg[i].busy, iregs.getu(i),
                isa::ireg_names()[i+1], iregs.reg[i].busy, iregs.getu(i+1),
                isa::freg_names()[i], fregs.reg[i].busy, fregs.getb(i),
                isa::freg_names()[i+1], fregs.reg[i].busy, fregs.getb(i+1)
            );
        }
        else {
            sprintf(log_buf, "    %s-%d: 0x%16lx, %s-%d: 0x%16lx,\n",
                isa::ireg_names()[i], iregs.reg[i].busy, iregs.getu(i),
                isa::ireg_names()[i+1], iregs.reg[i].busy, iregs.getu(i+1)
            );
        }
        s += (string(log_buf) + "\n");
    }
    simroot::print_log_info(s);
};


}

}

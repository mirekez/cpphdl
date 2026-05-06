#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "File.h"

#define STAGES_NUM 3

#include "Decode.h"
#include "Execute.h"
#include "Writeback.h"
#include "cache/L1Cache.h"

#define RAM_SIZE 2048
#define CACHE_ADDR_BITS 13

long sys_clock = -1;

class Tribe: public Module
{
    Decode          dec;
    Execute         exe;
    Writeback       wb;
    File<32,32>     regs;
    L1Cache<1024,32,2,0,CACHE_ADDR_BITS> icache;
    L1Cache<1024,32,2,1,CACHE_ADDR_BITS> dcache;

public:

    __PORT(bool)      dmem_write_out;
    __PORT(uint32_t)  dmem_write_data_out;
    __PORT(uint8_t)   dmem_write_mask_out;
    __PORT(bool)      dmem_read_out;
    __PORT(uint32_t)  dmem_addr_out;
    __PORT(uint32_t)  dmem_read_data_in;
    __PORT(uint32_t)  imem_read_addr_out;
    __PORT(uint32_t)  imem_read_data_in;
    __PORT(bool)      perf_hazard_stall_out = __VAR(hazard_stall_comb_func());
    __PORT(bool)      perf_branch_stall_out = __VAR(branch_stall_comb_func());
    __PORT(bool)      perf_dcache_wait_out = __VAR(dcache_wait_comb_func());
    __PORT(bool)      perf_icache_wait_out = __VAR(icache_wait_comb_func());
    __PORT(u<3>)      perf_icache_state_out;
    __PORT(u<3>)      perf_dcache_state_out;
    __PORT(bool)      perf_icache_hit_out;
    __PORT(bool)      perf_icache_lookup_wait_out;
    __PORT(bool)      perf_icache_refill_wait_out;
    __PORT(bool)      perf_icache_init_wait_out;
    __PORT(bool)      perf_icache_issue_wait_out;
    bool              debugen_in;

private:

    reg<u32>        pc;
    reg<u1>         valid;

    reg<u32>        alu_result_reg;
    reg<u32>        load_data_reg;
    reg<u1>         load_data_valid_reg;

    reg<array<State,STAGES_NUM-1>> state_reg;

    // debug
    reg<u32>        debug_alu_a_reg;
    reg<u32>        debug_alu_b_reg;
    reg<u32>        debug_branch_target_reg;
    reg<u1>         debug_branch_taken_reg;


    __LAZY_COMB(hazard_stall_comb, bool)
        const auto& dec_state_tmp = dec.state_out();

        hazard_stall_comb = false;
        if (state_reg[0].valid && state_reg[0].wb_op == Wb::MEM && state_reg[0].rd != 0) {  // Ex hazard
            if (state_reg[0].rd == dec_state_tmp.rs1) {
                hazard_stall_comb = true;
            }
            if (state_reg[0].rd == dec_state_tmp.rs2) {
                hazard_stall_comb = true;
            }
        }
        return hazard_stall_comb;
    }

    __LAZY_COMB(branch_stall_comb, bool)
        branch_stall_comb = state_reg[0].valid && state_reg[0].br_op != Br::BNONE;
        return branch_stall_comb;
    }

    __LAZY_COMB(branch_flush_comb, bool)
        branch_flush_comb = state_reg[0].valid && exe.branch_taken_out() && !branch_stall_comb_func();
        return branch_flush_comb;
    }

    __LAZY_COMB(stall_comb, bool)
        stall_comb = hazard_stall_comb_func() || branch_stall_comb_func();
        return stall_comb;
    }

    __LAZY_COMB(dcache_wait_comb, bool)
        return dcache_wait_comb = dcache.busy_out();
    }

    __LAZY_COMB(icache_wait_comb, bool)
        return icache_wait_comb = icache.busy_out();
    }

    __LAZY_COMB(cache_busy_comb, bool)
        return cache_busy_comb = dcache_wait_comb_func() || wb_mem_wait_comb_func();
    }

    __LAZY_COMB(fetch_valid_comb, bool)
        return fetch_valid_comb = valid && icache.read_valid_out() && icache.read_addr_out() == (uint32_t)pc;
    }

    __LAZY_COMB(load_response_comb, bool)
        return load_response_comb = state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
            dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg;
    }

    __LAZY_COMB(wb_mem_valid_comb, bool)
        return wb_mem_valid_comb = load_data_valid_reg || load_response_comb_func();
    }

    __LAZY_COMB(wb_write_ready_comb, bool)
        return wb_write_ready_comb = state_reg[1].wb_op != Wb::MEM || wb_mem_valid_comb_func();
    }

    __LAZY_COMB(wb_mem_wait_comb, bool)
        return wb_mem_wait_comb = state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
            !wb_mem_valid_comb_func();
    }

    __LAZY_COMB(wb_mem_data_comb, uint32_t)
        return wb_mem_data_comb = load_data_valid_reg ? (uint32_t)load_data_reg :
            (load_response_comb_func() ? dcache.read_data_out() : (uint32_t)0);
    }

    __LAZY_COMB(fetch_addr_comb, uint32_t)
        fetch_addr_comb = pc;
        if (fetch_valid_comb_func() && !stall_comb_func()) {
            fetch_addr_comb = pc + ((dec.instr_in()&3)==3?4:2);
        }
        if (state_reg[0].valid && exe.branch_taken_out()) {
            fetch_addr_comb = exe.branch_target_out();
        }
        return fetch_addr_comb;
    }

    __LAZY_COMB(exe_state_comb, State)
        exe_state_comb = state_reg[0];
        if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM && state_reg[1].rd != 0 &&
            wb_mem_valid_comb_func()) {
            if (state_reg[0].rs1 == state_reg[1].rd) {
                exe_state_comb.rs1_val = wb_mem_data_comb_func();
            }
            if (state_reg[0].rs2 == state_reg[1].rd) {
                exe_state_comb.rs2_val = wb_mem_data_comb_func();
            }
        }
        return exe_state_comb;
    }

    void forward()
    {
        const auto& dec_state_tmp = dec.state_out();

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::ALU && state_reg[1].rd != 0) {  // Mem/Wb alu
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg._next[0].rs1_val = alu_result_reg;
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", (uint32_t)alu_result_reg);
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg._next[0].rs2_val = alu_result_reg;
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", (uint32_t)alu_result_reg);
                }
            }
        }

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM && state_reg[1].rd != 0 &&
            wb_mem_valid_comb_func()) {  // Mem/Wb mem
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg._next[0].rs1_val = wb_mem_data_comb_func();
                if (debugen_in) {
                    printf("forwarding %.08x from MEM to RS1\n", (uint32_t)wb_mem_data_comb_func());
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg._next[0].rs2_val = wb_mem_data_comb_func();
                if (debugen_in) {
                    printf("forwarding %.08x from MEM to RS2\n", (uint32_t)wb_mem_data_comb_func());
                }
            }
        }

        if (state_reg[0].valid && state_reg[0].wb_op == Wb::ALU && state_reg[0].rd != 0) {  // Ex/Mem alu
            if (dec_state_tmp.rs1 == state_reg[0].rd) {
                state_reg._next[0].rs1_val = exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS1\n", (uint32_t)exe.alu_result_out());
                }
            }
            if (dec_state_tmp.rs2 == state_reg[0].rd) {
                state_reg._next[0].rs2_val = exe.alu_result_out();
                if (debugen_in) {
                    printf("forwarding %.08x from ALU to RS2\n", (uint32_t)exe.alu_result_out());
                }
            }
        }
    }

public:

    void _work(bool reset)
    {
        if (debugen_in) {
            debug();
        }

        if (dmem_addr_out() == 0x11223344 && dmem_write_out()) {
            FILE* out = fopen("out.txt", "a");
            if (debugen_in) {
                std::print("OUTPUT pc={} data={:08x} char={:02x}\n", pc, dmem_write_data_out(), dmem_write_data_out() & 0xFF);
            }
            fprintf(out, "%c", dmem_write_data_out()&0xFF);
            fclose(out);
        }

        if (cache_busy_comb_func()) {
            pc._next = pc;
            valid._next = valid;
            state_reg._next = state_reg;
            alu_result_reg._next = alu_result_reg;
            if (load_response_comb_func()) {
                load_data_reg._next = dcache.read_data_out();
                load_data_valid_reg._next = true;
                if (state_reg[1].rd != 0 && state_reg[0].valid) {
                    if (state_reg[0].rs1 == state_reg[1].rd) {
                        state_reg._next[0].rs1_val = dcache.read_data_out();
                    }
                    if (state_reg[0].rs2 == state_reg[1].rd) {
                        state_reg._next[0].rs2_val = dcache.read_data_out();
                    }
                }
            }
            debug_branch_target_reg._next = debug_branch_target_reg;
            debug_branch_taken_reg._next = debug_branch_taken_reg;
        }
        else {
            if (fetch_valid_comb_func() && !stall_comb_func()) {
                pc._next = pc + ((dec.instr_in()&3)==3?4:2);
            }
            if (state_reg[0].valid && exe.branch_taken_out()) {
                pc._next = exe.branch_target_out();
            }

            valid._next = true;

            if (hazard_stall_comb_func()) {
                state_reg._next[0] = State{};
                state_reg._next[0].valid = false;
            }
            else {
                state_reg._next[0] = dec.state_out();
                state_reg._next[0].valid = dec.instr_valid_in() && !branch_stall_comb_func() && !branch_flush_comb_func();
                forward();
            }
            state_reg._next[1] = state_reg[0];
            alu_result_reg._next = exe.alu_result_out();
            load_data_valid_reg._next = false;
            debug_branch_target_reg._next = exe.branch_target_out();
            debug_branch_taken_reg._next = exe.branch_taken_out();
        }

        regs._work(reset);
        dec._work(reset);
        exe._work(reset);
        wb._work(reset);
        icache._work(reset);
        dcache._work(reset);

        if (reset) {
            state_reg._next[0].valid = 0;
            state_reg._next[1].valid = 0;
            pc.clr();
            valid.clr();
            load_data_reg.clr();
            load_data_valid_reg.clr();
        }
    }

    void _work_neg(bool reset)
    {
    }

    void debug()
    {
        State tmp;
        Rv32im instr = {{{imem_read_data_in()}}};
        instr.decode(tmp);

        std::print("({:d}/{:d}){} st[h{} b{} dc{} ic{} is{} ds{} ih{}]: [{:s}]{:08x}  rs{:02d}/{:02d},imm:{:08x},rd{:02d} => ({:d})ops:{:02d}/{}/{}/{} rs{:02d}/{:02d}:{:08x}/{:08x},imm:{:08x},alu:{:09x},rd{:02d} br({:d}){:08x} => mem({:d}/{:d}@{:08x}){:08x}/{:01x} ({:d})wop({:x}),r({:d}){:08x}@{:02d}",
            (bool)valid, (bool)stall_comb_func(), pc,
            (bool)hazard_stall_comb_func(), (bool)branch_stall_comb_func(),
            (bool)dcache_wait_comb_func(), (bool)icache_wait_comb_func(),
            (uint32_t)icache.perf_state_out(), (uint32_t)dcache.perf_state_out(),
            (bool)icache.perf_hit_out(),
            instr.mnemonic(), (instr.raw&3)==3?instr.raw:(instr.raw|0xFFFF0000),
            (int)tmp.rs1, (int)tmp.rs2, tmp.imm, (int)tmp.rd,
            (bool)state_reg[0].valid, (uint8_t)state_reg[0].alu_op, (uint8_t)state_reg[0].mem_op, (uint8_t)state_reg[0].br_op, (uint8_t)state_reg[0].wb_op,
            (int)state_reg[0].rs1, (int)state_reg[0].rs2, state_reg[0].rs1_val, state_reg[0].rs2_val, state_reg[0].imm, exe.alu_result_out(), (int)state_reg[0].rd,
            (bool)exe.branch_taken_out(), exe.branch_target_out(),
            (bool)exe.mem_write_out(), (bool)exe.mem_read_out(), exe.mem_write_addr_out(), exe.mem_write_data_out(), exe.mem_write_mask_out(),
            (bool)state_reg[1].valid, (uint8_t)state_reg[1].wb_op, (bool)wb.regs_write_out(), wb.regs_data_out(), wb.regs_wr_id_out());

#ifndef SYNTHESIS
            // delayed by 1 to align EX to WB
        std::string interpret;
        if (state_reg[1].valid && state_reg[1].alu_op != Alu::ANONE) {
            interpret += std::format("r{:02d} r{:02d} {:5s}({:08x},{:08x}) ", (int)state_reg[1].rs1, (int)state_reg[1].rs2, AOPS[state_reg[1].alu_op],
                             (uint32_t)debug_alu_a_reg, (uint32_t)debug_alu_b_reg);
        }
        if (state_reg[1].valid && state_reg[1].br_op != Br::BNONE && debug_branch_taken_reg) {
            interpret += std::format("{}({:08x}) rd={:02d} ", BOPS[state_reg[1].br_op], (uint32_t)debug_branch_target_reg, (int)state_reg[1].rd);
        }
        if (state_reg[1].valid && state_reg[1].mem_op == Mem::LOAD) {
            interpret += std::format("LOAD({:08x}) ", (uint32_t)alu_result_reg);
        }
        if (state_reg[1].valid && state_reg[1].mem_op == Mem::STORE) {
            interpret += std::format("STOR({:08x}) {:08x} from r{:02d} ", (uint32_t)alu_result_reg, state_reg[1].rs2_val, (int)state_reg[1].rs2);
        }
        if (state_reg[1].valid && state_reg[1].wb_op != Wb::WNONE && wb.regs_write_out()) {
            interpret += std::format("wb {:08x} from {} to r{:02d} ", wb.regs_data_out(), WOPS[state_reg[1].wb_op], wb.regs_wr_id_out());
        }
            //
        std::print(": {}\n", interpret);
        debug_alu_a_reg._next = exe.debug_alu_a_out();
        debug_alu_b_reg._next = exe.debug_alu_b_out();
        debug_branch_target_reg = exe.branch_target_out();
#else
        std::print("\n");
#endif
    }

    void _strobe()
    {
        pc.strobe();
        valid.strobe();
        state_reg.strobe();
        alu_result_reg.strobe();
        load_data_reg.strobe();
        load_data_valid_reg.strobe();
        debug_alu_a_reg.strobe();
        debug_alu_b_reg.strobe();
        debug_branch_target_reg.strobe();
        debug_branch_taken_reg.strobe();

        regs._strobe();
        dec._strobe();
        exe._strobe();
        wb._strobe();
        icache._strobe();
        dcache._strobe();
    }

    void _assign()
    {
//        dec.state_in       = __VAR( state_reg[0] );  // execute stage input is same
        dec.pc_in          = __VAR( pc );
        dec.instr_valid_in = __EXPR(fetch_valid_comb_func());
        dec.instr_in       = icache.read_data_out;
        dec.regs_data0_in  = __EXPR( dec.rs1_out() == 0 ? 0 : regs.read_data0_out() );
        dec.regs_data1_in  = __EXPR( dec.rs2_out() == 0 ? 0 : regs.read_data1_out() );
        dec._assign();  // outputs are ready

        exe.state_in       = __VAR( exe_state_comb_func() );
        exe._assign();  // outputs are ready

        wb.state_in       = __VAR( state_reg[1] );
        wb.mem_data_in    = __VAR(wb_mem_data_comb_func());
        wb.alu_result_in  = __VAR( alu_result_reg );
        wb._assign();  // outputs are ready

        regs.read_addr0_in = __EXPR( (uint8_t)dec.rs1_out() );
        regs.read_addr1_in = __EXPR( (uint8_t)dec.rs2_out() );
        regs.write_in = __EXPR( wb.regs_write_out() && !cache_busy_comb_func() && wb_write_ready_comb_func() );
        regs.write_addr_in = wb.regs_wr_id_out;
        regs.write_data_in = wb.regs_data_out;
        regs.debugen_in = debugen_in;
        regs.__inst_name = __inst_name + "/regs";
        regs._assign();

        dcache.read_in = __EXPR( exe.mem_read_out() && !dcache_wait_comb_func() );
        dcache.write_in = __EXPR( exe.mem_write_out() && !dcache_wait_comb_func() );
        dcache.addr_in = __EXPR( exe.mem_read_out() ? (uint32_t)exe.mem_read_addr_out() : (uint32_t)exe.mem_write_addr_out() );
        dcache.write_data_in = exe.mem_write_data_out;
        dcache.write_mask_in = exe.mem_write_mask_out;
        dcache.mem_read_data_in = dmem_read_data_in;
        dcache.stall_in = __EXPR(branch_stall_comb_func());
        dcache.flush_in = __EXPR(false);
        dcache.debugen_in = debugen_in;
        dcache.__inst_name = __inst_name + "/dcache";
        dcache._assign();

        icache.read_in = __EXPR( true );
        icache.addr_in = __EXPR( fetch_addr_comb_func() );
        icache.write_in = __EXPR( false );
        icache.write_data_in = __EXPR( (uint32_t)0 );
        icache.write_mask_in = __EXPR( (uint8_t)0 );
        icache.mem_read_data_in = imem_read_data_in;
        icache.stall_in = __EXPR(dcache_wait_comb_func() || stall_comb_func());
        icache.flush_in = __EXPR(state_reg[0].valid && exe.branch_taken_out() && !cache_busy_comb_func());
        icache.debugen_in = debugen_in;
        icache.__inst_name = __inst_name + "/icache";
        icache._assign();

        dmem_write_out      = dcache.mem_write_out;
        dmem_write_data_out = dcache.mem_write_data_out;
        dmem_write_mask_out = dcache.mem_write_mask_out;
        dmem_read_out       = dcache.mem_read_out;
        dmem_addr_out       = dcache.mem_addr_out;
        imem_read_addr_out  = icache.mem_addr_out;
        perf_icache_state_out = icache.perf_state_out;
        perf_dcache_state_out = dcache.perf_state_out;
        perf_icache_hit_out = icache.perf_hit_out;
        perf_icache_lookup_wait_out = icache.perf_lookup_wait_out;
        perf_icache_refill_wait_out = icache.perf_refill_wait_out;
        perf_icache_init_wait_out = icache.perf_init_wait_out;
        perf_icache_issue_wait_out = icache.perf_issue_wait_out;
    }

};

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

#include <algorithm>
#include <fstream>

#include "Ram.h"

#include <tuple>
#include <utility>

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

class TestTribe : public Module
{
    Ram<32,RAM_SIZE,0> imem;
    Ram<32,RAM_SIZE,1> dmem;

#ifdef VERILATOR
    VERILATOR_MODEL tribe;
#else
    Tribe tribe;
#endif

    bool imem_write = false;
    uint32_t imem_write_addr;
    uint32_t imem_write_data;
    bool error;

    uint64_t perf_clocks = 0;
    uint64_t perf_stall = 0;
    uint64_t perf_hazard = 0;
    uint64_t perf_dcache_wait = 0;
    uint64_t perf_icache_wait = 0;
    uint64_t perf_branch = 0;
    uint64_t perf_icache_issue_wait_cycles = 0;
    uint64_t perf_icache_lookup_wait_cycles = 0;
    uint64_t perf_icache_refill_wait_cycles = 0;
    uint64_t perf_icache_init_wait_cycles = 0;
    uint64_t perf_icache_hit_lookup_cycles = 0;

//    size_t i;

public:

    bool      debugen_in;

    TestTribe(bool debug)
    {
        debugen_in = debug;
    }

    ~TestTribe()
    {
    }

    void _assign()
    {
#ifndef VERILATOR
        tribe._assign();

        tribe.dmem_read_data_in = dmem.read_data_out;
        tribe.imem_read_data_in = imem.read_data_out;
        tribe.debugen_in = debugen_in;
        tribe.__inst_name = __inst_name + "/tribe";
        tribe._assign();

        dmem.read_in = tribe.dmem_read_out;
        dmem.read_addr_in = tribe.dmem_addr_out;
        dmem.write_in = tribe.dmem_write_out;
        dmem.write_addr_in = tribe.dmem_addr_out;
        dmem.write_data_in = tribe.dmem_write_data_out;
        dmem.write_mask_in = tribe.dmem_write_mask_out;
        dmem.debugen_in = debugen_in;
        dmem.__inst_name = __inst_name + "/dmem";
        dmem._assign();

        imem.read_in = __EXPR( !imem_write );
        imem.read_addr_in = tribe.imem_read_addr_out;
        imem.write_in = __VAR( imem_write );
        imem.write_addr_in = __VAR( imem_write_addr );
        imem.write_data_in = __VAR( imem_write_data );
        imem.write_mask_in = __EXPR( (uint8_t)0xF );
        imem.debugen_in = debugen_in;
        imem.__inst_name = __inst_name + "/imem";
        imem._assign();
#else  // connecting Verilator to CppHDL
        dmem.read_in = __EXPR( (bool)tribe.dmem_read_out );
        dmem.read_addr_in = __EXPR( (uint32_t)tribe.dmem_addr_out );
        dmem.write_in = __EXPR( (bool)tribe.dmem_write_out );
        dmem.write_addr_in = __EXPR( (uint32_t)tribe.dmem_addr_out );
        dmem.write_data_in = __EXPR( (uint32_t)tribe.dmem_write_data_out );
        dmem.write_mask_in = __EXPR( (uint8_t)tribe.dmem_write_mask_out );
        dmem.debugen_in = debugen_in;
        dmem.__inst_name = __inst_name + "/dmem";
        dmem._assign();

        imem.read_in = __EXPR( (bool)!imem_write );
        imem.read_addr_in = __EXPR( (uint32_t)tribe.imem_read_addr_out );
        imem.write_in = __EXPR( (bool)imem_write );
        imem.write_addr_in = __EXPR( (uint32_t)imem_write_addr );
        imem.write_data_in = __EXPR( (uint32_t)imem_write_data );
        imem.write_mask_in = __EXPR( (uint8_t)0xF );
        imem.debugen_in = debugen_in;
        imem.__inst_name = __inst_name + "/imem";
        imem._assign();
#endif
    }

    void _work(bool reset)
    {
#ifndef VERILATOR
        tribe._work(reset);
#else
//        memcpy(&tribe.data_in.m_storage, data_out, sizeof(tribe.data_in.m_storage));
        tribe.debugen_in    = debugen_in;

//        data_in           = (array<DTYPE,LENGTH>*) &tribe.data_out.m_storage;

        tribe.clk = 1;
        tribe.reset = reset;
        tribe.eval();  // eval of verilator should be in the end
        tribe.dmem_read_data_in = dmem.read_data_out();
        tribe.imem_read_data_in = imem.read_data_out();
#endif
        dmem._work(reset);
        imem._work(reset);

        if (reset) {
            error = false;
            return;
        }
    }

    void _strobe()
    {
#ifndef VERILATOR
        tribe._strobe();
#endif
        dmem._strobe();  // we use these modules in Verilator test
        imem._strobe();
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        tribe.clk = 0;
        tribe.reset = reset;
        tribe.eval();  // eval of verilator should be in the end
#else
        tribe._work_neg(reset);
#endif

        if (debugen_in) {
            printf("----------- %ld\n", sys_clock);
        }
    }

    void _strobe_neg()
    {
    }

    bool perf_hazard_stall()
    {
#ifdef VERILATOR
        return tribe.perf_hazard_stall_out;
#else
        return tribe.perf_hazard_stall_out();
#endif
    }

    bool perf_branch_stall()
    {
#ifdef VERILATOR
        return tribe.perf_branch_stall_out;
#else
        return tribe.perf_branch_stall_out();
#endif
    }

    bool perf_dcache_wait_stall()
    {
#ifdef VERILATOR
        return tribe.perf_dcache_wait_out;
#else
        return tribe.perf_dcache_wait_out();
#endif
    }

    bool perf_icache_wait_stall()
    {
#ifdef VERILATOR
        return tribe.perf_icache_wait_out;
#else
        return tribe.perf_icache_wait_out();
#endif
    }

    bool perf_icache_issue_wait()
    {
#ifdef VERILATOR
        return tribe.perf_icache_issue_wait_out;
#else
        return tribe.perf_icache_issue_wait_out();
#endif
    }

    bool perf_icache_lookup_wait()
    {
#ifdef VERILATOR
        return tribe.perf_icache_lookup_wait_out;
#else
        return tribe.perf_icache_lookup_wait_out();
#endif
    }

    bool perf_icache_refill_wait()
    {
#ifdef VERILATOR
        return tribe.perf_icache_refill_wait_out;
#else
        return tribe.perf_icache_refill_wait_out();
#endif
    }

    bool perf_icache_init_wait()
    {
#ifdef VERILATOR
        return tribe.perf_icache_init_wait_out;
#else
        return tribe.perf_icache_init_wait_out();
#endif
    }

    bool perf_icache_hit_lookup()
    {
#ifdef VERILATOR
        return tribe.perf_icache_hit_out && tribe.perf_icache_lookup_wait_out;
#else
        return tribe.perf_icache_hit_out() && tribe.perf_icache_lookup_wait_out();
#endif
    }

    void perf_reset()
    {
        perf_clocks = 0;
        perf_stall = 0;
        perf_hazard = 0;
        perf_dcache_wait = 0;
        perf_icache_wait = 0;
        perf_branch = 0;
        perf_icache_issue_wait_cycles = 0;
        perf_icache_lookup_wait_cycles = 0;
        perf_icache_refill_wait_cycles = 0;
        perf_icache_init_wait_cycles = 0;
        perf_icache_hit_lookup_cycles = 0;
    }

    void perf_sample()
    {
        bool hazard = perf_hazard_stall();
        bool branch = perf_branch_stall();
        bool dcache_wait = perf_dcache_wait_stall();
        bool icache_wait = perf_icache_wait_stall();

        ++perf_clocks;
        perf_hazard += hazard;
        perf_branch += branch;
        perf_dcache_wait += dcache_wait;
        perf_icache_wait += icache_wait;
        perf_icache_issue_wait_cycles += perf_icache_issue_wait();
        perf_icache_lookup_wait_cycles += perf_icache_lookup_wait();
        perf_icache_refill_wait_cycles += perf_icache_refill_wait();
        perf_icache_init_wait_cycles += perf_icache_init_wait();
        perf_icache_hit_lookup_cycles += perf_icache_hit_lookup();
        perf_stall += hazard || branch || dcache_wait || icache_wait;
    }

    void perf_print()
    {
        auto percent = [&](uint64_t value) {
            return perf_clocks ? (100.0 * (double)value / (double)perf_clocks) : 0.0;
        };

        std::print("Performance: clocks={}, stalled={:.2f}% ({})"
                   ", hazards={:.2f}% ({})"
                   ", dcache_wait={:.2f}% ({})"
                   ", icache_wait={:.2f}% ({})"
                   ", branching={:.2f}% ({})\n",
            perf_clocks,
            percent(perf_stall), perf_stall,
            percent(perf_hazard), perf_hazard,
            percent(perf_dcache_wait), perf_dcache_wait,
            percent(perf_icache_wait), perf_icache_wait,
            percent(perf_branch), perf_branch);
        std::print("I-cache wait detail: issue={:.2f}% ({})"
                   ", lookup={:.2f}% ({})"
                   ", lookup_hit={:.2f}% ({})"
                   ", refill={:.2f}% ({})"
                   ", init={:.2f}% ({})\n",
            percent(perf_icache_issue_wait_cycles), perf_icache_issue_wait_cycles,
            percent(perf_icache_lookup_wait_cycles), perf_icache_lookup_wait_cycles,
            percent(perf_icache_hit_lookup_cycles), perf_icache_hit_lookup_cycles,
            percent(perf_icache_refill_wait_cycles), perf_icache_refill_wait_cycles,
            percent(perf_icache_init_wait_cycles), perf_icache_init_wait_cycles);
    }

    bool run(std::string filename, size_t start_offset)
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTribe...");
#else
        std::print("CppHDL TestTribe...");
#endif
        if (debugen_in) {
            std::print("\n");
        }

        FILE* out = fopen("out.txt", "w");
        fclose(out);

        __inst_name = "tribe_test";
        _assign();
        _strobe();
        ++sys_clock;
        _work(1);
        _strobe_neg();
        _work_neg(1);

        /////////////// read program to memory
        uint32_t ram[RAM_SIZE];
        FILE* fbin = fopen(filename.c_str(), "r");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        fseek(fbin, start_offset, SEEK_SET);
        int read_bytes = fread(ram, 1, 4*RAM_SIZE, fbin);
        std::print("Reading program into memory (size: {}, offset: {})\n", read_bytes, start_offset);

        imem_write = true;
        imem._work(1);
        for (size_t addr = 0; addr < RAM_SIZE; ++addr) {
            imem._strobe();
            ++sys_clock;
            imem_write_addr = addr*4;
            imem_write_data = ram[addr];
            imem._work(0);
            if (debugen_in) {
                std::print("{:04x}: {:08x}\n", addr, ram[addr]);
            }
        }
        imem_write = false;
        fclose(fbin);
        dmem.ram.buffer = imem.ram.buffer;  // we need data from binary
        ///////////////////////////////////////

        auto start = std::chrono::high_resolution_clock::now();
        perf_reset();
        int cycles = 2000000;
        while (--cycles && !error) {
            _strobe();
            ++sys_clock;
            perf_sample();
            _work(0);
            _strobe_neg();
            _work_neg(0);
        }

        std::ifstream a("rv32i.log", std::ios::binary), b("out.txt", std::ios::binary);
        error |= !std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(b));

        perf_print();
        std::print(" {} ({} microseconds)\n", !error?"PASSED":"FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main (int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    int only = -1;
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (argv[i][0] != '-') {
            only = atoi(argv[argc-1]);
        }
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "Tribe", {"Predef_pkg",
                  "State_pkg",
                  "Rv32i_pkg",
                  "Rv32ic_pkg",
                  "Rv32ic_rv16_pkg",
                  "Rv32im_pkg",
                  "Alu_pkg",
                  "Br_pkg",
                  "Mem_pkg",
                  "Wb_pkg",
                  "File",
                  "RAM1PORT",
                  "L1Cache",
                  "Decode",
                  "Execute",
                  "Writeback"}, {"../../../../include", "../../../../tribe", "../../../../tribe/common", "../../../../tribe/spec", "../../../../tribe/cache"});
        std::cout << "Executing tests... ===========================================================================\n";
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Tribe/obj_dir/VTribe") + (debug?" --debug":"") + " 0").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestTribe(debug).run("rv32i.bin", 0x37c))
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

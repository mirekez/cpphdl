#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "File.h"

#define STAGES_NUM 3

#include "Decode.h"
#include "Execute.h"
#include "Writeback.h"
#include "BranchPredictor.h"
#include "cache/L1Cache.h"

#define RAM_SIZE 2048
#define CACHE_ADDR_BITS 13
#define BRANCH_PREDICTOR_ENTRIES 16
#define BRANCH_PREDICTOR_COUNTER_BITS 2

long sys_clock = -1;

struct TribePerf
{
    unsigned hazard_stall:1;
    unsigned branch_stall:1;
    unsigned dcache_wait:1;
    unsigned icache_wait:1;
    L1CachePerf icache;
    L1CachePerf dcache;
} __PACKED;

class Tribe: public Module
{
    Decode          dec;
    Execute         exe;
    Writeback       wb;
    File<32,32>     regs;
    L1Cache<1024,32,2,0,CACHE_ADDR_BITS> icache;
    L1Cache<1024,32,2,1,CACHE_ADDR_BITS> dcache;
    BranchPredictor<BRANCH_PREDICTOR_ENTRIES, BRANCH_PREDICTOR_COUNTER_BITS> bp;

public:

    __PORT(bool)      dmem_write_out;
    __PORT(uint32_t)  dmem_write_data_out;
    __PORT(uint8_t)   dmem_write_mask_out;
    __PORT(bool)      dmem_read_out;
    __PORT(uint32_t)  dmem_addr_out;
    __PORT(uint32_t)  dmem_read_data_in;
    __PORT(uint32_t)  imem_read_addr_out;
    __PORT(uint32_t)  imem_read_data_in;
    __PORT(TribePerf) perf_out = __VAR(perf_comb_func());
    bool              debugen_in;

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
        wb.mem_data_in    = __EXPR(load_data_valid_reg ? (uint32_t)load_data_reg :
            ((state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
              dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg) ?
                dcache.read_data_out() : (uint32_t)0));
        wb.alu_result_in  = __VAR( alu_result_reg );
        wb._assign();  // outputs are ready

        regs.read_addr0_in = __EXPR( (uint8_t)dec.rs1_out() );
        regs.read_addr1_in = __EXPR( (uint8_t)dec.rs2_out() );
        regs.write_in = __EXPR(wb.regs_write_out() &&
            !memory_wait_comb_func() &&
            (state_reg[1].wb_op != Wb::MEM || load_data_valid_reg ||
                (dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg)));
        regs.write_addr_in = wb.regs_wr_id_out;
        regs.write_data_in = wb.regs_data_out;
        regs.debugen_in = debugen_in;
        regs.__inst_name = __inst_name + "/regs";
        regs._assign();

        dcache.read_in = __EXPR( exe.mem_read_out() && !dcache.busy_out() );
        dcache.write_in = __EXPR( exe.mem_write_out() && !dcache.busy_out() );
        dcache.addr_in = __EXPR( exe.mem_read_out() ? (uint32_t)exe.mem_read_addr_out() : (uint32_t)exe.mem_write_addr_out() );
        dcache.write_data_in = exe.mem_write_data_out;
        dcache.write_mask_in = exe.mem_write_mask_out;
        dcache.mem_read_data_in = dmem_read_data_in;
        dcache.stall_in = __EXPR(branch_stall_comb_func());
        dcache.flush_in = __EXPR(false);
        dcache.debugen_in = debugen_in;
        dcache.__inst_name = __inst_name + "/dcache";
        dcache._assign();

        bp.lookup_valid_in = __EXPR(decode_branch_valid_comb_func());
        bp.lookup_pc_in = __EXPR((uint32_t)dec.state_out().pc);
        bp.lookup_target_in = __EXPR(decode_branch_target_comb_func());
        bp.lookup_fallthrough_in = __EXPR(decode_fallthrough_comb_func());
        bp.lookup_br_op_in = __EXPR((u<4>)dec.state_out().br_op);
        bp.update_valid_in = __EXPR(state_reg[0].valid && state_reg[0].br_op != Br::BNONE && !memory_wait_comb_func());
        bp.update_pc_in = __EXPR((uint32_t)state_reg[0].pc);
        bp.update_taken_in = __EXPR(exe.branch_taken_out());
        bp.update_target_in = __EXPR(exe.branch_target_out());
        bp.__inst_name = __inst_name + "/bp";
        bp._assign();

        icache.read_in = __EXPR( true );
        icache.addr_in = __EXPR( fetch_addr_comb_func() );
        icache.write_in = __EXPR( false );
        icache.write_data_in = __EXPR( (uint32_t)0 );
        icache.write_mask_in = __EXPR( (uint8_t)0 );
        icache.mem_read_data_in = imem_read_data_in;
        icache.stall_in = __EXPR(memory_wait_comb_func() || stall_comb_func());
        icache.flush_in = __EXPR(branch_mispredict_comb_func() && !memory_wait_comb_func());
        icache.debugen_in = debugen_in;
        icache.__inst_name = __inst_name + "/icache";
        icache._assign();

        dmem_write_out      = dcache.mem_write_out;
        dmem_write_data_out = dcache.mem_write_data_out;
        dmem_write_mask_out = dcache.mem_write_mask_out;
        dmem_read_out       = dcache.mem_read_out;
        dmem_addr_out       = dcache.mem_addr_out;
        imem_read_addr_out  = icache.mem_addr_out;
    }

private:

    reg<u32>        pc;
    reg<u1>         valid;

    reg<u32>        alu_result_reg;
    reg<u32>        load_data_reg;
    reg<u1>         load_data_valid_reg;

    reg<array<State,STAGES_NUM-1>> state_reg;
    reg<array<u32,STAGES_NUM-1>> predicted_next_reg;
    reg<array<u32,STAGES_NUM-1>> fallthrough_reg;
    reg<array<u1,STAGES_NUM-1>> predicted_taken_reg;

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
        branch_stall_comb = branch_mispredict_comb_func();
        return branch_stall_comb;
    }

    __LAZY_COMB(branch_flush_comb, bool)
        branch_flush_comb = branch_mispredict_comb_func();
        return branch_flush_comb;
    }

    __LAZY_COMB(stall_comb, bool)
        stall_comb = hazard_stall_comb_func() || branch_stall_comb_func();
        return stall_comb;
    }

    __LAZY_COMB(perf_comb, TribePerf)
        perf_comb.hazard_stall = hazard_stall_comb_func();
        perf_comb.branch_stall = branch_stall_comb_func();
        perf_comb.dcache_wait = dcache.busy_out();
        perf_comb.icache_wait = icache.busy_out();
        perf_comb.icache = icache.perf_out();
        perf_comb.dcache = dcache.perf_out();
        return perf_comb;
    }

    __LAZY_COMB(memory_wait_comb, bool)
        memory_wait_comb = dcache.busy_out() || (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
            !(load_data_valid_reg || (dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg)));
        return memory_wait_comb;
    }

    __LAZY_COMB(fetch_valid_comb, bool)
        return fetch_valid_comb = valid && icache.read_valid_out() && icache.read_addr_out() == (uint32_t)pc;
    }

    __LAZY_COMB(decode_fallthrough_comb, uint32_t)
        return decode_fallthrough_comb = pc + ((dec.instr_in()&3)==3?4:2);
    }

    __LAZY_COMB(decode_branch_valid_comb, bool)
        decode_branch_valid_comb = fetch_valid_comb_func() && dec.state_out().valid && dec.state_out().br_op != Br::BNONE && !stall_comb_func();
        return decode_branch_valid_comb;
    }

    __LAZY_COMB(decode_branch_target_comb, uint32_t)
        const auto& dec_state_tmp = dec.state_out();
        decode_branch_target_comb = 0;
        if (dec_state_tmp.br_op == Br::JAL) {
            decode_branch_target_comb = dec_state_tmp.pc + dec_state_tmp.imm;
        }
        else if (dec_state_tmp.br_op == Br::JALR || dec_state_tmp.br_op == Br::JR) {
            decode_branch_target_comb = (dec_state_tmp.rs1_val + dec_state_tmp.imm) & ~1U;
        }
        else {
            decode_branch_target_comb = dec_state_tmp.pc + dec_state_tmp.imm;
        }
        return decode_branch_target_comb;
    }

    __LAZY_COMB(branch_actual_next_comb, uint32_t)
        return branch_actual_next_comb = exe.branch_taken_out() ? exe.branch_target_out() : (uint32_t)fallthrough_reg[0];
    }

    __LAZY_COMB(branch_mispredict_comb, bool)
        branch_mispredict_comb = state_reg[0].valid && state_reg[0].br_op != Br::BNONE &&
            branch_actual_next_comb_func() != (uint32_t)predicted_next_reg[0];
        return branch_mispredict_comb;
    }

    __LAZY_COMB(fetch_addr_comb, uint32_t)
        fetch_addr_comb = pc;
        if (fetch_valid_comb_func() && !stall_comb_func()) {
            fetch_addr_comb = decode_fallthrough_comb_func();
        }
        if (decode_branch_valid_comb_func()) {
            fetch_addr_comb = bp.predict_next_out();
        }
        if (branch_mispredict_comb_func()) {
            fetch_addr_comb = branch_actual_next_comb_func();
        }
        return fetch_addr_comb;
    }

    __LAZY_COMB(exe_state_comb, State)
        exe_state_comb = state_reg[0];
        if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM && state_reg[1].rd != 0 &&
            (load_data_valid_reg || (dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg))) {
            if (state_reg[0].rs1 == state_reg[1].rd) {
                exe_state_comb.rs1_val = load_data_valid_reg ? (uint32_t)load_data_reg : dcache.read_data_out();
            }
            if (state_reg[0].rs2 == state_reg[1].rd) {
                exe_state_comb.rs2_val = load_data_valid_reg ? (uint32_t)load_data_reg : dcache.read_data_out();
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
            (load_data_valid_reg || (dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg))) {  // Mem/Wb mem
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg._next[0].rs1_val = load_data_valid_reg ? (uint32_t)load_data_reg : dcache.read_data_out();
                if (debugen_in) {
                    printf("forwarding %.08x from MEM to RS1\n", load_data_valid_reg ? (uint32_t)load_data_reg : (uint32_t)dcache.read_data_out());
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg._next[0].rs2_val = load_data_valid_reg ? (uint32_t)load_data_reg : dcache.read_data_out();
                if (debugen_in) {
                    printf("forwarding %.08x from MEM to RS2\n", load_data_valid_reg ? (uint32_t)load_data_reg : (uint32_t)dcache.read_data_out());
                }
            }
        }

        if (state_reg[1].valid && (state_reg[1].wb_op == Wb::PC2 || state_reg[1].wb_op == Wb::PC4) && state_reg[1].rd != 0) {  // Mem/Wb link
            uint32_t link_value = state_reg[1].pc + (state_reg[1].wb_op == Wb::PC2 ? 2 : 4);
            if (dec_state_tmp.rs1 == state_reg[1].rd) {
                state_reg._next[0].rs1_val = link_value;
                if (debugen_in) {
                    printf("forwarding %.08x from LINK to RS1\n", link_value);
                }
            }
            if (dec_state_tmp.rs2 == state_reg[1].rd) {
                state_reg._next[0].rs2_val = link_value;
                if (debugen_in) {
                    printf("forwarding %.08x from LINK to RS2\n", link_value);
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

        if (state_reg[0].valid && (state_reg[0].wb_op == Wb::PC2 || state_reg[0].wb_op == Wb::PC4) && state_reg[0].rd != 0) {  // Ex/Mem link
            uint32_t link_value = state_reg[0].pc + (state_reg[0].wb_op == Wb::PC2 ? 2 : 4);
            if (dec_state_tmp.rs1 == state_reg[0].rd) {
                state_reg._next[0].rs1_val = link_value;
                if (debugen_in) {
                    printf("forwarding %.08x from LINK to RS1\n", link_value);
                }
            }
            if (dec_state_tmp.rs2 == state_reg[0].rd) {
                state_reg._next[0].rs2_val = link_value;
                if (debugen_in) {
                    printf("forwarding %.08x from LINK to RS2\n", link_value);
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

        if (memory_wait_comb_func()) {
            pc._next = pc;
            valid._next = valid;
            state_reg._next = state_reg;
            predicted_next_reg._next = predicted_next_reg;
            fallthrough_reg._next = fallthrough_reg;
            predicted_taken_reg._next = predicted_taken_reg;
            alu_result_reg._next = alu_result_reg;
            if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEM &&
                dcache.read_valid_out() && dcache.read_addr_out() == (uint32_t)alu_result_reg) {
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
                pc._next = decode_fallthrough_comb_func();
            }
            if (decode_branch_valid_comb_func()) {
                pc._next = bp.predict_next_out();
            }
            if (branch_mispredict_comb_func()) {
                pc._next = branch_actual_next_comb_func();
            }

            valid._next = true;

            if (hazard_stall_comb_func()) {
                state_reg._next[0] = State{};
                state_reg._next[0].valid = false;
                predicted_next_reg._next[0] = pc;
                fallthrough_reg._next[0] = pc;
                predicted_taken_reg._next[0] = false;
            }
            else {
                state_reg._next[0] = dec.state_out();
                state_reg._next[0].valid = dec.instr_valid_in() && !branch_stall_comb_func() && !branch_flush_comb_func();
                predicted_next_reg._next[0] = decode_branch_valid_comb_func() ? (uint32_t)bp.predict_next_out() : decode_fallthrough_comb_func();
                fallthrough_reg._next[0] = decode_fallthrough_comb_func();
                predicted_taken_reg._next[0] = decode_branch_valid_comb_func() && bp.predict_taken_out();
                forward();
            }
            state_reg._next[1] = state_reg[0];
            predicted_next_reg._next[1] = predicted_next_reg[0];
            fallthrough_reg._next[1] = fallthrough_reg[0];
            predicted_taken_reg._next[1] = predicted_taken_reg[0];
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
        bp._work(reset);

        if (reset) {
            state_reg._next[0].valid = 0;
            state_reg._next[1].valid = 0;
            pc.clr();
            valid.clr();
            load_data_reg.clr();
            load_data_valid_reg.clr();
            predicted_next_reg.clr();
            fallthrough_reg.clr();
            predicted_taken_reg.clr();
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
            (bool)dcache.busy_out(), (bool)icache.busy_out(),
            (uint32_t)icache.perf_out().state, (uint32_t)dcache.perf_out().state,
            (bool)icache.perf_out().hit,
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
        std::print(": {}", interpret);
        debug_alu_a_reg._next = exe.debug_alu_a_out();
        debug_alu_b_reg._next = exe.debug_alu_b_out();
        debug_branch_target_reg = exe.branch_target_out();
#else
#endif
    }

    void _strobe()
    {
        pc.strobe();
        valid.strobe();
        state_reg.strobe();
        predicted_next_reg.strobe();
        fallthrough_reg.strobe();
        predicted_taken_reg.strobe();
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
        bp._strobe();
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
static TribePerf verilator_tribe_perf(uint64_t bits)
{
    uint64_t storage = bits;
    return *reinterpret_cast<TribePerf*>(&storage);
}

#define PORT_VALUE(port) verilator_tribe_perf((uint64_t)(port))
#else
#define PORT_VALUE(port) (port())
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
        auto perf = PORT_VALUE(tribe.perf_out);
        bool hazard = perf.hazard_stall;
        bool branch = perf.branch_stall;
        bool dcache_wait = perf.dcache_wait;
        bool icache_wait = perf.icache_wait;

        ++perf_clocks;
        perf_hazard += hazard;
        perf_branch += branch;
        perf_dcache_wait += dcache_wait;
        perf_icache_wait += icache_wait;
        perf_icache_issue_wait_cycles += perf.icache.issue_wait;
        perf_icache_lookup_wait_cycles += perf.icache.lookup_wait;
        perf_icache_refill_wait_cycles += perf.icache.refill_wait;
        perf_icache_init_wait_cycles += perf.icache.init_wait;
        perf_icache_hit_lookup_cycles += perf.icache.hit && perf.icache.lookup_wait;
        perf_stall += hazard || branch || dcache_wait || icache_wait;
    }

    void debug_perf_counters_print()
    {
        std::print(" perf[c{:04x} s{:04x} h{:04x} b{:04x} dc{:04x} ic{:04x} ii{:04x} il{:04x} ih{:04x} ir{:04x} in{:04x}]\n",
            (uint16_t)perf_clocks,
            (uint16_t)perf_stall,
            (uint16_t)perf_hazard,
            (uint16_t)perf_branch,
            (uint16_t)perf_dcache_wait,
            (uint16_t)perf_icache_wait,
            (uint16_t)perf_icache_issue_wait_cycles,
            (uint16_t)perf_icache_lookup_wait_cycles,
            (uint16_t)perf_icache_hit_lookup_cycles,
            (uint16_t)perf_icache_refill_wait_cycles,
            (uint16_t)perf_icache_init_wait_cycles);
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
            if (debugen_in) {
                debug_perf_counters_print();
            }
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
                  "L1CachePerf_pkg",
                  "TribePerf_pkg",
                  "File",
                  "RAM1PORT",
                  "L1Cache",
                  "BranchPredictor",
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

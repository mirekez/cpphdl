`default_nettype none

import Predef_pkg::*;
import Rv32im_pkg::*;
import Rv32ic_pkg::*;
import Rv32ic_rv16_pkg::*;
import Rv32i_pkg::*;
import Mem_pkg::*;
import Alu_pkg::*;
import Wb_pkg::*;
import Br_pkg::*;
import State_pkg::*;
import L1CachePerf_pkg::*;
import TribePerf_pkg::*;


module Tribe (
    input wire clk
,   input wire reset
,   output wire dmem_write_out
,   output wire[31:0] dmem_write_data_out
,   output wire[7:0] dmem_write_mask_out
,   output wire dmem_read_out
,   output wire[31:0] dmem_addr_out
,   input wire[31:0] dmem_read_data_in
,   output wire[31:0] imem_read_addr_out
,   input wire[31:0] imem_read_data_in
,   output TribePerf perf_out
,   input wire debugen_in
);


    // regs and combs
    reg[32-1:0] pc;
    reg valid;
    reg[32-1:0] alu_result_reg;
    reg[32-1:0] load_data_reg;
    reg load_data_valid_reg;
    State[2-1:0] state_reg;
    reg[2-1:0][32-1:0] predicted_next_reg;
    reg[2-1:0][32-1:0] fallthrough_reg;
    reg[2-1:0] predicted_taken_reg;
    reg[32-1:0] debug_alu_a_reg;
    reg[32-1:0] debug_alu_b_reg;
    reg[32-1:0] debug_branch_target_reg;
    reg debug_branch_taken_reg;
    logic hazard_stall_comb;
;
    logic branch_stall_comb;
;
    logic branch_flush_comb;
;
    logic stall_comb;
;
    TribePerf perf_comb;
;
    logic memory_wait_comb;
;
    logic fetch_valid_comb;
;
    logic[31:0] decode_fallthrough_comb;
;
    logic decode_branch_valid_comb;
;
    logic[31:0] decode_branch_target_comb;
;
    logic[31:0] branch_actual_next_comb;
;
    logic branch_mispredict_comb;
;
    logic[31:0] fetch_addr_comb;
;
    State exe_state_comb;
;

    // members
    genvar gi, gj, gk;
      wire[31:0] dec__pc_in;
      wire dec__instr_valid_in;
      wire[31:0] dec__instr_in;
      wire[31:0] dec__regs_data0_in;
      wire[31:0] dec__regs_data1_in;
      wire[5-1:0] dec__rs1_out;
      wire[5-1:0] dec__rs2_out;
      State dec__state_out;
    Decode      dec (
        .clk(clk)
,       .reset(reset)
,       .pc_in(dec__pc_in)
,       .instr_valid_in(dec__instr_valid_in)
,       .instr_in(dec__instr_in)
,       .regs_data0_in(dec__regs_data0_in)
,       .regs_data1_in(dec__regs_data1_in)
,       .rs1_out(dec__rs1_out)
,       .rs2_out(dec__rs2_out)
,       .state_out(dec__state_out)
    );
      State exe__state_in;
      wire exe__mem_write_out;
      wire[31:0] exe__mem_write_addr_out;
      wire[31:0] exe__mem_write_data_out;
      wire[7:0] exe__mem_write_mask_out;
      wire exe__mem_read_out;
      wire[31:0] exe__mem_read_addr_out;
      wire[31:0] exe__alu_result_out;
      wire[31:0] exe__debug_alu_a_out;
      wire[31:0] exe__debug_alu_b_out;
      wire exe__branch_taken_out;
      wire[31:0] exe__branch_target_out;
    Execute      exe (
        .clk(clk)
,       .reset(reset)
,       .state_in(exe__state_in)
,       .mem_write_out(exe__mem_write_out)
,       .mem_write_addr_out(exe__mem_write_addr_out)
,       .mem_write_data_out(exe__mem_write_data_out)
,       .mem_write_mask_out(exe__mem_write_mask_out)
,       .mem_read_out(exe__mem_read_out)
,       .mem_read_addr_out(exe__mem_read_addr_out)
,       .alu_result_out(exe__alu_result_out)
,       .debug_alu_a_out(exe__debug_alu_a_out)
,       .debug_alu_b_out(exe__debug_alu_b_out)
,       .branch_taken_out(exe__branch_taken_out)
,       .branch_target_out(exe__branch_target_out)
    );
      State wb__state_in;
      wire[31:0] wb__alu_result_in;
      wire[31:0] wb__mem_data_in;
      wire[31:0] wb__regs_data_out;
      wire[7:0] wb__regs_wr_id_out;
      wire wb__regs_write_out;
    Writeback      wb (
        .clk(clk)
,       .reset(reset)
,       .state_in(wb__state_in)
,       .alu_result_in(wb__alu_result_in)
,       .mem_data_in(wb__mem_data_in)
,       .regs_data_out(wb__regs_data_out)
,       .regs_wr_id_out(wb__regs_wr_id_out)
,       .regs_write_out(wb__regs_write_out)
    );
      wire[7:0] regs__write_addr_in;
      wire regs__write_in;
      wire[31:0] regs__write_data_in;
      wire[7:0] regs__read_addr0_in;
      wire[7:0] regs__read_addr1_in;
      wire regs__read_in;
      wire[31:0] regs__read_data0_out;
      wire[31:0] regs__read_data1_out;
      wire regs__debugen_in;
    File #(
        32
,       32
    ) regs (
        .clk(clk)
,       .reset(reset)
,       .write_addr_in(regs__write_addr_in)
,       .write_in(regs__write_in)
,       .write_data_in(regs__write_data_in)
,       .read_addr0_in(regs__read_addr0_in)
,       .read_addr1_in(regs__read_addr1_in)
,       .read_in(regs__read_in)
,       .read_data0_out(regs__read_data0_out)
,       .read_data1_out(regs__read_data1_out)
,       .debugen_in(regs__debugen_in)
    );
      wire icache__write_in;
      wire[31:0] icache__write_data_in;
      wire[7:0] icache__write_mask_in;
      wire icache__read_in;
      wire[31:0] icache__addr_in;
      wire[31:0] icache__read_data_out;
      wire[31:0] icache__read_addr_out;
      wire icache__read_valid_out;
      wire icache__busy_out;
      wire icache__stall_in;
      wire icache__flush_in;
      wire icache__mem_write_out;
      wire[31:0] icache__mem_write_data_out;
      wire[7:0] icache__mem_write_mask_out;
      wire icache__mem_read_out;
      wire[31:0] icache__mem_addr_out;
      wire[31:0] icache__mem_read_data_in;
      L1CachePerf icache__perf_out;
      wire icache__debugen_in;
    L1Cache #(
        1024
,       32
,       2
,       0
,       13
    ) icache (
        .clk(clk)
,       .reset(reset)
,       .write_in(icache__write_in)
,       .write_data_in(icache__write_data_in)
,       .write_mask_in(icache__write_mask_in)
,       .read_in(icache__read_in)
,       .addr_in(icache__addr_in)
,       .read_data_out(icache__read_data_out)
,       .read_addr_out(icache__read_addr_out)
,       .read_valid_out(icache__read_valid_out)
,       .busy_out(icache__busy_out)
,       .stall_in(icache__stall_in)
,       .flush_in(icache__flush_in)
,       .mem_write_out(icache__mem_write_out)
,       .mem_write_data_out(icache__mem_write_data_out)
,       .mem_write_mask_out(icache__mem_write_mask_out)
,       .mem_read_out(icache__mem_read_out)
,       .mem_addr_out(icache__mem_addr_out)
,       .mem_read_data_in(icache__mem_read_data_in)
,       .perf_out(icache__perf_out)
,       .debugen_in(icache__debugen_in)
    );
      wire dcache__write_in;
      wire[31:0] dcache__write_data_in;
      wire[7:0] dcache__write_mask_in;
      wire dcache__read_in;
      wire[31:0] dcache__addr_in;
      wire[31:0] dcache__read_data_out;
      wire[31:0] dcache__read_addr_out;
      wire dcache__read_valid_out;
      wire dcache__busy_out;
      wire dcache__stall_in;
      wire dcache__flush_in;
      wire dcache__mem_write_out;
      wire[31:0] dcache__mem_write_data_out;
      wire[7:0] dcache__mem_write_mask_out;
      wire dcache__mem_read_out;
      wire[31:0] dcache__mem_addr_out;
      wire[31:0] dcache__mem_read_data_in;
      L1CachePerf dcache__perf_out;
      wire dcache__debugen_in;
    L1Cache #(
        1024
,       32
,       2
,       1
,       13
    ) dcache (
        .clk(clk)
,       .reset(reset)
,       .write_in(dcache__write_in)
,       .write_data_in(dcache__write_data_in)
,       .write_mask_in(dcache__write_mask_in)
,       .read_in(dcache__read_in)
,       .addr_in(dcache__addr_in)
,       .read_data_out(dcache__read_data_out)
,       .read_addr_out(dcache__read_addr_out)
,       .read_valid_out(dcache__read_valid_out)
,       .busy_out(dcache__busy_out)
,       .stall_in(dcache__stall_in)
,       .flush_in(dcache__flush_in)
,       .mem_write_out(dcache__mem_write_out)
,       .mem_write_data_out(dcache__mem_write_data_out)
,       .mem_write_mask_out(dcache__mem_write_mask_out)
,       .mem_read_out(dcache__mem_read_out)
,       .mem_addr_out(dcache__mem_addr_out)
,       .mem_read_data_in(dcache__mem_read_data_in)
,       .perf_out(dcache__perf_out)
,       .debugen_in(dcache__debugen_in)
    );
      wire bp__lookup_valid_in;
      wire[31:0] bp__lookup_pc_in;
      wire[31:0] bp__lookup_target_in;
      wire[31:0] bp__lookup_fallthrough_in;
      wire[4-1:0] bp__lookup_br_op_in;
      wire bp__predict_taken_out;
      wire[31:0] bp__predict_next_out;
      wire bp__update_valid_in;
      wire[31:0] bp__update_pc_in;
      wire bp__update_taken_in;
      wire[31:0] bp__update_target_in;
    BranchPredictor #(
        16
,       2
    ) bp (
        .clk(clk)
,       .reset(reset)
,       .lookup_valid_in(bp__lookup_valid_in)
,       .lookup_pc_in(bp__lookup_pc_in)
,       .lookup_target_in(bp__lookup_target_in)
,       .lookup_fallthrough_in(bp__lookup_fallthrough_in)
,       .lookup_br_op_in(bp__lookup_br_op_in)
,       .predict_taken_out(bp__predict_taken_out)
,       .predict_next_out(bp__predict_next_out)
,       .update_valid_in(bp__update_valid_in)
,       .update_pc_in(bp__update_pc_in)
,       .update_taken_in(bp__update_taken_in)
,       .update_target_in(bp__update_target_in)
    );

    // tmp variables
    logic[32-1:0] pc_tmp;
    logic valid_tmp;
    logic[32-1:0] alu_result_reg_tmp;
    logic[32-1:0] load_data_reg_tmp;
    logic load_data_valid_reg_tmp;
    State[2-1:0] state_reg_tmp;
    logic[2-1:0][32-1:0] predicted_next_reg_tmp;
    logic[2-1:0][32-1:0] fallthrough_reg_tmp;
    logic[2-1:0] predicted_taken_reg_tmp;
    logic[32-1:0] debug_alu_a_reg_tmp;
    logic[32-1:0] debug_alu_b_reg_tmp;
    logic[32-1:0] debug_branch_target_reg_tmp;
    logic debug_branch_taken_reg_tmp;


    always @(*) begin  // hazard_stall_comb_func
        hazard_stall_comb=0;
        if ((state_reg['h0].valid && (state_reg['h0].wb_op == Wb_pkg::MEM)) && (state_reg['h0].rd != 'h0)) begin
            if (state_reg['h0].rd == dec__state_out.rs1) begin
                hazard_stall_comb=1;
            end
            if (state_reg['h0].rd == dec__state_out.rs2) begin
                hazard_stall_comb=1;
            end
        end
    end

    always @(*) begin  // branch_actual_next_comb_func
        branch_actual_next_comb=(exe__branch_taken_out) ? (exe__branch_target_out) : (unsigned'(32'(fallthrough_reg['h0])));
    end

    always @(*) begin  // branch_mispredict_comb_func
        branch_mispredict_comb=(state_reg['h0].valid && (state_reg['h0].br_op != Br_pkg::BNONE)) && (branch_actual_next_comb != unsigned'(32'(predicted_next_reg['h0])));
    end

    always @(*) begin  // branch_stall_comb_func
        branch_stall_comb=branch_mispredict_comb;
    end

    always @(*) begin  // perf_comb_func
        perf_comb.hazard_stall=hazard_stall_comb;
        perf_comb.branch_stall=branch_stall_comb;
        perf_comb.dcache_wait=dcache__busy_out;
        perf_comb.icache_wait=icache__busy_out;
        perf_comb.icache = icache__perf_out;
        perf_comb.dcache = dcache__perf_out;
    end

    always @(*) begin  // branch_flush_comb_func
        branch_flush_comb=branch_mispredict_comb;
    end

    always @(*) begin  // stall_comb_func
        stall_comb=hazard_stall_comb || branch_stall_comb;
    end

    always @(*) begin  // memory_wait_comb_func
        memory_wait_comb=dcache__busy_out || (((state_reg['h1].valid && (state_reg['h1].wb_op == Wb_pkg::MEM)) && !((load_data_valid_reg || ((dcache__read_valid_out && (dcache__read_addr_out == unsigned'(32'(alu_result_reg)))))))));
    end

    always @(*) begin  // fetch_valid_comb_func
        fetch_valid_comb=(valid && icache__read_valid_out) && (icache__read_addr_out == unsigned'(32'(pc)));
    end

    always @(*) begin  // decode_fallthrough_comb_func
        decode_fallthrough_comb=pc + (((((dec__instr_in & 'h3)) == 'h3)) ? ('h4) : ('h2));
    end

    always @(*) begin  // decode_branch_valid_comb_func
        decode_branch_valid_comb=((fetch_valid_comb && dec__state_out.valid) && (dec__state_out.br_op != Br_pkg::BNONE)) && !stall_comb;
    end

    always @(*) begin  // decode_branch_target_comb_func
        decode_branch_target_comb='h0;
        if (dec__state_out.br_op == Br_pkg::JAL) begin
            decode_branch_target_comb=dec__state_out.pc + dec__state_out.imm;
        end
        else begin
            if ((dec__state_out.br_op == Br_pkg::JALR) || (dec__state_out.br_op == Br_pkg::JR)) begin
                decode_branch_target_comb=((dec__state_out.rs1_val + dec__state_out.imm)) & ~'h1;
            end
            else begin
                decode_branch_target_comb=dec__state_out.pc + dec__state_out.imm;
            end
        end
    end

    always @(*) begin  // fetch_addr_comb_func
        fetch_addr_comb=pc;
        if (fetch_valid_comb && !stall_comb) begin
            fetch_addr_comb=decode_fallthrough_comb;
        end
        if (decode_branch_valid_comb) begin
            fetch_addr_comb=bp__predict_next_out;
        end
        if (branch_mispredict_comb) begin
            fetch_addr_comb=branch_actual_next_comb;
        end
    end

    always @(*) begin  // exe_state_comb_func
        exe_state_comb = state_reg['h0];
        if (((state_reg['h1].valid && (state_reg['h1].wb_op == Wb_pkg::MEM)) && (state_reg['h1].rd != 'h0)) && ((load_data_valid_reg || ((dcache__read_valid_out && (dcache__read_addr_out == unsigned'(32'(alu_result_reg)))))))) begin
            if (state_reg['h0].rs1 == state_reg['h1].rd) begin
                exe_state_comb.rs1_val=(load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : (dcache__read_data_out);
            end
            if (state_reg['h0].rs2 == state_reg['h1].rd) begin
                exe_state_comb.rs2_val=(load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : (dcache__read_data_out);
            end
        end
    end

    task forward ();
    begin: forward
        if ((state_reg['h1].valid && (state_reg['h1].wb_op == Wb_pkg::ALU)) && (state_reg['h1].rd != 'h0)) begin
            if (dec__state_out.rs1 == state_reg['h1].rd) begin
                state_reg_tmp['h0].rs1_val=alu_result_reg;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS1\n", unsigned'(32'(alu_result_reg)));
                end
            end
            if (dec__state_out.rs2 == state_reg['h1].rd) begin
                state_reg_tmp['h0].rs2_val=alu_result_reg;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS2\n", unsigned'(32'(alu_result_reg)));
                end
            end
        end
        if (((state_reg['h1].valid && (state_reg['h1].wb_op == Wb_pkg::MEM)) && (state_reg['h1].rd != 'h0)) && ((load_data_valid_reg || ((dcache__read_valid_out && (dcache__read_addr_out == unsigned'(32'(alu_result_reg)))))))) begin
            if (dec__state_out.rs1 == state_reg['h1].rd) begin
                state_reg_tmp['h0].rs1_val=(load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : (dcache__read_data_out);
                if (debugen_in) begin
                    $write("forwarding %.08x from MEM to RS1\n", (load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : (unsigned'(32'(dcache__read_data_out))));
                end
            end
            if (dec__state_out.rs2 == state_reg['h1].rd) begin
                state_reg_tmp['h0].rs2_val=(load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : (dcache__read_data_out);
                if (debugen_in) begin
                    $write("forwarding %.08x from MEM to RS2\n", (load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : (unsigned'(32'(dcache__read_data_out))));
                end
            end
        end
        if ((state_reg['h1].valid && (((state_reg['h1].wb_op == Wb_pkg::PC2) || (state_reg['h1].wb_op == Wb_pkg::PC4)))) && (state_reg['h1].rd != 'h0)) begin
            logic[31:0] link_value; link_value = state_reg['h1].pc + (((state_reg['h1].wb_op == Wb_pkg::PC2)) ? ('h2) : ('h4));
            if (dec__state_out.rs1 == state_reg['h1].rd) begin
                state_reg_tmp['h0].rs1_val=link_value;
                if (debugen_in) begin
                    $write("forwarding %.08x from LINK to RS1\n", link_value);
                end
            end
            if (dec__state_out.rs2 == state_reg['h1].rd) begin
                state_reg_tmp['h0].rs2_val=link_value;
                if (debugen_in) begin
                    $write("forwarding %.08x from LINK to RS2\n", link_value);
                end
            end
        end
        if ((state_reg['h0].valid && (state_reg['h0].wb_op == Wb_pkg::ALU)) && (state_reg['h0].rd != 'h0)) begin
            if (dec__state_out.rs1 == state_reg['h0].rd) begin
                state_reg_tmp['h0].rs1_val=exe__alu_result_out;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS1\n", unsigned'(32'(exe__alu_result_out)));
                end
            end
            if (dec__state_out.rs2 == state_reg['h0].rd) begin
                state_reg_tmp['h0].rs2_val=exe__alu_result_out;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS2\n", unsigned'(32'(exe__alu_result_out)));
                end
            end
        end
        if ((state_reg['h0].valid && (((state_reg['h0].wb_op == Wb_pkg::PC2) || (state_reg['h0].wb_op == Wb_pkg::PC4)))) && (state_reg['h0].rd != 'h0)) begin
            logic[31:0] link_value; link_value = state_reg['h0].pc + (((state_reg['h0].wb_op == Wb_pkg::PC2)) ? ('h2) : ('h4));
            if (dec__state_out.rs1 == state_reg['h0].rd) begin
                state_reg_tmp['h0].rs1_val=link_value;
                if (debugen_in) begin
                    $write("forwarding %.08x from LINK to RS1\n", link_value);
                end
            end
            if (dec__state_out.rs2 == state_reg['h0].rd) begin
                state_reg_tmp['h0].rs2_val=link_value;
                if (debugen_in) begin
                    $write("forwarding %.08x from LINK to RS2\n", link_value);
                end
            end
        end
    end
    endtask

    function logic signed[31:0] Rv32i___sext (
        input Rv32i _this
,       input logic[31:0] val
,       input logic[31:0] bits
    );
        logic signed[31:0] m; m = 'h1 <<< ((bits - 'h1));
        return ((val ^ m)) - m;
    endfunction

    function logic signed[31:0] Rv32i___imm_I (input Rv32i _this);
        return Rv32i___sext(_this, _this._.i.imm11_0, 'hC);
    endfunction

    function logic signed[31:0] Rv32i___imm_S (input Rv32i _this);
        return Rv32i___sext(_this, _this._.s.imm4_0 | ((_this._.s.imm11_5 <<< 'h5)), 'hC);
    endfunction

    function logic signed[31:0] Rv32i___imm_B (input Rv32i _this);
        return Rv32i___sext(_this, ((((_this._.b.imm4_1 <<< 'h1)) | ((_this._.b.imm11 <<< 'hB))) | ((_this._.b.imm10_5 <<< 'h5))) | ((_this._.b.imm12 <<< 'hC)), 'hD);
    endfunction

    function logic signed[31:0] Rv32i___imm_J (input Rv32i _this);
        return Rv32i___sext(_this, ((((_this._.j.imm10_1 <<< 'h1)) | ((_this._.j.imm11 <<< 'hB))) | ((_this._.j.imm19_12 <<< 'hC))) | ((_this._.j.imm20 <<< 'h14)), 'h15);
    endfunction

    function logic signed[31:0] Rv32i___imm_U (input Rv32i _this);
        return signed'(32'(_this._.u.imm31_12 <<< 'hC));
    endfunction

    task Rv32i___decode (
        input Rv32i _this
,       output State state_out
    );
    begin: Rv32i___decode
        state_out = 0;
        if (_this._.r.opcode == 'h3) begin
            state_out.rd=_this._.i.rd;
            state_out.imm=Rv32i___imm_I(_this);
            state_out.mem_op=Mem_pkg::LOAD;
            state_out.alu_op=Alu_pkg::ADD;
            state_out.wb_op=Wb_pkg::MEM;
            state_out.funct3=_this._.i.funct3;
            state_out.rs1=_this._.i.rs1;
        end
        else begin
            if (_this._.r.opcode == 'h23) begin
                state_out.imm=Rv32i___imm_S(_this);
                state_out.mem_op=Mem_pkg::STORE;
                state_out.alu_op=Alu_pkg::ADD;
                state_out.funct3=_this._.s.funct3;
                state_out.rs1=_this._.s.rs1;
                state_out.rs2=_this._.s.rs2;
            end
            else begin
                if (_this._.r.opcode == 'h13) begin
                    state_out.rd=_this._.i.rd;
                    state_out.imm=Rv32i___imm_I(_this);
                    state_out.wb_op=Wb_pkg::ALU;
                    case (_this._.i.funct3)
                    'h0: begin
                        state_out.alu_op=Alu_pkg::ADD;
                    end
                    'h2: begin
                        state_out.alu_op=Alu_pkg::SLT;
                    end
                    'h3: begin
                        state_out.alu_op=Alu_pkg::SLTU;
                    end
                    'h4: begin
                        state_out.alu_op=Alu_pkg::XOR;
                    end
                    'h6: begin
                        state_out.alu_op=Alu_pkg::OR;
                    end
                    'h7: begin
                        state_out.alu_op=Alu_pkg::AND;
                    end
                    'h1: begin
                        state_out.alu_op=Alu_pkg::SLL;
                    end
                    'h5: begin
                        state_out.alu_op=(((_this._.i.imm11_0 >>> 'hA)) & 'h1) ? (Alu_pkg::SRA) : (Alu_pkg::SRL);
                    end
                    endcase
                    state_out.funct3=_this._.i.funct3;
                    state_out.rs1=_this._.i.rs1;
                end
                else begin
                    if (_this._.r.opcode == 'h33) begin
                        state_out.rd=_this._.r.rd;
                        state_out.wb_op=Wb_pkg::ALU;
                        case (_this._.r.funct3)
                        'h0: begin
                            state_out.alu_op=((_this._.r.funct7 == 'h20)) ? (Alu_pkg::SUB) : (Alu_pkg::ADD);
                        end
                        'h7: begin
                            state_out.alu_op=((_this._.r.funct7 == 'h1)) ? (Alu_pkg::REM) : (Alu_pkg::AND);
                        end
                        'h6: begin
                            state_out.alu_op=Alu_pkg::OR;
                        end
                        'h4: begin
                            state_out.alu_op=Alu_pkg::XOR;
                        end
                        'h1: begin
                            state_out.alu_op=Alu_pkg::SLL;
                        end
                        'h5: begin
                            state_out.alu_op=((_this._.r.funct7 == 'h20)) ? (Alu_pkg::SRA) : (Alu_pkg::SRL);
                        end
                        'h2: begin
                            state_out.alu_op=Alu_pkg::SLT;
                        end
                        'h3: begin
                            state_out.alu_op=Alu_pkg::SLTU;
                        end
                        endcase
                        state_out.funct3=_this._.r.funct3;
                        state_out.rs1=_this._.r.rs1;
                        state_out.rs2=_this._.r.rs2;
                    end
                    else begin
                        if (_this._.r.opcode == 'h63) begin
                            state_out.imm=Rv32i___imm_B(_this);
                            state_out.br_op=Br_pkg::BNONE;
                            case (_this._.b.funct3)
                            'h0: begin
                                state_out.br_op=Br_pkg::BEQ;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h1: begin
                                state_out.br_op=Br_pkg::BNE;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h4: begin
                                state_out.br_op=Br_pkg::BLT;
                                state_out.alu_op=Alu_pkg::SLT;
                            end
                            'h5: begin
                                state_out.br_op=Br_pkg::BGE;
                                state_out.alu_op=Alu_pkg::SLT;
                            end
                            'h6: begin
                                state_out.br_op=Br_pkg::BLTU;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h7: begin
                                state_out.br_op=Br_pkg::BGEU;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            endcase
                            state_out.funct3=_this._.b.funct3;
                            state_out.rs1=_this._.b.rs1;
                            state_out.rs2=_this._.b.rs2;
                        end
                        else begin
                            if (_this._.r.opcode == 'h6F) begin
                                state_out.rd=_this._.j.rd;
                                state_out.imm=Rv32i___imm_J(_this);
                                state_out.br_op=Br_pkg::JAL;
                                state_out.wb_op=Wb_pkg::PC4;
                            end
                            else begin
                                if (_this._.r.opcode == 'h67) begin
                                    state_out.rd=_this._.i.rd;
                                    state_out.imm=Rv32i___imm_I(_this);
                                    state_out.br_op=Br_pkg::JALR;
                                    state_out.wb_op=Wb_pkg::PC4;
                                    state_out.rs1=_this._.i.rs1;
                                end
                                else begin
                                    if (_this._.r.opcode == 'h37) begin
                                        state_out.rd=_this._.u.rd;
                                        state_out.imm=Rv32i___imm_U(_this);
                                        state_out.alu_op=Alu_pkg::PASS;
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (_this._.r.opcode == 'h17) begin
                                            state_out.rd=_this._.u.rd;
                                            state_out.imm=Rv32i___imm_U(_this);
                                            state_out.alu_op=Alu_pkg::ADD;
                                            state_out.wb_op=Wb_pkg::ALU;
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    endtask

    function logic[31:0] Rv32ic___bits (
        input Rv32ic _this
,       input logic signed[31:0] hi
,       input logic signed[31:0] lo
    );
        return ((_this._.raw >>> lo)) & (((('h1 <<< (((hi - lo) + 'h1)))) - 'h1));
    endfunction

    function logic[31:0] Rv32ic___bit (
        input Rv32ic _this
,       input logic signed[31:0] lo
    );
        return ((_this._.raw >>> lo)) & 'h1;
    endfunction

    task Rv32ic___decode (
        input Rv32ic _this
,       output State state_out
    );
    begin: Rv32ic___decode
        logic signed[31:0] imm_tmp;
        Rv32ic_rv16 i; i = {unsigned'(16'(_this._.raw))};
        state_out = 0;
        if (((_this._.raw & 'h3)) == 'h3) begin
            Rv32i___decode(_this, state_out);
            disable Rv32ic___decode;
        end
        state_out.funct3='h2;
        if (i.base.opcode == 'h0) begin
            if (i.base.funct3 == 'h0) begin
                state_out.rd=i.base.rd_p + 'h8;
                state_out.rs1='h2;
                state_out.imm=(((Rv32ic___bits(_this, 'hA, 'h7) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hB) <<< 'h4))) | ((Rv32ic___bits(_this, 'h6, 'h5) <<< 'h2));
                state_out.alu_op=Alu_pkg::ADD;
                state_out.wb_op=Wb_pkg::ALU;
            end
            else begin
                if (i.base.funct3 == 'h2) begin
                    state_out.rd=i.base.rd_p + 'h8;
                    state_out.rs1=i.base.rs1_p + 'h8;
                    state_out.imm=(((Rv32ic___bit(_this, 'h5) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hA) <<< 'h3))) | ((Rv32ic___bit(_this, 'h6) <<< 'h2));
                    state_out.alu_op=Alu_pkg::ADD;
                    state_out.mem_op=Mem_pkg::LOAD;
                    state_out.wb_op=Wb_pkg::MEM;
                end
                else begin
                    if (i.base.funct3 == 'h6) begin
                        state_out.rs1=i.base.rs1_p + 'h8;
                        state_out.rs2=i.base.rd_p + 'h8;
                        state_out.imm=(((Rv32ic___bit(_this, 'h5) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hA) <<< 'h3))) | ((Rv32ic___bit(_this, 'h6) <<< 'h2));
                        state_out.alu_op=Alu_pkg::ADD;
                        state_out.mem_op=Mem_pkg::STORE;
                    end
                end
            end
        end
        else begin
            if (i.base.opcode == 'h1) begin
                if (i.base.funct3 == 'h0) begin
                    state_out.rd=i.avg.rs1;
                    state_out.rs1=i.avg.rs1;
                    imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                    imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                    state_out.imm=imm_tmp;
                    state_out.alu_op=Alu_pkg::ADD;
                    state_out.wb_op=Wb_pkg::ALU;
                end
                else begin
                    if (i.base.funct3 == 'h1) begin
                        state_out.rd='h1;
                        state_out.wb_op=Wb_pkg::PC2;
                        state_out.br_op=Br_pkg::JAL;
                        state_out.imm=Rv32i___sext(_this, ((((((((i.base.b12 <<< 'hB)) | ((Rv32ic___bit(_this, 'h8) <<< 'hA))) | ((Rv32ic___bits(_this, 'hA, 'h9) <<< 'h8))) | ((Rv32ic___bit(_this, 'h6) <<< 'h7))) | ((Rv32ic___bit(_this, 'h7) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'hB) <<< 'h4))) | ((Rv32ic___bits(_this, 'h5, 'h3) <<< 'h1)), 'hC);
                    end
                    else begin
                        if (i.base.funct3 == 'h2) begin
                            state_out.rd=i.avg.rs1;
                            imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                            imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                            state_out.imm=imm_tmp;
                            state_out.alu_op=Alu_pkg::PASS;
                            state_out.wb_op=Wb_pkg::ALU;
                        end
                        else begin
                            if (i.base.funct3 == 'h3) begin
                                state_out.rd='h2;
                                state_out.rs1='h2;
                                imm_tmp=((((((Rv32ic___bit(_this, 'hC) <<< 'h9)) | ((Rv32ic___bit(_this, 'h4) <<< 'h8))) | ((Rv32ic___bit(_this, 'h3) <<< 'h7))) | ((Rv32ic___bit(_this, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'h6) <<< 'h4));
                                imm_tmp=((imm_tmp <<< 'h16)) >>> 'h16;
                                state_out.imm=imm_tmp;
                                state_out.alu_op=Alu_pkg::ADD;
                                state_out.wb_op=Wb_pkg::ALU;
                            end
                            else begin
                                if (i.base.funct3 == 'h4) begin
                                    if (i.base.bits11_10 == 'h0) begin
                                        state_out.rd=i.base.rs1_p + 'h8;
                                        state_out.rs1=i.base.rs1_p + 'h8;
                                        state_out.imm=Rv32ic___bits(_this, 'h6, 'h2);
                                        state_out.alu_op=Alu_pkg::SRL;
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (i.base.bits11_10 == 'h1) begin
                                            state_out.rd=i.base.rs1_p + 'h8;
                                            state_out.rs1=i.base.rs1_p + 'h8;
                                            state_out.imm=Rv32ic___bits(_this, 'h6, 'h2);
                                            state_out.alu_op=Alu_pkg::SRA;
                                            state_out.wb_op=Wb_pkg::ALU;
                                        end
                                        else begin
                                            if (i.base.bits11_10 == 'h2) begin
                                                state_out.rd=i.base.rs1_p + 'h8;
                                                state_out.rs1=i.base.rs1_p + 'h8;
                                                imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                                                imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                                                state_out.imm=imm_tmp;
                                                state_out.alu_op=Alu_pkg::AND;
                                                state_out.wb_op=Wb_pkg::ALU;
                                            end
                                            else begin
                                                if ((i.base.bits11_10 == 'h3) && (i.base.b12 == 'h0)) begin
                                                    state_out.rd=i.base.rs1_p + 'h8;
                                                    state_out.rs1=i.base.rs1_p + 'h8;
                                                    state_out.rs2=i.base.rd_p + 'h8;
                                                    state_out.alu_op=(i.base.bits6_5 == 'h0) ? (Alu_pkg::SUB) : (((i.base.bits6_5 == 'h1) ? (Alu_pkg::XOR) : (((i.base.bits6_5 == 'h2) ? (Alu_pkg::OR) : (Alu_pkg::AND)))));
                                                    state_out.wb_op=Wb_pkg::ALU;
                                                end
                                            end
                                        end
                                    end
                                end
                                else begin
                                    if (i.base.funct3 == 'h5) begin
                                        state_out.rd='h0;
                                        state_out.br_op=Br_pkg::JAL;
                                        state_out.imm=Rv32i___sext(_this, ((((((((i.base.b12 <<< 'hB)) | ((Rv32ic___bit(_this, 'h8) <<< 'hA))) | ((Rv32ic___bits(_this, 'hA, 'h9) <<< 'h8))) | ((Rv32ic___bit(_this, 'h6) <<< 'h7))) | ((Rv32ic___bit(_this, 'h7) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'hB) <<< 'h4))) | ((Rv32ic___bits(_this, 'h5, 'h3) <<< 'h1)), 'hC);
                                    end
                                    else begin
                                        if (i.base.funct3 == 'h6) begin
                                            state_out.rs1=i.base.rs1_p + 'h8;
                                            state_out.br_op=Br_pkg::BEQZ;
                                            state_out.alu_op=Alu_pkg::SLTU;
                                            state_out.imm=(((((i.base.b12 <<< 'h8)) | ((Rv32ic___bits(_this, 'h6, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bits(_this, 'hB, 'hA) <<< 'h3))) | ((Rv32ic___bits(_this, 'h4, 'h3) <<< 'h1));
                                            if (i.base.b12) begin
                                                state_out.imm|=~'h1FF;
                                            end
                                        end
                                        else begin
                                            if (i.base.funct3 == 'h7) begin
                                                state_out.rs1=i.base.rs1_p + 'h8;
                                                state_out.br_op=Br_pkg::BNEZ;
                                                state_out.alu_op=Alu_pkg::SLTU;
                                                state_out.imm=(((((i.base.b12 <<< 'h8)) | ((Rv32ic___bits(_this, 'h6, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bits(_this, 'hB, 'hA) <<< 'h3))) | ((Rv32ic___bits(_this, 'h4, 'h3) <<< 'h1));
                                                if (i.base.b12) begin
                                                    state_out.imm|=~'h1FF;
                                                end
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
            else begin
                if (i.base.opcode == 'h2) begin
                    if (i.base.funct3 == 'h0) begin
                        state_out.rd=i.big.rs1;
                        state_out.rs1=i.big.rs1;
                        state_out.imm=((i.base.b12 <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                        state_out.alu_op=Alu_pkg::SLL;
                        state_out.wb_op=Wb_pkg::ALU;
                    end
                    else begin
                        if (i.base.funct3 == 'h2) begin
                            state_out.rd=i.big.rs1;
                            state_out.rs1='h2;
                            state_out.imm=(((i.base.b12 <<< 'h5)) | ((Rv32ic___bits(_this, 'h6, 'h4) <<< 'h2))) | ((Rv32ic___bits(_this, 'h3, 'h2) <<< 'h6));
                            state_out.alu_op=Alu_pkg::ADD;
                            state_out.mem_op=Mem_pkg::LOAD;
                            state_out.wb_op=Wb_pkg::MEM;
                        end
                        else begin
                            if (i.base.funct3 == 'h4) begin
                                if (i.big.rs2 != 'h0) begin
                                    state_out.rd=i.big.rs1;
                                    state_out.rs2=i.big.rs2;
                                    if (i.base.b12 == 'h0) begin
                                        state_out.alu_op=Alu_pkg::PASS;
                                    end
                                    else begin
                                        state_out.rs1=i.big.rs1;
                                        state_out.alu_op=Alu_pkg::ADD;
                                    end
                                    state_out.wb_op=Wb_pkg::ALU;
                                end
                                else begin
                                    if ((i.big.rs2 == 'h0) && (i.base.b12 == 'h0)) begin
                                        state_out.rs1=i.big.rs1;
                                        state_out.br_op=Br_pkg::JR;
                                        state_out.wb_op=Wb_pkg::PC2;
                                    end
                                    else begin
                                        if ((i.big.rs2 == 'h0) && (i.base.b12 == 'h1)) begin
                                            state_out.rs1=i.big.rs1;
                                            state_out.rd='h1;
                                            state_out.br_op=Br_pkg::JALR;
                                            state_out.wb_op=Wb_pkg::PC2;
                                        end
                                    end
                                end
                            end
                            else begin
                                if (i.base.funct3 == 'h6) begin
                                    state_out.rs1='h2;
                                    state_out.rs2=i.big.rs2;
                                    state_out.imm=((Rv32ic___bits(_this, 'h8, 'h7) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'h9) <<< 'h2));
                                    state_out.mem_op=Mem_pkg::STORE;
                                    state_out.alu_op=Alu_pkg::ADD;
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    endtask

    task Rv32im___decode (
        input Rv32im _this
,       output State state_out
    );
    begin: Rv32im___decode
        state_out = 0;
        Rv32ic___decode(_this, state_out);
        if ((_this._.r.opcode == 'h33) && (_this._.r.funct7 == 'h1)) begin
            state_out.rd=_this._.r.rd;
            state_out.wb_op=Wb_pkg::ALU;
            case (_this._.r.funct3)
            'h0: begin
                state_out.alu_op=Alu_pkg::MUL;
            end
            'h1: begin
                state_out.alu_op=Alu_pkg::MULH;
            end
            'h2: begin
                state_out.alu_op=Alu_pkg::MULHSU;
            end
            'h3: begin
                state_out.alu_op=Alu_pkg::MULHU;
            end
            'h4: begin
                state_out.alu_op=Alu_pkg::DIV;
            end
            'h5: begin
                state_out.alu_op=Alu_pkg::DIVU;
            end
            'h6: begin
                state_out.alu_op=Alu_pkg::REM;
            end
            'h7: begin
                state_out.alu_op=Alu_pkg::REMU;
            end
            endcase
            state_out.funct3=_this._.r.funct3;
            state_out.rs1=_this._.r.rs1;
            state_out.rs2=_this._.r.rs2;
        end
    end
    endtask

    function string Rv32i___mnemonic (input Rv32i _this);
        logic[31:0] op;
        logic[31:0] f3;
        logic[31:0] f7;
        op=_this._.r.opcode;
        f3=_this._.r.funct3;
        f7=_this._.r.funct7;
        case (op)
        'h33: begin
            if ((f3 == 'h0) && (f7 == 'h0)) begin
                return "add   ";
            end
            if ((f3 == 'h0) && (f7 == 'h20)) begin
                return "sub   ";
            end
            if ((f3 == 'h0) && (f7 == 'h1)) begin
                return "mul   ";
            end
            if ((f3 == 'h7) && (f7 == 'h1)) begin
                return "remu  ";
            end
            if (f3 == 'h7) begin
                return "and   ";
            end
            if (f3 == 'h6) begin
                return "or    ";
            end
            if (f3 == 'h4) begin
                return "xor   ";
            end
            if (f3 == 'h1) begin
                return "sll   ";
            end
            if ((f3 == 'h5) && (f7 == 'h0)) begin
                return "srl   ";
            end
            if ((f3 == 'h5) && (f7 == 'h20)) begin
                return "sra   ";
            end
            if ((f3 == 'h5) && (f7 == 'h1)) begin
                return "divu  ";
            end
            if (f3 == 'h2) begin
                return "slt   ";
            end
            if ((f3 == 'h3) && (f7 == 'h1)) begin
                return "mulhu ";
            end
            if (f3 == 'h3) begin
                return "sltu  ";
            end
            return "r-type";
        end
        'h13: begin
            if (f3 == 'h0) begin
                return "addi  ";
            end
            if (f3 == 'h7) begin
                return "andi  ";
            end
            if (f3 == 'h6) begin
                return "ori   ";
            end
            if (f3 == 'h4) begin
                return "xori  ";
            end
            if (f3 == 'h1) begin
                return "slli  ";
            end
            if ((f3 == 'h5) && (f7 == 'h0)) begin
                return "srli  ";
            end
            if ((f3 == 'h5) && (f7 == 'h20)) begin
                return "srai  ";
            end
            if (f3 == 'h2) begin
                return "slti  ";
            end
            if (f3 == 'h3) begin
                return "sltiu ";
            end
            return "aluimm";
        end
        'h3: begin
            return "load  ";
        end
        'h23: begin
            return "store ";
        end
        'h63: begin
            return "branch";
        end
        'h6F: begin
            return "jal   ";
        end
        'h67: begin
            return "jalr  ";
        end
        'h37: begin
            return "lui   ";
        end
        'h17: begin
            return "auipc ";
        end
        default: begin
        end
        endcase
        return "unknwn";
    endfunction

    function string Rv32ic___mnemonic (input Rv32ic _this);
        logic[7:0] op;
        logic[7:0] f3;
        logic[7:0] b12;
        logic[7:0] rs2;
        logic[7:0] bits6_5;
        logic[7:0] bits11_10;
        Rv32ic_rv16 i; i = {unsigned'(16'(_this._.raw))};
        if (((_this._.raw & 'h3)) == 'h3) begin
            return Rv32i___mnemonic(_this);
        end
        op=i.base.opcode;
        f3=i.base.funct3;
        b12=i.base.b12;
        bits6_5=i.base.bits6_5;
        bits11_10=i.base.bits11_10;
        rs2=i.big.rs2;
        case (op)
        'h0: begin
            case (f3)
            'h0: begin
                return "addi4s";
            end
            'h2: begin
                return "lw    ";
            end
            'h6: begin
                return "sw    ";
            end
            'h3: begin
                return "ld    ";
            end
            'h7: begin
                return "sd    ";
            end
            default: begin
            end
            endcase
        end
        'h1: begin
            case (f3)
            'h0: begin
                return "addi  ";
            end
            'h1: begin
                return "jal   ";
            end
            'h2: begin
                return "li    ";
            end
            'h3: begin
                return "addisp";
            end
            'h4: begin
                if (bits11_10 == 'h0) begin
                    return "srli  ";
                end
                if (bits11_10 == 'h1) begin
                    return "srai  ";
                end
                if (bits11_10 == 'h2) begin
                    return "andi  ";
                end
                if (((bits11_10 == 'h3) && (b12 == 'h0)) && (bits6_5 == 'h0)) begin
                    return "sub   ";
                end
                if (((bits11_10 == 'h3) && (b12 == 'h0)) && (bits6_5 == 'h1)) begin
                    return "xor   ";
                end
                if (((bits11_10 == 'h3) && (b12 == 'h0)) && (bits6_5 == 'h2)) begin
                    return "or    ";
                end
                if (((bits11_10 == 'h3) && (b12 == 'h0)) && (bits6_5 == 'h3)) begin
                    return "and   ";
                end
                return "illgl ";
            end
            'h5: begin
                return "j     ";
            end
            'h6: begin
                return "beqz  ";
            end
            'h7: begin
                return "bnez  ";
            end
            endcase
        end
        'h2: begin
            case (f3)
            'h0: begin
                return "slli  ";
            end
            'h1: begin
                return "fldsp ";
            end
            'h2: begin
                return "lwsp  ";
            end
            'h4: begin
                if ((rs2 != 'h0) && (b12 == 'h0)) begin
                    return "mv    ";
                end
                if ((rs2 != 'h0) && (b12 == 'h1)) begin
                    return "add   ";
                end
                if ((rs2 == 'h0) && (b12 == 'h0)) begin
                    return "jr    ";
                end
                if ((rs2 == 'h0) && (b12 == 'h1)) begin
                    return "jalr  ";
                end
                return "illgl ";
            end
            'h6: begin
                return "swsp  ";
            end
            'h3: begin
                return "ldsp  ";
            end
            'h7: begin
                return "sdsp  ";
            end
            default: begin
            end
            endcase
        end
        endcase
        return "unknwn";
    endfunction

    function string Rv32im___mnemonic (input Rv32im _this);
        if ((_this._.r.opcode == 'h33) && (_this._.r.funct7 == 'h1)) begin
            if (_this._.r.funct3 == 'h0) begin
                return "mul   ";
            end
            if (_this._.r.funct3 == 'h1) begin
                return "mulh  ";
            end
            if (_this._.r.funct3 == 'h2) begin
                return "mulhsu";
            end
            if (_this._.r.funct3 == 'h3) begin
                return "mulhu ";
            end
            if (_this._.r.funct3 == 'h4) begin
                return "div   ";
            end
            if (_this._.r.funct3 == 'h5) begin
                return "divu  ";
            end
            if (_this._.r.funct3 == 'h6) begin
                return "rem   ";
            end
            if (_this._.r.funct3 == 'h7) begin
                return "remu  ";
            end
        end
        return Rv32ic___mnemonic(_this);
    endfunction

    task debug ();
    begin: debug
        State tmp;
        Rv32im instr; instr = {imem_read_data_in};
        Rv32im___decode(instr, tmp);
        $write("(%d/%d)%x st[h%x b%x dc%x ic%x is%x ds%x ih%x]: [%s]%08x  rs%02d/%02d,imm:%08x,rd%02d => (%d)ops:%02d/%x/%x/%x rs%02d/%02d:%08x/%08x,imm:%08x,alu:%09x,rd%02d br(%d)%08x => mem(%d/%d@%08x)%08x/%01x (%d)wop(%x),r(%d)%08x@%02d", valid, stall_comb, pc, hazard_stall_comb, branch_stall_comb, dcache__busy_out, icache__busy_out, unsigned'(32'(icache__perf_out.state)), unsigned'(32'(dcache__perf_out.state)), icache__perf_out.hit, Rv32im___mnemonic(instr), (((instr._.raw & 'h3)) == 'h3) ? (instr._.raw) : ((instr._.raw | 'hFFFF0000)), signed'(32'(tmp.rs1)), signed'(32'(tmp.rs2)), tmp.imm, signed'(32'(tmp.rd)), state_reg['h0].valid, unsigned'(8'(state_reg['h0].alu_op)), unsigned'(8'(state_reg['h0].mem_op)), unsigned'(8'(state_reg['h0].br_op)), unsigned'(8'(state_reg['h0].wb_op)), signed'(32'(state_reg['h0].rs1)), signed'(32'(state_reg['h0].rs2)), state_reg['h0].rs1_val, state_reg['h0].rs2_val, state_reg['h0].imm, exe__alu_result_out, signed'(32'(state_reg['h0].rd)), exe__branch_taken_out, exe__branch_target_out, exe__mem_write_out, exe__mem_read_out, exe__mem_write_addr_out, exe__mem_write_data_out, exe__mem_write_mask_out, state_reg['h1].valid, unsigned'(8'(state_reg['h1].wb_op)), wb__regs_write_out, wb__regs_data_out, wb__regs_wr_id_out);
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (debugen_in) begin
            debug();
        end
        if ((dmem_addr_out == 'h11223344) && dmem_write_out) begin
            logic signed[31:0] out; out = $fopen("out.txt", "a");
            if (debugen_in) begin
                $write("OUTPUT pc=%x data=%08x char=%02x\n", pc, dmem_write_data_out, dmem_write_data_out & 'hFF);
            end
            $fwrite(out, "%c", dmem_write_data_out & 'hFF);
            $fclose(out);
        end
        if (memory_wait_comb) begin
            pc_tmp = pc;
            valid_tmp = valid;
            state_reg_tmp = state_reg;
            predicted_next_reg_tmp = predicted_next_reg;
            fallthrough_reg_tmp = fallthrough_reg;
            predicted_taken_reg_tmp = predicted_taken_reg;
            alu_result_reg_tmp = alu_result_reg;
            if (((state_reg['h1].valid && (state_reg['h1].wb_op == Wb_pkg::MEM)) && dcache__read_valid_out) && (dcache__read_addr_out == unsigned'(32'(alu_result_reg)))) begin
                load_data_reg_tmp = dcache__read_data_out;
                load_data_valid_reg_tmp = 1;
                if ((state_reg['h1].rd != 'h0) && state_reg['h0].valid) begin
                    if (state_reg['h0].rs1 == state_reg['h1].rd) begin
                        state_reg_tmp['h0].rs1_val=dcache__read_data_out;
                    end
                    if (state_reg['h0].rs2 == state_reg['h1].rd) begin
                        state_reg_tmp['h0].rs2_val=dcache__read_data_out;
                    end
                end
            end
            debug_branch_target_reg_tmp = debug_branch_target_reg;
            debug_branch_taken_reg_tmp = debug_branch_taken_reg;
        end
        else begin
            if (fetch_valid_comb && !stall_comb) begin
                pc_tmp = decode_fallthrough_comb;
            end
            if (decode_branch_valid_comb) begin
                pc_tmp = bp__predict_next_out;
            end
            if (branch_mispredict_comb) begin
                pc_tmp = branch_actual_next_comb;
            end
            valid_tmp = 1;
            if (hazard_stall_comb) begin
                state_reg_tmp['h0] = 0;
                state_reg_tmp['h0].valid=0;
                predicted_next_reg_tmp['h0] = pc;
                fallthrough_reg_tmp['h0] = pc;
                predicted_taken_reg_tmp['h0] = 0;
            end
            else begin
                state_reg_tmp['h0] = dec__state_out;
                state_reg_tmp['h0].valid=(dec__instr_valid_in && !branch_stall_comb) && !branch_flush_comb;
                predicted_next_reg_tmp['h0] = (decode_branch_valid_comb) ? (unsigned'(32'(bp__predict_next_out))) : (decode_fallthrough_comb);
                fallthrough_reg_tmp['h0] = decode_fallthrough_comb;
                predicted_taken_reg_tmp['h0] = decode_branch_valid_comb && bp__predict_taken_out;
                forward();
            end
            state_reg_tmp['h1] = state_reg['h0];
            predicted_next_reg_tmp['h1] = predicted_next_reg['h0];
            fallthrough_reg_tmp['h1] = fallthrough_reg['h0];
            predicted_taken_reg_tmp['h1] = predicted_taken_reg['h0];
            alu_result_reg_tmp = exe__alu_result_out;
            load_data_valid_reg_tmp = 0;
            debug_branch_target_reg_tmp = exe__branch_target_out;
            debug_branch_taken_reg_tmp = exe__branch_taken_out;
        end
        if (reset) begin
            state_reg_tmp['h0].valid='h0;
            state_reg_tmp['h1].valid='h0;
            pc_tmp = '0;
            valid_tmp = '0;
            load_data_reg_tmp = '0;
            load_data_valid_reg_tmp = '0;
            predicted_next_reg_tmp = '0;
            fallthrough_reg_tmp = '0;
            predicted_taken_reg_tmp = '0;
        end
    end
    endtask

    task _work_neg (input logic reset);
    begin: _work_neg
    end
    endtask

    generate  // _assign
        assign dec__pc_in = pc;
        assign dec__instr_valid_in = fetch_valid_comb;
        assign dec__instr_in = icache__read_data_out;
        assign dec__regs_data0_in = (dec__rs1_out == 'h0) ? ('h0) : (regs__read_data0_out);
        assign dec__regs_data1_in = (dec__rs2_out == 'h0) ? ('h0) : (regs__read_data1_out);
        assign exe__state_in = exe_state_comb;
        assign wb__state_in = state_reg['h1];
        assign wb__mem_data_in = (load_data_valid_reg) ? (unsigned'(32'(load_data_reg))) : ((((((state_reg['h1].valid && (state_reg['h1].wb_op == Wb_pkg::MEM)) && dcache__read_valid_out) && (dcache__read_addr_out == unsigned'(32'(alu_result_reg))))) ? (dcache__read_data_out) : (unsigned'(32'('h0)))));
        assign wb__alu_result_in = alu_result_reg;
        assign regs__read_addr0_in = unsigned'(8'(dec__rs1_out));
        assign regs__read_addr1_in = unsigned'(8'(dec__rs2_out));
        assign regs__write_in = (wb__regs_write_out && !memory_wait_comb) && ((((state_reg['h1].wb_op != Wb_pkg::MEM) || load_data_valid_reg) || ((dcache__read_valid_out && (dcache__read_addr_out == unsigned'(32'(alu_result_reg)))))));
        assign regs__write_addr_in = wb__regs_wr_id_out;
        assign regs__write_data_in = wb__regs_data_out;
        assign regs__debugen_in=debugen_in;
        assign dcache__read_in = exe__mem_read_out && !dcache__busy_out;
        assign dcache__write_in = exe__mem_write_out && !dcache__busy_out;
        assign dcache__addr_in = (exe__mem_read_out) ? (unsigned'(32'(exe__mem_read_addr_out))) : (unsigned'(32'(exe__mem_write_addr_out)));
        assign dcache__write_data_in = exe__mem_write_data_out;
        assign dcache__write_mask_in = exe__mem_write_mask_out;
        assign dcache__mem_read_data_in = dmem_read_data_in;
        assign dcache__stall_in = branch_stall_comb;
        assign dcache__flush_in = 0;
        assign dcache__debugen_in=debugen_in;
        assign bp__lookup_valid_in = decode_branch_valid_comb;
        assign bp__lookup_pc_in = unsigned'(32'(dec__state_out.pc));
        assign bp__lookup_target_in = decode_branch_target_comb;
        assign bp__lookup_fallthrough_in = decode_fallthrough_comb;
        assign bp__lookup_br_op_in = unsigned'(4'(dec__state_out.br_op));
        assign bp__update_valid_in = (state_reg['h0].valid && (state_reg['h0].br_op != Br_pkg::BNONE)) && !memory_wait_comb;
        assign bp__update_pc_in = unsigned'(32'(state_reg['h0].pc));
        assign bp__update_taken_in = exe__branch_taken_out;
        assign bp__update_target_in = exe__branch_target_out;
        assign icache__read_in = 1;
        assign icache__addr_in = fetch_addr_comb;
        assign icache__write_in = 0;
        assign icache__write_data_in = unsigned'(32'('h0));
        assign icache__write_mask_in = unsigned'(8'('h0));
        assign icache__mem_read_data_in = imem_read_data_in;
        assign icache__stall_in = memory_wait_comb || stall_comb;
        assign icache__flush_in = branch_mispredict_comb && !memory_wait_comb;
        assign icache__debugen_in=debugen_in;
        assign dmem_write_out = dcache__mem_write_out;
        assign dmem_write_data_out = dcache__mem_write_data_out;
        assign dmem_write_mask_out = dcache__mem_write_mask_out;
        assign dmem_read_out = dcache__mem_read_out;
        assign dmem_addr_out = dcache__mem_addr_out;
        assign imem_read_addr_out = icache__mem_addr_out;
    endgenerate

    always @(posedge clk) begin
        pc_tmp = pc;
        valid_tmp = valid;
        alu_result_reg_tmp = alu_result_reg;
        load_data_reg_tmp = load_data_reg;
        load_data_valid_reg_tmp = load_data_valid_reg;
        state_reg_tmp = state_reg;
        predicted_next_reg_tmp = predicted_next_reg;
        fallthrough_reg_tmp = fallthrough_reg;
        predicted_taken_reg_tmp = predicted_taken_reg;
        debug_alu_a_reg_tmp = debug_alu_a_reg;
        debug_alu_b_reg_tmp = debug_alu_b_reg;
        debug_branch_target_reg_tmp = debug_branch_target_reg;
        debug_branch_taken_reg_tmp = debug_branch_taken_reg;

        _work(reset);

        pc <= pc_tmp;
        valid <= valid_tmp;
        alu_result_reg <= alu_result_reg_tmp;
        load_data_reg <= load_data_reg_tmp;
        load_data_valid_reg <= load_data_valid_reg_tmp;
        state_reg <= state_reg_tmp;
        predicted_next_reg <= predicted_next_reg_tmp;
        fallthrough_reg <= fallthrough_reg_tmp;
        predicted_taken_reg <= predicted_taken_reg_tmp;
        debug_alu_a_reg <= debug_alu_a_reg_tmp;
        debug_alu_b_reg <= debug_alu_b_reg_tmp;
        debug_branch_target_reg <= debug_branch_target_reg_tmp;
        debug_branch_taken_reg <= debug_branch_taken_reg_tmp;
    end

    always @(negedge clk) begin
        _work_neg(reset);
    end

    assign perf_out = perf_comb;


endmodule

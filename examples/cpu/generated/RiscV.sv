`default_nettype none

import Predef_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_pkg::*;
import MemWBint_int_0_0_State_pkg::*;


module RiscV (
    input wire clk
,   input wire reset
,   output wire dmem_write_out
,   output logic[31:0] dmem_write_addr_out
,   output logic[31:0] dmem_write_data_out
,   output logic[7:0] dmem_write_mask_out
,   output wire dmem_read_out
,   output logic[31:0] dmem_read_addr_out
,   input logic[31:0] dmem_read_data_in
,   output logic[31:0] imem_read_addr_out
,   input logic[31:0] imem_read_data_in
,   input wire debugen_in
);

    reg[31:0] pc;
    reg valid;
    MakeBigStateDecodeFetchint_int_0_0_State[3-1:0] states_comb;

      logic[7:0] regs__write_addr_in;
      wire regs__write_in;
      logic[31:0] regs__write_data_in;
      logic[7:0] regs__read_addr0_in;
      logic[7:0] regs__read_addr1_in;
      wire regs__read_in;
      logic[31:0] regs__read_data0_out;
      logic[31:0] regs__read_data1_out;
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
      logic[31:0] members_tuple_0__pc_in;
      wire members_tuple_0__instr_valid_in;
      logic[31:0] members_tuple_0__instr_in;
      logic[31:0] members_tuple_0__regs_data0_in;
      logic[31:0] members_tuple_0__regs_data1_in;
      logic[7:0] members_tuple_0__rs1_out;
      logic[7:0] members_tuple_0__rs2_out;
      logic[31:0] members_tuple_0__alu_result_in;
      logic[31:0] members_tuple_0__mem_data_in;
      wire members_tuple_0__stall_out;
      MakeBigStateDecodeFetchint_int_0_0_State[3-1:0] members_tuple_0__state_in;
      DecodeFetchint_int_0_0_State[3-1:0] members_tuple_0__state_out;
    DecodeFetchDecodeFetchint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
        0
,       3
    ) members_tuple_0 (
        .clk(clk)
,       .reset(reset)
,       .pc_in(members_tuple_0__pc_in)
,       .instr_valid_in(members_tuple_0__instr_valid_in)
,       .instr_in(members_tuple_0__instr_in)
,       .regs_data0_in(members_tuple_0__regs_data0_in)
,       .regs_data1_in(members_tuple_0__regs_data1_in)
,       .rs1_out(members_tuple_0__rs1_out)
,       .rs2_out(members_tuple_0__rs2_out)
,       .alu_result_in(members_tuple_0__alu_result_in)
,       .mem_data_in(members_tuple_0__mem_data_in)
,       .stall_out(members_tuple_0__stall_out)
,       .state_in(members_tuple_0__state_in)
,       .state_out(members_tuple_0__state_out)
    );
      wire members_tuple_1__mem_write_out;
      logic[31:0] members_tuple_1__mem_write_addr_out;
      logic[31:0] members_tuple_1__mem_write_data_out;
      logic[7:0] members_tuple_1__mem_write_mask_out;
      wire members_tuple_1__mem_read_out;
      logic[31:0] members_tuple_1__mem_read_addr_out;
      logic[63:0] members_tuple_1__alu_result_out;
      wire members_tuple_1__branch_taken_out;
      logic[31:0] members_tuple_1__branch_target_out;
      MakeBigStateDecodeFetchint_int_0_0_State[3-1:0] members_tuple_1__state_in;
      ExecuteCalcint_int_0_0_State[2-1:0] members_tuple_1__state_out;
    ExecuteCalcExecuteCalcint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
        1
,       3
    ) members_tuple_1 (
        .clk(clk)
,       .reset(reset)
,       .mem_write_out(members_tuple_1__mem_write_out)
,       .mem_write_addr_out(members_tuple_1__mem_write_addr_out)
,       .mem_write_data_out(members_tuple_1__mem_write_data_out)
,       .mem_write_mask_out(members_tuple_1__mem_write_mask_out)
,       .mem_read_out(members_tuple_1__mem_read_out)
,       .mem_read_addr_out(members_tuple_1__mem_read_addr_out)
,       .alu_result_out(members_tuple_1__alu_result_out)
,       .branch_taken_out(members_tuple_1__branch_taken_out)
,       .branch_target_out(members_tuple_1__branch_target_out)
,       .state_in(members_tuple_1__state_in)
,       .state_out(members_tuple_1__state_out)
    );
      logic[31:0] members_tuple_2__mem_data_in;
      logic[31:0] members_tuple_2__regs_data_out;
      logic[7:0] members_tuple_2__regs_wr_id_out;
      wire members_tuple_2__regs_write_out;
      MakeBigStateDecodeFetchint_int_0_0_State[3-1:0] members_tuple_2__state_in;
      MemWBint_int_0_0_State[1-1:0] members_tuple_2__state_out;
    MemWBMemWBint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
        2
,       3
    ) members_tuple_2 (
        .clk(clk)
,       .reset(reset)
,       .mem_data_in(members_tuple_2__mem_data_in)
,       .regs_data_out(members_tuple_2__regs_data_out)
,       .regs_wr_id_out(members_tuple_2__regs_wr_id_out)
,       .regs_write_out(members_tuple_2__regs_write_out)
,       .state_in(members_tuple_2__state_in)
,       .state_out(members_tuple_2__state_out)
    );

    generate
        assign members_tuple_0__pc_in = pc;
        assign members_tuple_0__instr_valid_in = valid;
        assign members_tuple_0__instr_in = imem_read_data_in;
        assign members_tuple_0__regs_data0_in = members_tuple_0__rs1_out == 0 ? 0 : regs__read_data0_out;
        assign members_tuple_0__regs_data1_in = members_tuple_0__rs2_out == 0 ? 0 : regs__read_data1_out;
        assign members_tuple_0__alu_result_in = members_tuple_1__alu_result_out;
        assign members_tuple_0__mem_data_in = dmem_read_data_in;
        assign members_tuple_2__mem_data_in = dmem_read_data_in;
        assign regs__read_addr0_in = members_tuple_0__rs1_out;
        assign regs__read_addr1_in = members_tuple_0__rs2_out;
        assign regs__write_in = members_tuple_2__regs_write_out;
        assign regs__write_addr_in = members_tuple_2__regs_wr_id_out;
        assign regs__write_data_in = members_tuple_2__regs_data_out;
    endgenerate
    assign dmem_write_out = members_tuple_1__mem_write_out;
    assign dmem_write_addr_out = members_tuple_1__mem_write_addr_out;
    assign dmem_write_data_out = members_tuple_1__mem_write_data_out;
    assign dmem_write_mask_out = members_tuple_1__mem_write_mask_out;
    assign dmem_read_out = members_tuple_1__mem_read_out;
    assign dmem_read_addr_out = members_tuple_1__mem_read_addr_out;
    assign imem_read_addr_out = pc;

    task work (input logic reset);
    begin: work
        if (reset) begin
            pc = '0;
            valid = '0;
            disable work;
        end
        if (dmem_write_addr_out == 287454020 && dmem_write_out) begin
            integer out; out = $fopen("out.txt", "a");
            $fwrite("%c", dmem_write_data_out & 255);
            $fclose(out);
        end
        if (valid && !members_tuple_0__stall_out) begin
            pc = pc + ((members_tuple_0__instr_in & 3) == 3 ? 4 : 2);
        end
        if (states_comb[0].valid && members_tuple_1__branch_taken_out) begin
            pc = members_tuple_1__branch_target_out;
        end
        valid = 1;
    end
    endtask



    always @(posedge clk) begin
        work(reset);
    end

endmodule

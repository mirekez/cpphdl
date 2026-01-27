`default_nettype none

import Predef_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_pkg::*;
import MemWBint_int_0_0_State_pkg::*;


module MemWBMemWBint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
    parameter ID
,   parameter LENGTH
 )
 (
    input wire clk
,   input wire reset
,   input logic[31:0] mem_data_in
,   output logic[31:0] regs_data_out
,   output logic[31:0] regs_wr_id_out
,   output wire regs_write_out
,   input MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[LENGTH-1:0] state_in
,   output MemWBint_int_0_0_State[LENGTH - ID-1:0] state_out
);

    logic[31:0] regs_out_comb;
    logic regs_write_comb;
    MemWBint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg;


    generate
    endgenerate
    assign state_out = PipelineStage___state_reg;


    task PipelineStage____work (input logic reset);
    begin: PipelineStage____work
        logic[31:0] i;
        for (i = 1;i < LENGTH - ID;i=i+1) begin
            PipelineStage___state_reg[i] = PipelineStage___state_reg[i - 1];
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (reset) begin
        end
        PipelineStage____work(reset);
    end
    endtask

    always @(posedge clk) begin
        _work(reset);
    end

endmodule

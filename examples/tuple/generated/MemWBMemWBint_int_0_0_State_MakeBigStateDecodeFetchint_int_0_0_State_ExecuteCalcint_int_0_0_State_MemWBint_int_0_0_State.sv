`default_nettype none

import Predef_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import MemWBint_int_0_0_State_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_pkg::*;
import Wb_pkg::*;


module MemWBMemWBint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
    parameter ID = 2
,   parameter LENGTH = 3
 )
 (
    input wire clk
,   input wire reset
,   input wire[31:0] mem_data_in
,   output wire[31:0] regs_data_out
,   output wire[7:0] regs_wr_id_out
,   output wire regs_write_out
,   input wire MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[LENGTH-1:0] state_in
,   output wire MemWBint_int_0_0_State[LENGTH - ID-1:0] state_out
);


    // regs and combs
    logic[31:0] regs_out_comb;
;
    logic regs_write_comb;
;
    MemWBint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg;

    // members

    // tmp variables
    MemWBint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg_tmp;


    always_comb begin : regs_out_comb_func  // regs_out_comb_func
        regs_out_comb='h0;
        if (state_in[(ID - 'h1)].wb_op == Wb_pkg::PC2) begin
            regs_out_comb=state_in[(ID - 'h1)].pc + 'h2;
        end
        else begin
            if (state_in[(ID - 'h1)].wb_op == Wb_pkg::PC4) begin
                regs_out_comb=state_in[(ID - 'h1)].pc + 'h4;
            end
            else begin
                if (state_in[(ID - 'h1)].wb_op == Wb_pkg::ALU) begin
                    regs_out_comb=state_in[ID - 'h1].alu_result;
                end
                else begin
                    if (state_in[(ID - 'h1)].wb_op == Wb_pkg::MEM) begin
                        case (state_in[ID - 'h1].funct3)
                        'h0: begin
                            regs_out_comb=signed'(8'(mem_data_in));
                        end
                        'h1: begin
                            regs_out_comb=signed'(16'(mem_data_in));
                        end
                        'h2: begin
                            regs_out_comb=signed'(32'(mem_data_in));
                        end
                        'h4: begin
                            regs_out_comb=unsigned'(8'(mem_data_in));
                        end
                        'h5: begin
                            regs_out_comb=unsigned'(16'(mem_data_in));
                        end
                        endcase
                    end
                end
            end
        end
    end

    always_comb begin : regs_write_comb_func  // regs_write_comb_func
        regs_write_comb='h0;
        if (state_in[(ID - 'h1)].wb_op != Wb_pkg::WNONE) begin
            regs_write_comb=state_in[ID - 'h1].valid;
        end
    end

    task PipelineStage____work (input logic reset);
    begin: PipelineStage____work
        logic[63:0] i;
        for (i='h1;i < (LENGTH - ID);i=i+1) begin
            PipelineStage___state_reg_tmp[i] = PipelineStage___state_reg[i - 'h1];
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        PipelineStage____work(reset);
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        PipelineStage___state_reg_tmp = PipelineStage___state_reg;

        _work(reset);

        PipelineStage___state_reg <= PipelineStage___state_reg_tmp;
    end

    assign regs_data_out = regs_out_comb;

    assign regs_wr_id_out = state_in[ID - 'h1].rd;

    assign regs_write_out = regs_write_comb;

    assign state_out = PipelineStage___state_reg;


endmodule

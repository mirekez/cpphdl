`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Wb_pkg::*;


module Writeback (
    input wire clk
,   input wire reset
,   input State state_in
,   input logic[31:0] alu_result_in
,   input logic[31:0] mem_data_in
,   output logic[31:0] regs_data_out
,   output logic[7:0] regs_wr_id_out
,   output wire regs_write_out
);

    // regs and combs
    logic[31:0] regs_out_comb;
;
    logic regs_write_comb;
;

    // members

    // tmp variables


    always @(*) begin  // regs_out_comb_func
        regs_out_comb = 0;
        if (state_in.wb_op == Wb_pkg::PC2) begin
            regs_out_comb = state_in.pc + 2;
        end
        else begin
            if (state_in.wb_op == Wb_pkg::PC4) begin
                regs_out_comb = state_in.pc + 4;
            end
            else begin
                if (state_in.wb_op == Wb_pkg::ALU) begin
                    regs_out_comb = alu_result_in;
                end
                else begin
                    if (state_in.wb_op == Wb_pkg::MEM) begin
                        case (state_in.funct3)
                        0: begin
                            regs_out_comb = signed'(8'(mem_data_in));
                        end
                        1: begin
                            regs_out_comb = signed'(16'(mem_data_in));
                        end
                        2: begin
                            regs_out_comb = signed'(32'(mem_data_in));
                        end
                        4: begin
                            regs_out_comb = unsigned'(8'(mem_data_in));
                        end
                        5: begin
                            regs_out_comb = unsigned'(16'(mem_data_in));
                        end
                        endcase
                    end
                end
            end
        end
    end

    always @(*) begin  // regs_write_comb_func
        regs_write_comb = 0;
        if (state_in.wb_op != Wb_pkg::WNONE) begin
            regs_write_comb = state_in.valid;
        end
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _connect
    endgenerate

    always @(posedge clk) begin
        _work(reset);

    end

    assign regs_data_out = regs_out_comb;

    assign regs_wr_id_out = state_in.rd;

    assign regs_write_out = regs_write_comb;


endmodule

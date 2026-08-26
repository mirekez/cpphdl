`default_nettype none

import Predef_pkg::*;
import BF16E8_pkg::*;


module TemplateArrayDimension #(
    parameter DWIDTH_BYTES = 'h40
 )
 (
    input wire clk
,   input wire reset
,   input wire[16-1:0] seed_in
,   input wire[6-1:0] index_in
,   output wire[16-1:0] data_out
);
    localparam  VALUES_IN_WORD = DWIDTH_BYTES/($bits(BF16E8)/8);


    // regs and combs
    BF16E8[VALUES_IN_WORD-1:0] TemplateArrayDimensionBase___bf16_sum_a_comb;
    reg[VALUES_IN_WORD-1:0][DWIDTH_BYTES-1:0] TemplateArrayDimensionBase___buffered_reg;
    logic[16-1:0] TemplateArrayDimensionBase___data_comb;

    // members

    // tmp variables
    logic[VALUES_IN_WORD-1:0][DWIDTH_BYTES-1:0] TemplateArrayDimensionBase___buffered_reg_tmp;


    always_comb begin : TemplateArrayDimensionBase___data_comb_func  // TemplateArrayDimensionBase___data_comb_func
        logic[63:0] i;
        for (i='h0;i < VALUES_IN_WORD;i=i+1) begin
            TemplateArrayDimensionBase___bf16_sum_a_comb[i].raw=unsigned'(16'(seed_in)) + (i*'h7);
        end
        TemplateArrayDimensionBase___data_comb=TemplateArrayDimensionBase___bf16_sum_a_comb[unsigned'(8'(index_in)) % VALUES_IN_WORD].raw;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin
        TemplateArrayDimensionBase___buffered_reg_tmp = TemplateArrayDimensionBase___buffered_reg;

        _work(reset);

        TemplateArrayDimensionBase___buffered_reg <= TemplateArrayDimensionBase___buffered_reg_tmp;
    end

    assign data_out = TemplateArrayDimensionBase___data_comb;


endmodule

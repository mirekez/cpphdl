`default_nettype none

import Predef_pkg::*;


module TemplateArrayPortLeaf #(
    parameter SIZE = 'h4
 )
 (
    input wire clk
,   input wire reset
,   input wire[SIZE-1:0][8-1:0] mul_a_in
,   input wire[8-1:0] add_in[SIZE]
,   input wire[2-1:0] index_in
,   output wire[8-1:0] value_out
);


    // regs and combs
    logic[8-1:0] value_comb;

    // members

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        logic[7:0] index;
        index=unsigned'(8'(index_in));
        value_comb = mul_a_in[index] + add_in[index];
    end

    task _work (input logic unused);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule

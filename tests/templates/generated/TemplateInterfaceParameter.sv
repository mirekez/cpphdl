`default_nettype none

import Predef_pkg::*;


module TemplateInterfaceParameter (
    input wire clk
,   input wire reset
,   input wire[64-1:0] value_in
,   output wire[64-1:0] value_out
);


    // regs and combs

    // members
    wire[64-1:0] leaf__mem_out__data_in;
    wire[64-1:0] leaf__value_out;
    TemplateInterfaceLeaf #(
        64
    ) leaf (
        .clk(clk)
,       .reset(reset)
,       .mem_out__data_in(leaf__mem_out__data_in)
,       .value_out(leaf__value_out)
    );

    // tmp variables


    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
        assign leaf__mem_out__data_in = value_in;
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = leaf__value_out;


endmodule

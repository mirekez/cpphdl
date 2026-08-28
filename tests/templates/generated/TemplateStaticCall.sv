`default_nettype none

import Predef_pkg::*;
import Conv16_pkg::*;
import Native16_pkg::*;
import ArithmeticMiniConv16_pkg::*;


module TemplateStaticCall (
    input wire clk
,   input wire reset
,   input wire[16-1:0] value_in
,   output wire[16-1:0] value_out
);


    // regs and combs

    // members
    wire[16-1:0] encoder__value_in;
    wire[16-1:0] encoder__value_out;
    EncoderMiniConv16 #(
    ) encoder (
        .clk(clk)
,       .reset(reset)
,       .value_in(encoder__value_in)
,       .value_out(encoder__value_out)
    );

    // tmp variables


    generate  // _assign
        assign encoder__value_in = value_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = encoder__value_out;


endmodule

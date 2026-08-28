`default_nettype none

import Predef_pkg::*;
import TemplateMemberConv16_pkg::*;
import TemplateMemberArithmeticHelperTemplateMemberConv16_pkg::*;
import TemplateMemberNative16_pkg::*;


module TemplateMemberTop (
    input wire clk
,   input wire reset
,   input wire en_in
,   input wire[32-1:0] data_in
,   output wire[32-1:0] data_out
);


    // regs and combs

    // members
    wire decoder__en_in;
    wire[4*'h8-1:0] decoder__data_in;
    wire[4*'h8-1:0] decoder__data_out;
    TemplateMemberDecoderTemplateMemberConv16 #(
        4
    ) decoder (
        .clk(clk)
,       .reset(reset)
,       .en_in(decoder__en_in)
,       .data_in(decoder__data_in)
,       .data_out(decoder__data_out)
    );

    // tmp variables


    generate  // _assign
        assign decoder__en_in = en_in;
        assign decoder__data_in = data_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign data_out = decoder__data_out;


endmodule

`default_nettype none

import Predef_pkg::*;
import TemplateMemberConv16_pkg::*;
import TemplateMemberArithmeticHelperTemplateMemberConv16_pkg::*;
import TemplateMemberNative16_pkg::*;


module TemplateMemberDecoderTemplateMemberConv16 #(
    parameter DWIDTH_BYTES = 4
 )
 (
    input wire clk
,   input wire reset
,   input wire en_in
,   input wire[DWIDTH_BYTES*'h8-1:0] data_in
,   output wire[DWIDTH_BYTES*'h8-1:0] data_out
);
    localparam  VALUE_BITS = 'h10;
    localparam  VALUES_IN_WORD = DWIDTH_BYTES/($bits(TemplateMemberNative16)/8);


    // regs and combs
    logic[DWIDTH_BYTES*'h8-1:0] data_comb;

    // members
    wire[16-1:0] arithm__value_in;
    wire[16-1:0] arithm__value_out;
    TemplateMemberArithmeticTemplateMemberConv16 #(
    ) arithm (
        .clk(clk)
,       .reset(reset)
,       .value_in(arithm__value_in)
,       .value_out(arithm__value_out)
    );

    // tmp variables


    always_comb begin : data_comb_func  // data_comb_func
        logic[31:0] i;
        data_comb = data_in;
        if (en_in) begin
            data_comb['h0 +:VALUE_BITS - 'h1 - 'h0 + 1] = arithm__value_out;
            for (i='h1;i < VALUES_IN_WORD;i=i+1) begin
                data_comb[i*VALUE_BITS +:(0 + VALUE_BITS) - 'h1 - 0 + 1] = data_in[i*VALUE_BITS +:(0 + VALUE_BITS) - 'h1 - 0 + 1];
            end
        end
    end

    generate  // _assign
        assign arithm__value_in = data_in['h0 +:VALUE_BITS - 'h1 - 'h0 + 1];
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign data_out = data_comb;


endmodule

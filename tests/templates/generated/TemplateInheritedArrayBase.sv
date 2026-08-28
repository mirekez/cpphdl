`default_nettype none

import Predef_pkg::*;


module TemplateInheritedArrayBase #(
    parameter COUNT = 'h2
 )
 (
    input wire clk
,   input wire reset
,   output wire[8-1:0] value_out
);


    // regs and combs
    logic[8-1:0] value_comb;

    // members
    genvar __i;
    wire[8-1:0] leaf__value_in[COUNT];
    wire[8-1:0] leaf__value_out[COUNT];
    generate
    for (__i=0; __i < COUNT; __i = __i + 1) begin
        TemplateInheritedArrayLeaf          leaf (
            .clk(clk)
        ,           .reset(reset)
        ,           .value_in(leaf__value_in[__i])
        ,           .value_out(leaf__value_out[__i])
        );
    end
    endgenerate

    // tmp variables


    generate  // _assign
        genvar gi;
        for (gi='h0;gi < COUNT;gi=gi+1) begin
            assign leaf__value_in[gi] = gi;
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        for (i='h0;i < COUNT;i=i+1) begin
        end
    end
    endtask

    always_comb begin : value_comb_func  // value_comb_func
        value_comb=leaf__value_out['h0];
    end

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule

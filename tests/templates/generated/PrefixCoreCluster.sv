`default_nettype none

import Predef_pkg::*;


module PrefixCoreCluster #(
    parameter COUNT = 1
 )
 (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);


    // regs and combs

    // members
    genvar __i;
    wire[8-1:0] cores__value_in[COUNT];
    wire[8-1:0] cores__value_out[COUNT];
    generate
    for (__i=0; __i < COUNT; __i = __i + 1) begin
        PrefixCore          cores (
            .clk(clk)
        ,           .reset(reset)
        ,           .value_in(cores__value_in[__i])
        ,           .value_out(cores__value_out[__i])
        );
    end
    endgenerate

    // tmp variables


    generate  // _assign
        assign cores__value_in['h0] = value_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = cores__value_out['h0];


endmodule

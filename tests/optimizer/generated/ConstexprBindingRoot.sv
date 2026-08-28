`default_nettype none

import Predef_pkg::*;


module ConstexprBindingRoot (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[8-1:0] disabled_output;
    logic[8-1:0] enabled_output;
    logic[1-1:0] reset_seen;
    logic[8-1:0] source_comb;

    // members
    ConstexprBindingBus      bus (
        .clk(clk)
,       .reset(reset)
    );
    ConstexprBindingParent #(
        0
    ) disabled (
        .clk(clk)
,       .reset(reset)
    );
    ConstexprBindingParent #(
        1
    ) enabled (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    always_comb begin : source_comb_func  // source_comb_func
        source_comb = 'h5A;
    end

    generate  // _assign
        assign bus__value = source_comb;
        assign disabled__source = bus;
        assign enabled__source = bus;
        assign disabled_output = disabled___output;
        assign enabled_output = enabled___output;
    endgenerate

    task _work (input logic reset);
    begin: _work
        reset_seen = reset;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

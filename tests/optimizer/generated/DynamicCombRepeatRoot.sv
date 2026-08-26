`default_nettype none

import Predef_pkg::*;


module DynamicCombRepeatRoot (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[1-1:0] work_output;
    logic[3-1:0] first_repeat;
    logic[3-1:0] second_repeat;
    logic[3-1:0] third_repeat;
    logic[3-1:0] fourth_repeat;
    logic[3-1:0] fifth_repeat;
    logic[3-1:0] sixth_repeat;
    logic[3-1:0] seventh_repeat;
    logic[3-1:0] first_lazy;
    logic[3-1:0] second_lazy;
    logic[31:0] lazy_evaluations;
    logic[3-1:0] lazy_comb;
;
    logic[3-1:0] advancing_comb;

    // members
    DynamicCombRepeatLeaf      leaf (
        .clk(clk)
,       .reset(reset)
    );
    DynamicCombRepeatLeaf      lazy_leaf (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    always_comb begin : advancing_comb_func  // advancing_comb_func
        advancing_comb['h2] = advancing_comb['h1];
        advancing_comb['h1] = advancing_comb['h0];
        advancing_comb['h0] = 'h1;
    end

    always_comb begin : lazy_comb_func  // lazy_comb_func
        lazy_evaluations=lazy_evaluations+1;
        lazy_comb = 'h5;
    end

    generate  // _assign
        if (1) begin
            assign leaf___input = advancing_comb['h2];
            assign lazy_leaf___input = lazy_comb['h2];
        end
    endgenerate

    task _work (input logic unused);
    begin: _work
        work_output = leaf___output;
        first_repeat = advancing_comb;
        second_repeat = advancing_comb;
        third_repeat = advancing_comb;
        fourth_repeat = advancing_comb;
        fifth_repeat = advancing_comb;
        sixth_repeat = advancing_comb;
        seventh_repeat = advancing_comb;
        first_lazy = lazy_comb;
        second_lazy = lazy_comb;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

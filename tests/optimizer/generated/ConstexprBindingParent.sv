`default_nettype none

import Predef_pkg::*;


module ConstexprBindingParent #(
    parameter Enabled = 1
 )
 (
    input wire clk
,   input wire reset
);


    // regs and combs
    ConstexprBindingBus source;
    logic[8-1:0] _output;
    logic[8-1:0] invalid_comb;
    logic[8-1:0] output_comb;

    // members
    ConstexprBindingChild      child (
        .clk(clk)
,       .reset(reset)
    );
    ConstexprBindingChild      discarded_child (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    generate  // _assign
        if (Enabled) begin
        end
        if (Enabled) begin
        end
    endgenerate

    always_comb begin : invalid_comb_func  // invalid_comb_func
        invalid_comb = sv_bits8;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

`default_nettype none

import Predef_pkg::*;


module OpaqueSubtreeRoot (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[1-1:0] _input;
    logic[1-1:0] observed;
    logic[1-1:0] forwarded_comb;

    // members
    OpaqueSubtreeChild      child (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    generate  // _assign
        assign child___input = _input;
    endgenerate

    always_comb begin : forwarded_comb_func  // forwarded_comb_func
        forwarded_comb = child___output;
    end

    task _work (input logic reset);
    begin: _work
        observed = forwarded_comb;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

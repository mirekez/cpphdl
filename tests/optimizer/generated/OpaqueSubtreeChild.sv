`default_nettype none

import Predef_pkg::*;


module OpaqueSubtreeChild (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[1-1:0] _input;
    logic[1-1:0] _output;
    reg[1-1:0] state;
    logic[31:0] assign_calls;
    logic[1-1:0] output_comb;

    // members

    // tmp variables


    always_comb begin : output_comb_func  // output_comb_func
        output_comb = state;
    end

    generate  // _assign
        assign_calls=assign_calls+1;
    endgenerate

    task _work (input logic unused);
    begin: _work
        state <= _input;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

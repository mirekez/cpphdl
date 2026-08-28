`default_nettype none

import Predef_pkg::*;


module LazyCycleChild (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[1-1:0] _input;
    logic[1-1:0] _output;
    logic[31:0] evaluations;
    logic[1-1:0] output_comb;

    // members

    // tmp variables


    always_comb begin : output_comb_func  // output_comb_func
        evaluations=evaluations+1;
        output_comb = _input;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

`default_nettype none

import Predef_pkg::*;


module AliasForward (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[8-1:0] _input;
    logic[8-1:0] _output;

    // members

    // tmp variables


    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

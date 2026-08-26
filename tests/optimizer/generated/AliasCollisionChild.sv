`default_nettype none

import Predef_pkg::*;


module AliasCollisionChild (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[8-1:0] collision;

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

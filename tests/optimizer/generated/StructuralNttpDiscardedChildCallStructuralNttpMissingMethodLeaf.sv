`default_nettype none

import Predef_pkg::*;


module StructuralNttpDiscardedChildCallStructuralNttpMissingMethodLeaf #(
    parameter Enabled = 0
 )
 (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[1-1:0] _output;
    logic[1-1:0] output_comb;

    // members
    StructuralNttpMissingMethodLeaf      child (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

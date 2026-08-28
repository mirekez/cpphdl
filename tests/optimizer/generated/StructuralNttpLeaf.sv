`default_nettype none

import Predef_pkg::*;


module StructuralNttpLeaf (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[Config.width-1:0] _input;
    logic[Config.width-1:0] _output;
    logic[Config.width-1:0] mirrored_output;
    logic[Config.width-1:0] output_comb;
    logic[Config.width-1:0] mirrored_output_comb;

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

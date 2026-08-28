`default_nettype none

import Predef_pkg::*;


module ModuleArrayCallPortSink (
    input wire clk
,   input wire reset
);
    localparam  constant_index = 'h1;


    // regs and combs
    ModuleArrayCallLeaf[2-1:0] sources;
    logic[1-1:0] _output;

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

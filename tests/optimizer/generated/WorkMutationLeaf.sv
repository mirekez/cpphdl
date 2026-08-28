`default_nettype none

import Predef_pkg::*;


module WorkMutationLeaf (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[8-1:0] _input;
    logic[8-1:0] observed;

    // members

    // tmp variables


    task _work (input logic unused);
    begin: _work
        observed = _input;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

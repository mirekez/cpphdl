`default_nettype none

import Predef_pkg::*;


module BlockingOptimize (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire[32-1:0] data_in
,   output wire[32-1:0] value_out
);


    // regs and combs
    reg[32-1:0] once_accessed_reg;
    logic[32-1:0] value_comb;

    // members
    genvar gi, gj, gk;

    // tmp variables


    always @(*) begin  // value_comb_func
        value_comb = once_accessed_reg;
    end

    task update_once_accessed (input logic reset);
    begin: update_once_accessed
        once_accessed_reg <= (reset) ? (unsigned'(32'('h0))) : (unsigned'(32'(data_in + ((enable_in) ? ('h1234) : ('h10)))));
    end
    endtask

    task _work (input logic reset);
    begin: _work
        update_once_accessed(reset);
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule

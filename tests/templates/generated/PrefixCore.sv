`default_nettype none

import Predef_pkg::*;


module PrefixCore (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);


    // regs and combs
    logic[8-1:0] value_comb;

    // members

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = value_in;
    end

    generate  // _assign
    endgenerate

    task _work (input logic unused);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule

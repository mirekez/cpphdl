`default_nettype none

import Predef_pkg::*;


module MemberArrayLeaf (
    input wire clk
,   input wire reset
,   input wire[16-1:0] base_in
,   input wire[16-1:0] add_in
,   output wire[16-1:0] value_out
);


    // regs and combs
    logic[16-1:0] value_comb;

    // members
    genvar gi, gj, gk;

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = base_in + add_in;
        disable value_comb_func;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule

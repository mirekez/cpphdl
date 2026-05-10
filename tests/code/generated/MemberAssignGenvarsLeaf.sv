`default_nettype none

import Predef_pkg::*;


module MemberAssignGenvarsLeaf (
    input wire clk
,   input wire reset
,   input wire[32-1:0] value_in
,   output wire[32-1:0] value_out
);


    // regs and combs
    logic[32-1:0] value_comb;

    // members

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = (value_in*unsigned'(32'('h7))) + unsigned'(32'('h3));
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

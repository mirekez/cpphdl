`default_nettype none

import Predef_pkg::*;


module WorkMutationRoot (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[8-1:0] value;
    logic[8-1:0] value_comb;

    // members
    WorkMutationLeaf      leaf (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = value;
    end

    generate  // _assign
        assign leaf___input = value_comb;
    endgenerate

    task _work (input logic reset);
    begin: _work
        update_during_work(value);
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

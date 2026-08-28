`default_nettype none

import Predef_pkg::*;
import AnnotateReplacementNestedLocal_pkg::*;
import AnnotateReplacementParentLocal_pkg::*;


module AnnotateReplacementParent (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);


    // regs and combs
    AnnotateReplacementParentLocal _local;
    logic[8-1:0] value_comb;

    // members
    wire[8-1:0] nested__value_in;
    wire[8-1:0] nested__value_out;
    AnnotateReplacementNested      nested (
        .clk(clk)
,       .reset(reset)
,       .value_in(nested__value_in)
,       .value_out(nested__value_out)
    );

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = value_in ^ unsigned'(8'(unsigned'(8'hA5)));
    end

    generate  // _assign
        assign nested__value_in = value_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
        _local.value=value_in;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;


endmodule

`default_nettype none

import Predef_pkg::*;
import AnnotateReplacementNestedLocal_pkg::*;


module AnnotateReplacementScriptNested (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);


    // regs and combs
    AnnotateReplacementNestedLocal _local;
    logic[8-1:0] value_comb;

    // members
    wire[8-1:0] child__value_in;
    wire[8-1:0] child__value_out;
    AnnotateReplacementScript      child (
        .clk(clk)
,       .reset(reset)
,       .value_in(child__value_in)
,       .value_out(child__value_out)
    );

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = value_in ^ unsigned'(8'(unsigned'(8'h3C)));
    end

    generate  // _assign
        assign child__value_in = value_in;
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

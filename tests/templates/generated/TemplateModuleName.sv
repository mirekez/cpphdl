`default_nettype none

import Predef_pkg::*;


module TemplateModuleName (
    input wire clk
,   input wire reset
,   input wire[16-1:0] value_in
,   output wire[16-1:0] value_out
);


    // regs and combs

    // members
    wire[16-1:0] arithmetic__value_in;
    wire[16-1:0] arithmetic__value_out;
    TemplateModuleName_Arithmetic #(
        0
,       0
    ) arithmetic (
        .clk(clk)
,       .reset(reset)
,       .value_in(arithmetic__value_in)
,       .value_out(arithmetic__value_out)
    );

    // tmp variables


    generate  // _assign
        assign arithmetic__value_in = value_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = arithmetic__value_out;


endmodule

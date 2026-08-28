`default_nettype none

import Predef_pkg::*;


module TemplateDefaultParameter (
    input wire clk
,   input wire reset
,   input wire[16-1:0] value_in
,   output wire[16-1:0] default_out
,   output wire[16-1:0] override_out
);


    // regs and combs

    // members
    wire[16-1:0] default_leaf__value_in;
    wire[16-1:0] default_leaf__value_out;
    TemplateDefaultLeaf #(
        12
,       5
    ) default_leaf (
        .clk(clk)
,       .reset(reset)
,       .value_in(default_leaf__value_in)
,       .value_out(default_leaf__value_out)
    );
    wire[16-1:0] override_leaf__value_in;
    wire[16-1:0] override_leaf__value_out;
    TemplateDefaultLeaf #(
        9
,       4
    ) override_leaf (
        .clk(clk)
,       .reset(reset)
,       .value_in(override_leaf__value_in)
,       .value_out(override_leaf__value_out)
    );

    // tmp variables


    generate  // _assign
        assign default_leaf__value_in = value_in;
        assign override_leaf__value_in = value_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign default_out = default_leaf__value_out;

    assign override_out = override_leaf__value_out;


endmodule

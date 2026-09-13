`default_nettype none

import Predef_pkg::*;
import HelperInstantiationType_pkg::*;
import TemplateInstantiationHelperHelperInstantiationType_pkg::*;


module TemplateHelperInstantiation (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);


    // regs and combs

    // members
    wire[8-1:0] leaf__value_in;
    wire[8-1:0] leaf__value_out;
    TemplateHelperInstantiationLeafHelperInstantiationType #(
    ) leaf (
        .clk(clk)
,       .reset(reset)
,       .value_in(leaf__value_in)
,       .value_out(leaf__value_out)
    );

    // tmp variables


    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
        assign leaf__value_in = value_in;
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = leaf__value_out;


endmodule

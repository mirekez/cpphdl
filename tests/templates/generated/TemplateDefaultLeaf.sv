`default_nettype none

import Predef_pkg::*;


module TemplateDefaultLeaf #(
    parameter WIDTH_ = 'hC
,   parameter ADD_ = 'h5
 )
 (
    input wire clk
,   input wire reset
,   input wire[16-1:0] value_in
,   output wire[16-1:0] value_out
);
    localparam  WIDTH = WIDTH_;
    localparam  ADD = ADD_;


    // regs and combs
    logic[16-1:0] value_comb;

    // members

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = (unsigned'(16'(value_in)) + WIDTH) + ADD;
    end

    task _work (input logic unused);
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

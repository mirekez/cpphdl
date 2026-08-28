`default_nettype none

import Predef_pkg::*;


module TemplateBaseAliasPackage #(
    parameter WIDTH_ = 9
 )
 (
    input wire clk
,   input wire reset
,   input wire[MIRROR_WIDTH-1:0] value_in
,   output wire[OUTPUT_WIDTH-1:0] value_out
);
    localparam  LOCAL_WIDTH = MIRROR_WIDTH;
    localparam  LOCAL_OUTPUT_WIDTH = OUTPUT_WIDTH;
    localparam  PRODUCTION_TOTAL = TOTAL;
    localparam  WIDTH = WIDTH_;
    localparam  MIRROR_WIDTH = 64'h9;
    localparam  OUTPUT_WIDTH = 64'hD;
    localparam  EXTRA = 64'h3;
    localparam  TOTAL = 64'hC;


    // regs and combs
    logic[OUTPUT_WIDTH-1:0] value_comb;

    // members

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = (unsigned'(16'(value_in)) + LOCAL_WIDTH) + EXTRA;
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

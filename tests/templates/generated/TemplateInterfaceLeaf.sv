`default_nettype none

import Predef_pkg::*;


module TemplateInterfaceLeaf #(
    parameter WIDTH = 'h20
 )
 (
    input wire clk
,   input wire reset
,   input wire[WIDTH-1:0] mem_out__data_in
,   output wire[WIDTH-1:0] value_out
);


    // regs and combs
    logic[WIDTH-1:0] value_comb;

    // members

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = mem_out__data_in;
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

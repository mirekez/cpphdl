`default_nettype none

import Predef_pkg::*;


module TemplateSymbolicWidthCast #(
    parameter WIDTH = 'h3
 )
 (
    input wire clk
,   input wire reset
,   input wire zero_in
,   input wire[WIDTH-1:0] data_in
,   output wire[WIDTH-1:0] data_out
);


    // regs and combs
    logic[WIDTH-1:0] data_comb;

    // members

    // tmp variables


    always_comb begin : data_comb_func  // data_comb_func
        data_comb = (zero_in) ? (unsigned'(WIDTH'(unsigned'(WIDTH'('h0))))) : (unsigned'(WIDTH'(unsigned'(WIDTH'(data_in + 'h1)))));
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

    assign data_out = data_comb;


endmodule

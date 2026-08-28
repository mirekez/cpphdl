`default_nettype none

import Predef_pkg::*;


module TemplateInheritedArrayLeaf (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);


    // regs and combs

    // members

    // tmp variables


    task _work (input logic unused);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = unsigned'(8'(value_in));


endmodule

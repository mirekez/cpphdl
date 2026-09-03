`default_nettype none

import Predef_pkg::*;
import TemplateModuleSpecWide_pkg::*;


module TemplateModuleLeafTemplateModuleSpecWide_rx #(
    parameter SCALE = 3
 )
 (
    input wire clk
,   input wire reset
,   input wire[16-1:0] data_in
,   output wire[16-1:0] data_out
);
    localparam  TYPE_MASK = 'h5A;


    // regs and combs
    logic[16-1:0] data_comb;

    // members

    // tmp variables


    always_comb begin : data_comb_func  // data_comb_func
        data_comb = unsigned'(16'(((unsigned'(16'(data_in)) + SCALE) + TYPE_MASK)));
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

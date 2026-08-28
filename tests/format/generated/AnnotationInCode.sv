`default_nettype none

import Predef_pkg::*;


module AnnotationInCode #(
    parameter WIDTH = 'h8
 )
 (
    input wire clk
,   input wire reset
,   input wire[WIDTH-1:0] value_in
,   output wire[WIDTH-1:0] value_out
);


    // regs and combs
    (* preserve, dont_merge *)
    (* altera_attribute = "-name PRESERVE_REGISTER ON; -name DONT_MERGE_REGISTER ON" *)
    logic[WIDTH-1:0] q_out_reg;
    reg[WIDTH-1:0] unannotated_reg;

    // members

    // tmp variables
    logic[WIDTH-1:0] q_out_reg_tmp;
    logic[WIDTH-1:0] unannotated_reg_tmp;


    task _work (input logic reset);
    begin: _work
        q_out_reg_tmp = value_in;
        unannotated_reg_tmp = value_in;
        if (reset) begin
            q_out_reg_tmp = 'h0;
            unannotated_reg_tmp = 'h0;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        q_out_reg_tmp = q_out_reg;
        unannotated_reg_tmp = unannotated_reg;

        _work(reset);

        q_out_reg <= q_out_reg_tmp;
        unannotated_reg <= unannotated_reg_tmp;
    end

    assign value_out = q_out_reg;


endmodule

`default_nettype none

import Predef_pkg::*;


module TemplateArrayPortMember (
    input wire clk
,   input wire reset
,   input wire[4-1:0][8-1:0] mul_a_in
,   input wire[8-1:0] add_in[4]
,   input wire[2-1:0] index_in
,   output wire[8-1:0] value_out
);


    // regs and combs

    // members
    wire[4-1:0][8-1:0] arithm__mul_a_in;
    wire[8-1:0] arithm__add_in[4];
    wire[2-1:0] arithm__index_in;
    wire[8-1:0] arithm__value_out;
    TemplateArrayPortLeaf #(
        4
    ) arithm (
        .clk(clk)
,       .reset(reset)
,       .mul_a_in(arithm__mul_a_in)
,       .add_in(arithm__add_in)
,       .index_in(arithm__index_in)
,       .value_out(arithm__value_out)
    );

    // tmp variables


    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
        genvar gi;
        assign arithm__mul_a_in = mul_a_in;
        for (gi='h0;gi < 'h4;gi=gi+1) begin
            assign arithm__add_in[gi] = add_in[gi];
        end
        assign arithm__index_in = index_in;
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = unsigned'(8'(arithm__value_out));


endmodule

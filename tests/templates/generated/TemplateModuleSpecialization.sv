`default_nettype none

import Predef_pkg::*;
import TemplateModuleSpecWide_pkg::*;
import TemplateModuleSpecNarrow_pkg::*;


module TemplateModuleSpecialization (
    input wire clk
,   input wire reset
,   input wire[16-1:0] data_in
,   output wire[16-1:0] wide_small_out
,   output wire[16-1:0] wide_large_out
,   output wire[16-1:0] narrow_text_out
);


    // regs and combs

    // members
    wire[16-1:0] wide_small__data_in;
    wire[16-1:0] wide_small__data_out;
    TemplateModuleLeafTemplateModuleSpecWide_rx #(
        3
    ) wide_small (
        .clk(clk)
,       .reset(reset)
,       .data_in(wide_small__data_in)
,       .data_out(wide_small__data_out)
    );
    wire[16-1:0] wide_large__data_in;
    wire[16-1:0] wide_large__data_out;
    TemplateModuleLeafTemplateModuleSpecWide_rx #(
        7
    ) wide_large (
        .clk(clk)
,       .reset(reset)
,       .data_in(wide_large__data_in)
,       .data_out(wide_large__data_out)
    );
    wire[16-1:0] narrow_text__data_in;
    wire[16-1:0] narrow_text__data_out;
    TemplateModuleLeafTemplateModuleSpecNarrow_tx #(
        5
    ) narrow_text (
        .clk(clk)
,       .reset(reset)
,       .data_in(narrow_text__data_in)
,       .data_out(narrow_text__data_out)
    );

    // tmp variables


    generate  // _assign
        assign wide_small__data_in = data_in;
        assign wide_large__data_in = data_in;
        assign narrow_text__data_in = data_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign wide_small_out = wide_small__data_out;

    assign wide_large_out = wide_large__data_out;

    assign narrow_text_out = narrow_text__data_out;


endmodule

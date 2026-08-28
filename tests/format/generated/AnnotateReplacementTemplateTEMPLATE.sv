`default_nettype none

module AnnotateReplacementTemplateTEMPLATE #(
    parameter int __paramNumber1 = 109
) (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);
    // CPPHDL_ANNOTATE_REPLACEMENT_TEMPLATE_MARKER_109_TEMPLATE_$
    assign value_out = value_in ^ 8'd109;
endmodule

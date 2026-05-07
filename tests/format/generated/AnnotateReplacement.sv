`default_nettype none

module AnnotateReplacement (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);
    // CPPHDL_ANNOTATE_REPLACEMENT_MARKER
    assign value_out = value_in ^ 8'hA5;
endmodule

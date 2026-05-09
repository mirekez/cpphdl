#!/usr/bin/env bash
cat <<'SV'
`default_nettype none

module AnnotateReplacementScript (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);
    // CPPHDL_ANNOTATE_REPLACEMENT_SCRIPT_MARKER
    assign value_out = value_in ^ 8'h3C;
endmodule
SV

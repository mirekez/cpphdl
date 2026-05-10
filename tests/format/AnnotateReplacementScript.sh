#!/usr/bin/env bash
mask="${1:-3C}"
cat <<'SV'
`default_nettype none

module AnnotateReplacementScript (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);
SV
printf '    // CPPHDL_ANNOTATE_REPLACEMENT_SCRIPT_MARKER_%s\n' "$mask"
printf "    assign value_out = value_in ^ 8'h%s;\n" "$mask"
cat <<'SV'
endmodule
SV

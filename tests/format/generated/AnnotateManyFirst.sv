`default_nettype none

module AnnotateManyFirstHelper (
    input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);
    assign value_out = value_in ^ 8'h11;
endmodule

module AnnotateManyFirst (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);
    AnnotateManyFirstHelper helper(.value_in(value_in), .value_out(value_out));
endmodule

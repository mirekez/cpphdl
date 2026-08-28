`default_nettype none

module AnnotateManySecondHelper (
    input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);
    assign value_out = value_in ^ 8'h22;
endmodule

module AnnotateManySecond (
    input wire clk
,   input wire reset
,   input wire[8-1:0] value_in
,   output wire[8-1:0] value_out
);
    AnnotateManySecondHelper helper(.value_in(value_in), .value_out(value_out));
endmodule

module WideSelect #(
    parameter int unsigned Width = 65
) (
    input logic [Width-1:0] value_i,
    input logic [6:0] index_i,
    input logic [63:0] left_i,
    input logic [63:0] right_i,
    output logic bit_o,
    output logic carry_o,
    output logic [64:0] sum_o,
    output logic [64:0] contextual_o
);
    logic [64:0] sum;
    assign sum = {1'b0, left_i} + {1'b0, right_i};
    assign bit_o = value_i[index_i];
    assign carry_o = sum[64];
    assign sum_o = sum;
    assign contextual_o = left_i + right_i;
endmodule

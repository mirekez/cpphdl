package SymbolicArithmeticPackage;
    parameter int unsigned BaseWidth = 7;
endpackage

module SymbolicArithmetic #(
    parameter int unsigned Width = 65,
    parameter int unsigned OperandWidth = 32,
    parameter int unsigned ProductWidth = Width > 64 ? 64 : Width,
    parameter int unsigned ConstantWidth = SymbolicArithmeticPackage::BaseWidth * 2 + 1
) (
    input logic [63:0] left_i,
    input logic [63:0] right_i,
    output logic [Width-1:0] sum_o,
    output logic [Width-1:0] concat_o,
    output logic [ProductWidth-1:0] product_o,
    output logic [63:0] signed_product_o,
    output logic [ConstantWidth-1:0] constant_o
);
    assign sum_o = left_i + right_i;
    assign concat_o = left_i + (right_i[0] ? {{Width-2{1'b0}}, 2'h2} : {{Width-3{1'b0}}, 3'h4});
    assign product_o = left_i * right_i;
    assign signed_product_o = $signed({left_i[OperandWidth-1] & left_i[63], left_i[OperandWidth-1:0]}) *
                              $signed({right_i[OperandWidth-1] & right_i[63], right_i[OperandWidth-1:0]});
    assign constant_o = ConstantWidth;
endmodule

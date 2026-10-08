module Xperm #(
    parameter int XLEN = 32,
    parameter logic [63:0] Parameter = 64'h1122334455667788
) (
    input logic [XLEN-1:0] operand_a,
    input logic [XLEN-1:0] operand_b,
    input logic choose,
    input logic signed [7:0] signed_a,
    input logic signed [7:0] signed_b,
    output logic [XLEN-1:0] result_o,
    output logic [XLEN-1:0] reverse_o,
    output logic [XLEN-1:0] nested_o,
    output logic [2*XLEN-1:0] wide_o,
    output logic [31:0] whole_o,
    output logic signed [15:0] signed_o,
    output logic [XLEN-1:0] constant_o
);
    localparam logic [63:0] Local = 64'hfedcba9876543210;
    localparam logic [63:0] Table [0:1] = '{64'h0123456789abcdef, 64'ha55a123456789abc};
    assign constant_o = Parameter[XLEN-1:0] ^ Local[XLEN-1:0] ^ Table[choose][XLEN-1:0];
    for (genvar index = 0; index < XLEN / 8; index++) begin
        // Original reproducer: the two eight-bit branches used to become
        // uint64_t and logic<8>, making the C++ conditional ambiguous.
        assign result_o[index << 3 +: 8] =
            (operand_b[index << 3 +: 8] < XLEN / 8)
            ? operand_a[operand_b[index << 3 +: 8] << 3 +: 8] : 8'b0;
        assign reverse_o[index*8 +: 8] =
            (operand_b[index*8 +: 8] >= XLEN / 8)
            ? 8'ha5 : operand_a[operand_b[index*8 +: 8]*8 +: 8];
        assign nested_o[index*8 +: 8] = choose
            ? ((operand_b[index*8 +: 8] < XLEN / 8)
                ? operand_a[operand_b[index*8 +: 8]*8 +: 8] : 8'h7e)
            : 8'hc3;
        assign wide_o[index*16 +: 16] =
            (operand_b[index*8 +: 8] < XLEN / 8)
            ? operand_a[operand_b[index*8 +: 8]*8 +: 8] : 16'hd00d;
    end
    always_comb whole_o = choose ? operand_a[7:0] : 8'hff;
    // Explicit signed conversions exercise the supported signed-value path;
    // declaration-only signed port widening is a separate emitter issue.
    assign signed_o = choose ? $signed(signed_a) : $signed(signed_b);
endmodule

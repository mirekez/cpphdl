module ConstPackedRom (
    input logic [1:0] address_i,
    output logic [63:0] data_o
);
    localparam int Rows = 3;
    const logic [Rows-1:0][63:0] words = {
        64'hfedcba9876543210,
        64'h0123456789abcdef,
        64'h1050007303c58593
    };
    assign data_o = address_i < Rows ? words[address_i] : '0;
endmodule

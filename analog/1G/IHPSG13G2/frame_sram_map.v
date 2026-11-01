// Yosys memory_bram mapping: four instances implement a 4096-word FIFO.
// The read register is inside the PDK macro; no asynchronous-read substitution.
module \$__IHP_FRAME_1024X16 (
    input CLK1, CLK2,
    input [9:0] A1ADDR, B1ADDR,
    input [15:0] A1DATA,
    input A1EN, B1EN,
    output [15:0] B1DATA
);
    RM_IHPSG13_2P_1024x16_c2_bm_bist _TECHMAP_REPLACE_ (
        .A_CLK(CLK1), .A_MEN(A1EN), .A_WEN(A1EN), .A_REN(1'b0),
        .A_ADDR(A1ADDR), .A_DIN(A1DATA), .A_DLY(1'b1), .A_DOUT(), .A_BM(16'hffff),
        .A_BIST_CLK(1'b0), .A_BIST_EN(1'b0), .A_BIST_MEN(1'b0),
        .A_BIST_WEN(1'b0), .A_BIST_REN(1'b0), .A_BIST_ADDR(10'b0),
        .A_BIST_DIN(16'b0), .A_BIST_BM(16'b0),
        .B_CLK(CLK2), .B_MEN(B1EN), .B_WEN(1'b0), .B_REN(B1EN),
        .B_ADDR(B1ADDR), .B_DIN(16'b0), .B_DLY(1'b1), .B_DOUT(B1DATA), .B_BM(16'b0),
        .B_BIST_CLK(1'b0), .B_BIST_EN(1'b0), .B_BIST_MEN(1'b0),
        .B_BIST_WEN(1'b0), .B_BIST_REN(1'b0), .B_BIST_ADDR(10'b0),
        .B_BIST_DIN(16'b0), .B_BIST_BM(16'b0)
    );
endmodule

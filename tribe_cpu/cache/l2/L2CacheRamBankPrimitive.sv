`default_nettype none

// Canonical synchronous single-port BRAM leaf for one CppHDL L2 cache bank.
module L2CacheRamBank #(
    parameter integer WIDTH = 32,
    parameter integer DEPTH = 512
) (
    input  wire                         clk,
    input  wire                         l2_clock,
    input  wire                         reset,
    input  wire [$clog2(DEPTH)-1:0]     addr_in,
    input  wire                         wr_in,
    input  wire                         rd_in,
    input  wire [WIDTH-1:0]             data_in,
    output wire [WIDTH-1:0]             data_out
);
    (* ram_style = "block" *)
    reg [WIDTH-1:0] memory [0:DEPTH-1];
    reg [WIDTH-1:0] data_out_reg;

    always_ff @(posedge l2_clock) begin
        if (wr_in)
            memory[addr_in] <= data_in;
        if (rd_in)
            data_out_reg <= memory[addr_in];
    end

    assign data_out = data_out_reg;

    wire unused_clk = clk;
    wire unused_reset = reset;
endmodule

`default_nettype wire

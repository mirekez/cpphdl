`default_nettype none

// Physical storage leaf for Tribe's CppHDL RAM. Cache organization and all
// cache/CPU control remain generated from C++; this module only presents the
// synchronous single-address RAM pattern expected by FPGA synthesis tools.
module RAM #(
    parameter integer WIDTH = 33,
    parameter integer DEPTH = 16
) (
    input  wire                         clk,
    input  wire                         l2_clock,
    input  wire                         reset,
    input  wire [$clog2(DEPTH)-1:0]     addr_in,
    input  wire [WIDTH-1:0]             data_in,
    input  wire                         wr_in,
    input  wire                         rd_in,
    output wire [WIDTH-1:0]             q_out,
    input  wire signed [31:0]           id_in
);
    (* ram_style = "block" *)
    reg [WIDTH-1:0] memory [0:DEPTH-1];
    reg [WIDTH-1:0] read_data_reg;

    always_ff @(posedge clk) begin
        if (wr_in)
            memory[addr_in] <= data_in;
        if (reset)
            read_data_reg <= '0;
        else if (rd_in)
            read_data_reg <= memory[addr_in];
    end

    assign q_out = read_data_reg;

    wire unused_l2_clock = l2_clock;
    wire signed [31:0] unused_id_in = id_in;
endmodule

`default_nettype none

module L2CacheRamBank #(
    parameter integer WIDTH = 32,
    parameter integer DEPTH = 128
) (
    input  wire                         clk,
    input  wire                         l2_clock,
    input  wire                         reset,
    input  wire [$clog2(DEPTH)-1:0]     addr_in,
    input  wire                         write_in,
    input  wire                         read_in,
    input  wire [WIDTH-1:0]             write_data_in,
    output wire [WIDTH-1:0]             read_data_out
);
    (* ram_style = "block" *)
    reg [WIDTH-1:0] memory [0:DEPTH-1];
    reg [WIDTH-1:0] read_data_reg;

    always_ff @(posedge l2_clock) begin
        if (reset) begin
            read_data_reg <= '0;
        end else begin
            if (write_in)
                memory[addr_in] <= write_data_in;
            if (read_in)
                read_data_reg <= memory[addr_in];
        end
    end

    assign read_data_out = read_data_reg;
    wire unused_clk = clk;
endmodule

`default_nettype wire

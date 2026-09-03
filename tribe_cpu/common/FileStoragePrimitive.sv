`default_nettype none

// Physical storage leaf for the CppHDL File module.  The architectural
// register-file behavior (including write-first bypasses) remains in File.h.
// A validity bitmap avoids resetting the distributed RAM, which would force
// Vivado to implement the 32x32 array as flip-flops.
module FileStorage #(
    parameter integer MEM_WIDTH = 32,
    parameter integer MEM_DEPTH = 32
) (
    input  wire                         clk,
    input  wire                         l2_clock,
    input  wire                         reset,
    input  wire [7:0]                   write_addr_in,
    input  wire                         write_in,
    input  wire [MEM_WIDTH-1:0]         write_data_in,
    input  wire [7:0]                   write2_addr_in,
    input  wire                         write2_in,
    input  wire [MEM_WIDTH-1:0]         write2_data_in,
    input  wire [7:0]                   read_addr0_in,
    input  wire [7:0]                   read_addr1_in,
    input  wire [MEM_WIDTH-1:0]         reset_x10_in,
    input  wire [MEM_WIDTH-1:0]         reset_x11_in,
    output wire [MEM_WIDTH-1:0]         read_data0_out,
    output wire [MEM_WIDTH-1:0]         read_data1_out,
    output wire [MEM_WIDTH-1:0]         x1_out,
    output wire [MEM_WIDTH-1:0]         x10_out,
    output wire [MEM_WIDTH-1:0]         x11_out,
    output wire [MEM_WIDTH-1:0]         x16_out,
    output wire [MEM_WIDTH-1:0]         x17_out
);
    localparam integer ADDR_BITS = $clog2(MEM_DEPTH);

    (* ram_style = "distributed" *)
    reg [MEM_WIDTH-1:0] memory [0:MEM_DEPTH-1];
    reg [MEM_DEPTH-1:0] valid_reg;
    reg [MEM_WIDTH-1:0] x10_reg;
    reg [MEM_WIDTH-1:0] x11_reg;

    wire [ADDR_BITS-1:0] write_addr = write_addr_in[ADDR_BITS-1:0];
    wire [ADDR_BITS-1:0] read_addr0 = read_addr0_in[ADDR_BITS-1:0];
    wire [ADDR_BITS-1:0] read_addr1 = read_addr1_in[ADDR_BITS-1:0];

    function automatic [MEM_WIDTH-1:0] read_storage;
        input [ADDR_BITS-1:0] address;
        begin
            if (address == ADDR_BITS'(10))
                read_storage = x10_reg;
            else if (address == ADDR_BITS'(11))
                read_storage = x11_reg;
            else if (valid_reg[address])
                read_storage = memory[address];
            else
                read_storage = '0;
        end
    endfunction

    always_ff @(posedge clk) begin
        if (reset) begin
            valid_reg <= '0;
            x10_reg <= reset_x10_in;
            x11_reg <= reset_x11_in;
        end

        if (write_in) begin
            if (write_addr == ADDR_BITS'(10))
                x10_reg <= write_data_in;
            else if (write_addr == ADDR_BITS'(11))
                x11_reg <= write_data_in;
            else begin
                memory[write_addr] <= write_data_in;
                valid_reg[write_addr] <= 1'b1;
            end
        end

        // Tribe hardwires File's second port to x11 for SBI return a1.  Keep
        // it out of the array so the ordinary storage has exactly one write
        // port and can map to replicated distributed RAM.
        if (write2_in)
            x11_reg <= write2_data_in;
    end

    assign read_data0_out = read_storage(read_addr0);
    assign read_data1_out = read_storage(read_addr1);
    assign x1_out = read_storage(ADDR_BITS'(1));
    assign x10_out = x10_reg;
    assign x11_out = x11_reg;
    assign x16_out = read_storage(ADDR_BITS'(16));
    assign x17_out = read_storage(ADDR_BITS'(17));

    wire unused_l2_clock = l2_clock;
    wire [7:0] unused_write2_addr = write2_addr_in;
endmodule

`default_nettype wire

`default_nettype none

module RAM #(
    parameter WIDTH = 32,
    parameter DEPTH = 32
) (
    input  wire                     clk,
    input  wire                     l2_clock,
    input  wire                     reset,
    input  wire[$clog2(DEPTH)-1:0]  addr_in,
    input  wire[WIDTH-1:0]          data_in,
    input  wire                     wr_in,
    input  wire                     rd_in,
    output wire[WIDTH-1:0]          q_out,
    input  wire signed[31:0]        id_in
);
    // CppHDL's byte-lane memory representation generates a wide bank of
    // individual registers.  Keep wide L1 data arrays in BRAM, but map the
    // shallow, narrow tag arrays to distributed RAM.  Forcing a 16-entry tag
    // array into RAMB18 adds a BRAM clock-to-output delay to every hit and was
    // the startpoint of the routed tag-to-next-data-address critical path.
    generate
        if (WIDTH <= 64 && DEPTH <= 32) begin : gen_distributed_ram
            (* ram_style = "distributed" *) reg [WIDTH-1:0] ram [0:DEPTH-1];
            reg [WIDTH-1:0] q_out_reg;

            always_ff @(posedge clk) begin
                if (reset) begin
                    q_out_reg <= '0;
                end
                else begin
                    if (wr_in)
                        ram[addr_in] <= data_in;
                    if (rd_in)
                        q_out_reg <= ram[addr_in];
                end
            end

            assign q_out = q_out_reg;
        end
        else begin : gen_block_ram
            (* ram_style = "block" *) reg [WIDTH-1:0] ram [0:DEPTH-1];
            reg [WIDTH-1:0] q_out_reg;

            always_ff @(posedge clk) begin
                if (reset) begin
                    q_out_reg <= '0;
                end
                else begin
                    if (wr_in)
                        ram[addr_in] <= data_in;
                    if (rd_in)
                        q_out_reg <= ram[addr_in];
                end
            end

            assign q_out = q_out_reg;
        end
    endgenerate

    // l2_clock and id_in are part of the generated generic RAM interface but
    // intentionally unused: every current RAM instance belongs to an L1 cache.
endmodule

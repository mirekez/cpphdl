// Delay by DEPTH enabled edges. Reset invalidates the owner's valid pipeline;
// payload RAM is deliberately not reset. No read/write address collision.
`ifndef CPPHDL_ENABLED_DELAY
`define CPPHDL_ENABLED_DELAY
module CpphdlEnabledDelay #(parameter WIDTH=32, parameter DEPTH=1)(
    input wire clk, reset, advance,
    input wire [WIDTH-1:0] data_in,
    output wire [WIDTH-1:0] data_out
);
    generate if (DEPTH == 0) begin: direct
        assign data_out = data_in;
    end else if (DEPTH < 8) begin: short_delay
        reg [WIDTH-1:0] words[0:DEPTH-1];
        integer i;
        always @(posedge clk) begin
            if (!reset && advance) begin
                words[0] <= data_in;
                for (i=1; i<DEPTH; i=i+1) words[i] <= words[i-1];
            end
        end
        assign data_out = words[DEPTH-1];
    end else begin: ram_delay
        localparam ADDRESS_BITS = $clog2(DEPTH);
        reg [ADDRESS_BITS-1:0] position;
        wire [ADDRESS_BITS-1:0] next_position = position == DEPTH-1 ? 0 : position+1'b1;
        (* ramstyle="M20K", ram_style="block" *) reg [WIDTH-1:0] words[0:DEPTH-1];
        reg [WIDTH-1:0] result;
        always @(posedge clk) begin
            if (reset) position <= 0;
            else if (advance) position <= next_position;
        end
        always @(posedge clk) begin
            if (!reset && advance) begin
                words[position] <= data_in;
                result <= words[next_position];
            end
        end
        assign data_out = result;
    end endgenerate
endmodule
`endif

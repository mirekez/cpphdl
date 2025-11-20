`default_nettype none

import Predef_pkg::*;


module Memory #(
    parameter MEM_WIDTH_BYTES
,   parameter MEM_DEPTH
,   parameter SHOWAHEAD
 )
 (
    input wire clk
,   input wire reset
,   input wire[$clog2(MEM_DEPTH)-1:0] write_addr_in
,   input wire write_in
,   input wire[MEM_WIDTH_BYTES*8-1:0] data_in
,   input wire[$clog2(MEM_DEPTH)-1:0] read_addr_in
,   input wire read_in
,   output wire[MEM_WIDTH_BYTES*8-1:0] data_out
,   input wire debugen_in
);

    logic[MEM_WIDTH_BYTES*8-1:0] data_out_comb;
    reg[MEM_WIDTH_BYTES*8-1:0] data_out_reg;
    reg[MEM_WIDTH_BYTES-1:0][7:0] buffer[MEM_DEPTH];
    logic[63:0] i;


    generate
    endgenerate
    assign data_out = data_out_comb;

    always @(*) begin
        if (SHOWAHEAD) begin
            data_out_comb = buffer[read_addr_in];
        end
        else begin
            data_out_comb = data_out_reg;
        end
    end

    task work (input logic reset);
    begin: work
        if (write_in) begin
            buffer[write_addr_in] = data_in;
        end
        if (!SHOWAHEAD) begin
            data_out_reg = buffer[read_addr_in];
        end
        if (debugen_in) begin
            $write("%m: input: (%x)%x@%x, output: (%x)%x@%x\n", write_in, data_in, write_addr_in, read_in, data_out, read_addr_in);
        end
    end
    endtask



    always @(posedge clk) begin
        work(reset);
    end

endmodule

`default_nettype none

import Predef_pkg::*;


module Fifo #(
    parameter FIFO_WIDTH_BYTES
,   parameter FIFO_DEPTH
,   parameter SHOWAHEAD
 )
 (
    input wire clk
,   input wire reset
,   input wire write_in
,   input wire[FIFO_WIDTH_BYTES*8-1:0] data_in
,   input wire read_in
,   output wire[FIFO_WIDTH_BYTES*8-1:0] data_out
,   output wire empty_out
,   output wire full_out
,   input wire clear_in
,   output wire afull_out
,   input wire debugen_in
);

    logic full_comb;
    logic empty_comb;
    reg[$clog2(FIFO_DEPTH)-1:0] wp_reg;
    reg[$clog2(FIFO_DEPTH)-1:0] rp_reg;
    reg full_reg;
    reg afull_reg;

      wire[$clog2((FIFO_DEPTH))-1:0] mem__write_addr_in;
      wire mem__write_in;
      wire[(FIFO_WIDTH_BYTES)*8-1:0] mem__data_in;
      wire[$clog2((FIFO_DEPTH))-1:0] mem__read_addr_in;
      wire mem__read_in;
      wire[(FIFO_WIDTH_BYTES)*8-1:0] mem__data_out;
      wire mem__debugen_in;
    Memory #(
        FIFO_WIDTH_BYTES
,       FIFO_DEPTH
,       SHOWAHEAD
    ) mem (
        .clk(clk)
,       .reset(reset)
,       .write_addr_in(mem__write_addr_in)
,       .write_in(mem__write_in)
,       .data_in(mem__data_in)
,       .read_addr_in(mem__read_addr_in)
,       .read_in(mem__read_in)
,       .data_out(mem__data_out)
,       .debugen_in(mem__debugen_in)
    );

    generate
        assign mem__data_in = data_in;
        assign mem__write_in = write_in;
        assign mem__write_addr_in = wp_reg;
        assign mem__read_in = read_in;
        assign mem__read_addr_in = rp_reg;
        assign mem__debugen_in = debugen_in;
    endgenerate
    assign data_out = mem__data_out;
    assign empty_out = empty_comb;
    assign full_out = full_comb;
    assign afull_out = afull_reg;

    always @(*) begin
        full_comb = (wp_reg == rp_reg) && full_reg;
    end

    always @(*) begin
        empty_comb = (wp_reg == rp_reg) && !full_reg;
    end

    task work (input logic reset);
    begin: work
        if (reset) begin
            wp_reg = '0;
            rp_reg = '0;
            full_reg = '0;
            afull_reg = '0;
            disable work;
        end
        if (read_in) begin
            if (empty_comb) begin
                $write("%m: reading from an empty fifo\n");
                $finish();
            end
            if (!empty_comb) begin
                rp_reg = rp_reg + 1;
            end
            if (!write_in) begin
                full_reg = 0;
            end
        end
        if (write_in) begin
            if (full_comb) begin
                $write("%m: writing to a full fifo\n");
                $finish();
            end
            if (!full_comb) begin
                wp_reg = wp_reg + 1;
            end
            if (wp_reg == rp_reg) begin
                full_reg = 1;
            end
        end
        if (clear_in) begin
            wp_reg = 0;
            rp_reg = 0;
            full_reg = 0;
        end
        afull_reg = full_reg || (wp_reg >= rp_reg ? wp_reg - rp_reg : FIFO_DEPTH - rp_reg + wp_reg) >= FIFO_DEPTH/2;
        if (debugen_in) begin
            $write("%m: input: (%x)%x, output: (%x)%x, full: %x, empty: %x\n", write_in, data_in, read_in, data_out, full_out, empty_out);
        end
    end
    endtask



    always @(posedge clk) begin
        work(reset);
    end

endmodule

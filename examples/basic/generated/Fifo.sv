`default_nettype none

import Predef_pkg::*;


module Fifo #(
    parameter FIFO_WIDTH_BYTES = 64
,   parameter FIFO_DEPTH = 65536
,   parameter SHOWAHEAD = 0
 )
 (
    input wire clk
,   input wire reset
,   input wire write_in
,   input wire[FIFO_WIDTH_BYTES*8-1:0] write_data_in
,   input wire read_in
,   output wire[FIFO_WIDTH_BYTES*8-1:0] read_data_out
,   output wire empty_out
,   output wire full_out
,   input wire clear_in
,   output wire afull_out
,   input wire debugen_in
);

    reg[$clog2(FIFO_DEPTH)-1:0] wp_reg;
    reg[$clog2(FIFO_DEPTH)-1:0] rp_reg;
    reg full_reg;
    reg afull_reg;
    logic full_comb;
    logic empty_comb;

      wire[$clog2((FIFO_DEPTH))-1:0] mem__write_addr_in;
      wire mem__write_in;
      wire[(FIFO_WIDTH_BYTES)*8-1:0] mem__write_data_in;
      wire[(FIFO_WIDTH_BYTES)-1:0] mem__write_mask_in;
      wire[$clog2((FIFO_DEPTH))-1:0] mem__read_addr_in;
      wire mem__read_in;
      wire[(FIFO_WIDTH_BYTES)*8-1:0] mem__read_data_out;
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
,       .write_data_in(mem__write_data_in)
,       .write_mask_in(mem__write_mask_in)
,       .read_addr_in(mem__read_addr_in)
,       .read_in(mem__read_in)
,       .read_data_out(mem__read_data_out)
,       .debugen_in(mem__debugen_in)
    );

    reg[$clog2(FIFO_DEPTH)-1:0] wp_reg_next;
    reg[$clog2(FIFO_DEPTH)-1:0] rp_reg_next;
    reg full_reg_next;
    reg afull_reg_next;


    always @(*) begin  // full_comb_func
        full_comb = (wp_reg == rp_reg) && full_reg;
    end

    always @(*) begin  // empty_comb_func
        empty_comb = (wp_reg == rp_reg) && !full_reg;
    end

    task _work (input logic reset);
    begin: _work
        if (debugen_in) begin
            $write("%m: input: (%x)%x, output: (%x)%x, wp_reg: %x, rp_reg: %x, full: %x, empty: %x, reset: %x\n", signed'(32'(write_in)), write_data_in, signed'(32'(read_in)), read_data_out, wp_reg, rp_reg, signed'(32'(full_reg)), signed'(32'(empty_out)), reset);
        end
        if (reset) begin
            wp_reg_next = '0;
            rp_reg_next = '0;
            full_reg_next = '0;
            afull_reg_next = '0;
            disable _work;
        end
        if (write_in) begin
            if (full_out && !read_in) begin
                $write("%m: writing to a full fifo\n");
                $finish();
            end
            if (!full_out || read_in) begin
                wp_reg_next = wp_reg + 1;
            end
            if (wp_reg_next == rp_reg) begin
                full_reg_next = 1;
            end
        end
        if (read_in) begin
            if (empty_out) begin
                $write("%m: reading from an empty fifo\n");
                $finish();
            end
            if (!empty_out) begin
                rp_reg_next = rp_reg + 1;
            end
            if (!write_in) begin
                full_reg_next = 0;
            end
        end
        if (clear_in) begin
            wp_reg_next = 0;
            rp_reg_next = 0;
            full_reg_next = 0;
        end
        afull_reg_next = full_reg || (wp_reg >= rp_reg ? wp_reg - rp_reg : FIFO_DEPTH - rp_reg + wp_reg) >= FIFO_DEPTH/2;
    end
    endtask

    generate  // _connect
        assign mem__write_data_in = write_data_in;
        assign mem__write_data_in = write_data_in;
        assign mem__write_in = write_in;
        assign mem__write_mask_in = 64'(18446744073709551615);
        assign mem__write_addr_in = wp_reg;
        assign mem__read_in = read_in;
        assign mem__read_addr_in = rp_reg;
        assign mem__debugen_in = debugen_in;
    endgenerate

    always @(posedge clk) begin
        _work(reset);

        wp_reg <= wp_reg_next;
        rp_reg <= rp_reg_next;
        full_reg <= full_reg_next;
        afull_reg <= afull_reg_next;
    end

    assign read_data_out = mem__read_data_out;

    assign empty_out = empty_comb;

    assign full_out = full_comb;

    assign afull_out = afull_reg;


endmodule

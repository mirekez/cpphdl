`default_nettype none

import Predef_pkg::*;


module Fifo #(
    parameter FIFO_WIDTH_BYTES = 64
,   parameter FIFO_DEPTH = 65536
,   parameter SHOWAHEAD = 1
,   parameter OUTPUT_REG = 0
 )
 (
    input wire clk
,   input wire reset
,   input wire write_in
,   input wire[FIFO_WIDTH_BYTES*'h8-1:0] write_data_in
,   input wire read_in
,   output wire[FIFO_WIDTH_BYTES*'h8-1:0] read_data_out
,   output wire empty_out
,   output wire full_out
,   input wire clear_in
,   output wire afull_out
,   input wire debugen_in
);


    // regs and combs
    reg[$clog2(FIFO_DEPTH)-1:0] wp_reg;
    reg[$clog2(FIFO_DEPTH)-1:0] rp_reg;
    reg full_reg;
    reg afull_reg;
    reg read_valid_reg;
    reg[FIFO_WIDTH_BYTES*'h8-1:0] read_data_reg;
    logic full_comb;
    logic empty_comb;
    logic[FIFO_WIDTH_BYTES*'h8-1:0] read_data_comb;
    logic mem_read_comb;
    logic mem_write_comb;

    // members
    wire[$clog2(FIFO_DEPTH)-1:0] mem__write_addr_in;
    wire mem__write_in;
    wire[FIFO_WIDTH_BYTES*'h8-1:0] mem__write_data_in;
    wire[FIFO_WIDTH_BYTES-1:0] mem__write_mask_in;
    wire[$clog2(FIFO_DEPTH)-1:0] mem__read_addr_in;
    wire mem__read_in;
    wire[FIFO_WIDTH_BYTES*'h8-1:0] mem__read_data_out;
    wire mem__debugen_in;
    Memory #(
        FIFO_WIDTH_BYTES
,       FIFO_DEPTH
,       (OUTPUT_REG) ? (1) : (SHOWAHEAD)
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

    // tmp variables
    logic[$clog2(FIFO_DEPTH)-1:0] wp_reg_tmp;
    logic[$clog2(FIFO_DEPTH)-1:0] rp_reg_tmp;
    logic full_reg_tmp;
    logic afull_reg_tmp;
    logic read_valid_reg_tmp;
    logic[FIFO_WIDTH_BYTES*'h8-1:0] read_data_reg_tmp;


    always_comb begin : full_comb_func  // full_comb_func
        if (OUTPUT_REG) begin
            full_comb=(((wp_reg == rp_reg)) && full_reg) && read_valid_reg;
        end
        else begin
            full_comb=((wp_reg == rp_reg)) && full_reg;
        end
    end

    always_comb begin : empty_comb_func  // empty_comb_func
        if (OUTPUT_REG) begin
            empty_comb=!read_valid_reg;
        end
        else begin
            empty_comb=((wp_reg == rp_reg)) && !full_reg;
        end
    end

    always_comb begin : read_data_comb_func  // read_data_comb_func
        if (OUTPUT_REG) begin
            read_data_comb = read_data_reg;
        end
        else begin
            read_data_comb = mem__read_data_out;
        end
    end

    always_comb begin : mem_read_comb_func  // mem_read_comb_func
        if (OUTPUT_REG) begin
            logic mem_empty;
            logic output_needs_word;
            mem_empty=((wp_reg == rp_reg)) && !full_reg;
            output_needs_word=!read_valid_reg || read_in;
            mem_read_comb=output_needs_word && !mem_empty;
        end
        else begin
            mem_read_comb=read_in;
        end
    end

    always_comb begin : mem_write_comb_func  // mem_write_comb_func
        if (OUTPUT_REG) begin
            logic mem_full;
            mem_full=((wp_reg == rp_reg)) && full_reg;
            mem_write_comb=write_in && ((!mem_full || mem_read_comb));
        end
        else begin
            mem_write_comb=write_in;
        end
    end

    task _work (input logic reset);
    begin: _work
        logic mem_read;
        logic mem_write;
        logic output_read;
        logic[16-1:0] wp_next_value;
        logic[63:0] mem_count;
        if (debugen_in) begin
            $write("%m: input: (%x)%x, output: (%x)%x, wp_reg: %x, rp_reg: %x, full: %x, empty: %x, out_valid: %x, reset: %x\n", signed'(32'(write_in)), write_data_in, signed'(32'(read_in)), read_data_out, wp_reg, rp_reg, signed'(32'(full_reg)), signed'(32'(empty_out)), signed'(32'(read_valid_reg)), reset);
        end
        if (reset) begin
            wp_reg_tmp = '0;
            rp_reg_tmp = '0;
            full_reg_tmp = '0;
            afull_reg_tmp = '0;
            read_valid_reg_tmp = '0;
            read_data_reg_tmp = '0;
            disable _work;
        end
        if (OUTPUT_REG) begin
            mem_read=mem_read_comb;
            mem_write=mem_write_comb;
            output_read=read_in && read_valid_reg;
            wp_next_value = wp_reg + 'h1;
            if (write_in && !mem_write) begin
                $write("%m: writing to a full fifo\n");
                $finish();
            end
            if (read_in && !read_valid_reg) begin
                $write("%m: reading from an empty fifo\n");
                $finish();
            end
            if (mem_write) begin
                wp_reg_tmp = wp_reg + 'h1;
            end
            if (mem_read) begin
                rp_reg_tmp = rp_reg + 'h1;
                read_data_reg_tmp = mem__read_data_out;
                read_valid_reg_tmp = unsigned'(1'h1);
            end
            else begin
                if (output_read) begin
                    read_valid_reg_tmp = unsigned'(1'h0);
                end
            end
            if ((mem_write && !mem_read) && (wp_next_value == rp_reg)) begin
                full_reg_tmp = unsigned'(1'h1);
            end
            if (mem_read && !mem_write) begin
                full_reg_tmp = unsigned'(1'h0);
            end
            mem_count=(full_reg) ? (FIFO_DEPTH) : (((wp_reg>=rp_reg) ? (unsigned'(64'((wp_reg - rp_reg)))) : ((FIFO_DEPTH - unsigned'(64'(rp_reg))) + unsigned'(64'(wp_reg)))));
            afull_reg_tmp = unsigned'(1'(mem_count + ((read_valid_reg) ? ('h1) : ('h0))>=FIFO_DEPTH/'h2));
        end
        else begin
            if (write_in) begin
                if (full_out && !read_in) begin
                    $write("%m: writing to a full fifo\n");
                    $finish();
                end
                if (!full_out || read_in) begin
                    wp_reg_tmp = wp_reg + 'h1;
                end
                if (wp_reg_tmp == rp_reg) begin
                    full_reg_tmp = unsigned'(1'h1);
                end
            end
            if (read_in) begin
                if (empty_out) begin
                    $write("%m: reading from an empty fifo\n");
                    $finish();
                end
                if (!empty_out) begin
                    rp_reg_tmp = rp_reg + 'h1;
                end
                if (!write_in) begin
                    full_reg_tmp = unsigned'(1'h0);
                end
            end
            afull_reg_tmp = unsigned'(1'(full_reg || (wp_reg>=rp_reg) ? ((wp_reg - rp_reg)) : (((FIFO_DEPTH - rp_reg) + wp_reg))>=(FIFO_DEPTH/'h2)));
        end
        if (clear_in) begin
            wp_reg_tmp = 'h0;
            rp_reg_tmp = 'h0;
            full_reg_tmp = unsigned'(1'h0);
            read_valid_reg_tmp = unsigned'(1'h0);
        end
    end
    endtask

    generate  // _assign
        assign mem__write_data_in = write_data_in;
        assign mem__write_in = mem_write_comb;
        assign mem__write_mask_in = 64'hFFFFFFFFFFFFFFFF;
        assign mem__write_addr_in = wp_reg;
        assign mem__read_in = mem_read_comb;
        assign mem__read_addr_in = rp_reg;
        assign mem__debugen_in=debugen_in;
    endgenerate

    always @(posedge clk) begin
        wp_reg_tmp = wp_reg;
        rp_reg_tmp = rp_reg;
        full_reg_tmp = full_reg;
        afull_reg_tmp = afull_reg;
        read_valid_reg_tmp = read_valid_reg;
        read_data_reg_tmp = read_data_reg;

        _work(reset);

        wp_reg <= wp_reg_tmp;
        rp_reg <= rp_reg_tmp;
        full_reg <= full_reg_tmp;
        afull_reg <= afull_reg_tmp;
        read_valid_reg <= read_valid_reg_tmp;
        read_data_reg <= read_data_reg_tmp;
    end

    assign read_data_out = read_data_comb;

    assign empty_out = empty_comb;

    assign full_out = full_comb;

    assign afull_out = afull_reg;


endmodule

`default_nettype none

import Predef_pkg::*;


module Memory #(
    parameter MEM_WIDTH_BYTES
,   parameter MEM_DEPTH
,   parameter SHOWAHEAD
,   parameter ID
 )
 (
    input wire clk
,   input wire reset
,   input wire[$clog2(MEM_DEPTH)-1:0] addr0_in
,   input wire write0_in
,   input wire[MEM_WIDTH_BYTES*'h8-1:0] write0_data_in
,   input wire[MEM_WIDTH_BYTES-1:0] write0_mask_in
,   input wire read0_in
,   output wire[MEM_WIDTH_BYTES*'h8-1:0] read0_data_out
,   input wire[$clog2(MEM_DEPTH)-1:0] addr1_in
,   input wire write1_in
,   input wire[MEM_WIDTH_BYTES*'h8-1:0] write1_data_in
,   input wire[MEM_WIDTH_BYTES-1:0] write1_mask_in
,   input wire read1_in
,   output wire[MEM_WIDTH_BYTES*'h8-1:0] read1_data_out
,   input wire debugen_in
);


    // regs and combs
    reg[MEM_WIDTH_BYTES-1:0][8-1:0] buffer[MEM_DEPTH];
    reg[MEM_WIDTH_BYTES*'h8-1:0] data0_out_reg;
    reg[MEM_WIDTH_BYTES*'h8-1:0] data1_out_reg;
    logic[MEM_WIDTH_BYTES*'h8-1:0] read_data0_out_comb;
;
    logic[MEM_WIDTH_BYTES*'h8-1:0] read_data1_out_comb;
;

    // members

    // tmp variables


    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        logic[128-1:0] mask;
        if (debugen_in) begin
            $write("%m: port0: @%x(%x/%x)%x(%x)%x, port1: @%x(%x/%x)%x(%x)%x\n", addr0_in, signed'(32'(write0_in)), signed'(32'(read0_in)), write0_data_in, write0_mask_in, read0_data_out, addr1_in, signed'(32'(write1_in)), signed'(32'(read1_in)), write1_data_in, write1_mask_in, read1_data_out);
        end
        if (write0_in) begin
            mask = 'h0;
            for (i='h0;i < MEM_WIDTH_BYTES;i=i+1) begin
                mask[i*'h8 +:8] = (write0_mask_in[i]) ? ('hFF) : ('h0);
            end
            buffer[addr0_in] <= (buffer[addr0_in] & ~(mask)) | (write0_data_in & mask);
        end
        if (write1_in) begin
            mask = 'h0;
            for (i='h0;i < MEM_WIDTH_BYTES;i=i+1) begin
                mask[i*'h8 +:8] = (write1_mask_in[i]) ? ('hFF) : ('h0);
            end
            buffer[addr1_in] <= (buffer[addr1_in] & ~(mask)) | (write1_data_in & mask);
        end
        if (!SHOWAHEAD) begin
            data0_out_reg <= buffer[addr0_in];
            data1_out_reg <= buffer[addr1_in];
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always_comb begin : read_data0_out_comb_func  // read_data0_out_comb_func
        if (SHOWAHEAD) begin
            read_data0_out_comb=buffer[addr0_in];
        end
        else begin
            read_data0_out_comb=data0_out_reg;
        end
    end

    always_comb begin : read_data1_out_comb_func  // read_data1_out_comb_func
        if (SHOWAHEAD) begin
            read_data1_out_comb=buffer[addr1_in];
        end
        else begin
            read_data1_out_comb=data1_out_reg;
        end
    end

    always @(posedge clk) begin

        _work(reset);

    end

    assign read0_data_out = read_data0_out_comb;

    assign read1_data_out = read_data1_out_comb;


endmodule

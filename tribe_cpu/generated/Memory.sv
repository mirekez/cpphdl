`default_nettype none

import Predef_pkg::*;


module Memory #(
    parameter MEM_WIDTH_BYTES = 32
,   parameter MEM_DEPTH = 3584
,   parameter SHOWAHEAD = 1
,   parameter ID = 'h0
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire[unsigned'(64'($clog2(unsigned'(32'(MEM_DEPTH)))))-1:0] addr0_in
,   input wire write0_in
,   input wire[MEM_WIDTH_BYTES*64'h8-1:0] write0_data_in
,   input wire[MEM_WIDTH_BYTES-1:0] write0_mask_in
,   input wire read0_in
,   output wire[MEM_WIDTH_BYTES*64'h8-1:0] read0_data_out
,   input wire[unsigned'(64'($clog2(unsigned'(32'(MEM_DEPTH)))))-1:0] addr1_in
,   input wire write1_in
,   input wire[MEM_WIDTH_BYTES*64'h8-1:0] write1_data_in
,   input wire[MEM_WIDTH_BYTES-1:0] write1_mask_in
,   input wire read1_in
,   output wire[MEM_WIDTH_BYTES*64'h8-1:0] read1_data_out
,   input wire debugen_in
);


    // regs and combs
    reg[MEM_WIDTH_BYTES-1:0][8-1:0] buffer[MEM_DEPTH];
    reg[MEM_WIDTH_BYTES*64'h8-1:0] data0_out_reg;
    reg[MEM_WIDTH_BYTES*64'h8-1:0] data1_out_reg;
    logic[MEM_WIDTH_BYTES*64'h8-1:0] read_data0_out_comb;
    logic[MEM_WIDTH_BYTES*64'h8-1:0] read_data1_out_comb;

    // members

    // tmp variables


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

    always_ff @(posedge clk) begin

        begin : _work
            logic[63:0] i;
            logic[MEM_WIDTH_BYTES*64'h8-1:0] mask;
            if (debugen_in) begin
                $write("%m: port0: @%x(%x/%x)%x(%x)%x, port1: @%x(%x/%x)%x(%x)%x\n", addr0_in, signed'(32'(write0_in)), signed'(32'(read0_in)), write0_data_in, write0_mask_in, read0_data_out, addr1_in, signed'(32'(write1_in)), signed'(32'(read1_in)), write1_data_in, write1_mask_in, read1_data_out);
            end
            if (write0_in) begin
                mask = 'h0;
                for (i=64'h0;i < MEM_WIDTH_BYTES;i=i+1) begin
                    mask[i*64'h8 +:8] = unsigned'(64'(signed'(32'((write0_mask_in[i]) ? ('hFF) : ('h0)))));
                end
                buffer[addr0_in] <= (buffer[addr0_in] & $bits(mask)'(~(mask))) | (write0_data_in & mask);
            end
            if (write1_in) begin
                mask = 'h0;
                for (i=64'h0;i < MEM_WIDTH_BYTES;i=i+1) begin
                    mask[i*64'h8 +:8] = unsigned'(64'(signed'(32'((write1_mask_in[i]) ? ('hFF) : ('h0)))));
                end
                buffer[addr1_in] <= (buffer[addr1_in] & $bits(mask)'(~(mask))) | (write1_data_in & mask);
            end
            if (!SHOWAHEAD) begin
                data0_out_reg <= buffer[addr0_in];
                data1_out_reg <= buffer[addr1_in];
            end
        end

    end

    always_ff @(posedge l2_clock) begin

        begin : _work_l2_clock
        end

    end

    assign read0_data_out = read_data0_out_comb;

    assign read1_data_out = read_data1_out_comb;


endmodule

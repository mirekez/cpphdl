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
,   input wire[MEM_WIDTH_BYTES*8-1:0] write_data_in
,   input wire[MEM_WIDTH_BYTES-1:0] write_mask_in
,   input wire[$clog2(MEM_DEPTH)-1:0] read_addr_in
,   input wire read_in
,   output wire[MEM_WIDTH_BYTES*8-1:0] read_data_out
,   input wire debugen_in
);

    // regs and combs
    reg[MEM_WIDTH_BYTES*8-1:0] data_out_reg;
    reg[MEM_WIDTH_BYTES-1:0][8-1:0] buffer[MEM_DEPTH];
    logic[MEM_WIDTH_BYTES*8-1:0] data_out_comb;

    // members

    // tmp variables
    logic[MEM_WIDTH_BYTES*8-1:0] data_out_reg_tmp;


    always @(*) begin  // data_out_comb_func
        if (SHOWAHEAD) begin
            data_out_comb = buffer[read_addr_in];
        end
        else begin
            data_out_comb = data_out_reg;
        end
    end

    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        logic[512-1:0] mask;
        logic[64-1:0] mask_in; mask_in = write_mask_in;
        if (debugen_in) begin
            $write("%m: input: (%x)%x@%x(%x), output: (%x)%x@%x\n", signed'(32'(write_in)), write_data_in, write_addr_in, mask_in, signed'(32'(read_in)), read_data_out, read_addr_in);
        end
        if (write_in) begin
            mask = 0;
            for (i = 0;i < MEM_WIDTH_BYTES;i=i+1) begin
                mask[i*8 +:(0 + 1 * 8 - 1 - 0)+1] = mask_in[i] ? 255 : 0;
            end
            buffer[write_addr_in] <= (buffer[write_addr_in] & ~(mask)) | (write_data_in & mask);
        end
        if (!SHOWAHEAD) begin
            data_out_reg_tmp = buffer[read_addr_in];
        end
    end
    endtask

    always @(posedge clk) begin
        _work(reset);

        data_out_reg <= data_out_reg_tmp;
    end

    assign read_data_out = data_out_comb;


endmodule

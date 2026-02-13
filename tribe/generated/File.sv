`default_nettype none

import Predef_pkg::*;


module File #(
    parameter MEM_WIDTH
,   parameter MEM_DEPTH
 )
 (
    input wire clk
,   input wire reset
,   input logic[7:0] write_addr_in
,   input wire write_in
,   input logic[31:0] write_data_in
,   input logic[7:0] read_addr0_in
,   input logic[7:0] read_addr1_in
,   input wire read_in
,   output logic[31:0] read_data0_out
,   output logic[31:0] read_data1_out
,   input wire debugen_in
);

    // regs and combs
    reg[MEM_WIDTH/32-1:0][31:0] buffer[MEM_DEPTH];
    logic[31:0] data0_out_comb;
;
    logic[31:0] data1_out_comb;
;

    // members

    // tmp variables


    task _work (input logic reset);
    begin: _work
        logic[7:0] i;
        if (reset) begin
            for (i = 0;i < MEM_DEPTH;i=i+1) begin
                buffer[i] <= 0;
            end
        end
        if (debugen_in) begin
            $write("%m: port0: @%x(%x)%08x, port1: @%x(%x)%08x @%x(%x)%08x\n", write_addr_in, signed'(32'(write_in)), write_data_in, read_addr0_in, signed'(32'(read_in)), read_data0_out, read_addr1_in, signed'(32'(read_in)), read_data1_out);
        end
        if (write_in) begin
            buffer[write_addr_in] <= write_data_in;
        end
    end
    endtask

    generate  // _connect
    endgenerate

    always @(*) begin  // data0_out_comb_func
        data0_out_comb = buffer[(read_addr0_in)];
    end

    always @(*) begin  // data1_out_comb_func
        data1_out_comb = buffer[(read_addr1_in)];
    end

    always @(posedge clk) begin
        _work(reset);

    end

    assign read_data0_out = data0_out_comb;

    assign read_data1_out = data1_out_comb;


endmodule

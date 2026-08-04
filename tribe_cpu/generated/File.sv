`default_nettype none

import Predef_pkg::*;


module File #(
    parameter MEM_WIDTH
,   parameter MEM_DEPTH
 )
 (
    input wire clk
,   input wire reset
,   input wire[7:0] write_addr_in
,   input wire write_in
,   input wire[31:0] write_data_in
,   input wire[7:0] read_addr0_in
,   input wire[7:0] read_addr1_in
,   input wire read_in
,   output wire[31:0] read_data0_out
,   output wire[31:0] read_data1_out
,   input wire[31:0] reset_x10_in
,   input wire[31:0] reset_x11_in
,   output wire[31:0] x1_out
,   output wire[31:0] x10_out
,   output wire[31:0] x11_out
,   output wire[31:0] x17_out
,   input wire debugen_in
);

    typedef logic[31:0] DTYPE;

    // regs and combs
    reg[MEM_WIDTH/'h20-1:0][32-1:0] buffer[MEM_DEPTH];
    logic[31:0] data0_out_comb;
;
    logic[31:0] data1_out_comb;
;
    logic[31:0] x1_comb;
;
    logic[31:0] x10_comb;
;
    logic[31:0] x11_comb;
;
    logic[31:0] x17_comb;
;

    // members

    // tmp variables


    task _work (input logic reset);
    begin: _work
        logic[7:0] i;
        if (reset) begin
            for (i='h0;i < MEM_DEPTH;i=i+1) begin
                buffer[i] <= 'h0;
            end
            buffer['hA] <= reset_x10_in;
            buffer['hB] <= reset_x11_in;
        end
        if (debugen_in) begin
            $write("%m: port0: @%x(%x)%08x, port1: @%x(%x)%08x @%x(%x)%08x\n", write_addr_in, signed'(32'(write_in)), write_data_in, read_addr0_in, signed'(32'(read_in)), read_data0_out, read_addr1_in, signed'(32'(read_in)), read_data1_out);
        end
        if (write_in) begin
            buffer[write_addr_in] <= write_data_in;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always_comb begin : data0_out_comb_func  // data0_out_comb_func
        if (write_in && (write_addr_in == read_addr0_in)) begin
            data0_out_comb=write_data_in;
        end
        else begin
            data0_out_comb=buffer[read_addr0_in];
        end
    end

    always_comb begin : data1_out_comb_func  // data1_out_comb_func
        if (write_in && (write_addr_in == read_addr1_in)) begin
            data1_out_comb=write_data_in;
        end
        else begin
            data1_out_comb=buffer[read_addr1_in];
        end
    end

    always_comb begin : x1_comb_func  // x1_comb_func
        x1_comb=buffer['h1];
    end

    always_comb begin : x10_comb_func  // x10_comb_func
        x10_comb=buffer['hA];
    end

    always_comb begin : x11_comb_func  // x11_comb_func
        x11_comb=buffer['hB];
    end

    always_comb begin : x17_comb_func  // x17_comb_func
        x17_comb=buffer['h11];
    end

    always @(posedge clk) begin

        _work(reset);

    end

    assign read_data0_out = data0_out_comb;

    assign read_data1_out = data1_out_comb;

    assign x1_out = x1_comb;

    assign x10_out = x10_comb;

    assign x11_out = x11_comb;

    assign x17_out = x17_comb;


endmodule

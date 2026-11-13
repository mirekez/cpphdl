`default_nettype none

import Predef_pkg::*;


module File #(
    parameter MEM_WIDTH = 32
,   parameter MEM_DEPTH = 32
,   parameter PRIMARY_WRITE_FIRST = 1
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire[7:0] write_addr_in
,   input wire write_in
,   input wire[31:0] write_data_in
,   input wire[7:0] write2_addr_in
,   input wire write2_in
,   input wire[31:0] write2_data_in
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
,   output wire[31:0] x16_out
,   output wire[31:0] x17_out
,   input wire debugen_in
);

    typedef logic[31:0] DTYPE;

    // regs and combs
    logic[31:0] data0_out_comb;
    logic[31:0] data1_out_comb;
    logic[31:0] x1_comb;
    logic[31:0] x10_comb;
    logic[31:0] x11_comb;
    logic[31:0] x16_comb;
    logic[31:0] x17_comb;

    // members
    wire[7:0] storage__write_addr_in;
    wire storage__write_in;
    wire[31:0] storage__write_data_in;
    wire[7:0] storage__write2_addr_in;
    wire storage__write2_in;
    wire[31:0] storage__write2_data_in;
    wire[7:0] storage__read_addr0_in;
    wire[7:0] storage__read_addr1_in;
    wire[31:0] storage__reset_x10_in;
    wire[31:0] storage__reset_x11_in;
    wire[31:0] storage__read_data0_out;
    wire[31:0] storage__read_data1_out;
    wire[31:0] storage__x1_out;
    wire[31:0] storage__x10_out;
    wire[31:0] storage__x11_out;
    wire[31:0] storage__x16_out;
    wire[31:0] storage__x17_out;
    FileStorage #(
        MEM_WIDTH
,       MEM_DEPTH
    ) storage (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .write_addr_in(storage__write_addr_in)
,       .write_in(storage__write_in)
,       .write_data_in(storage__write_data_in)
,       .write2_addr_in(storage__write2_addr_in)
,       .write2_in(storage__write2_in)
,       .write2_data_in(storage__write2_data_in)
,       .read_addr0_in(storage__read_addr0_in)
,       .read_addr1_in(storage__read_addr1_in)
,       .reset_x10_in(storage__reset_x10_in)
,       .reset_x11_in(storage__reset_x11_in)
,       .read_data0_out(storage__read_data0_out)
,       .read_data1_out(storage__read_data1_out)
,       .x1_out(storage__x1_out)
,       .x10_out(storage__x10_out)
,       .x11_out(storage__x11_out)
,       .x16_out(storage__x16_out)
,       .x17_out(storage__x17_out)
    );

    // tmp variables


    always_comb begin : data0_out_comb_func  // data0_out_comb_func
        if ((PRIMARY_WRITE_FIRST && write_in) && (signed'(32'(write_addr_in)) == signed'(32'(read_addr0_in)))) begin
            data0_out_comb=write_data_in;
        end
        else begin
            if (write2_in && (signed'(32'(write2_addr_in)) == signed'(32'(read_addr0_in)))) begin
                data0_out_comb=write2_data_in;
            end
            else begin
                data0_out_comb=storage__read_data0_out;
            end
        end
    end

    always_comb begin : data1_out_comb_func  // data1_out_comb_func
        if ((PRIMARY_WRITE_FIRST && write_in) && (signed'(32'(write_addr_in)) == signed'(32'(read_addr1_in)))) begin
            data1_out_comb=write_data_in;
        end
        else begin
            if (write2_in && (signed'(32'(write2_addr_in)) == signed'(32'(read_addr1_in)))) begin
                data1_out_comb=write2_data_in;
            end
            else begin
                data1_out_comb=storage__read_data1_out;
            end
        end
    end

    always_comb begin : x1_comb_func  // x1_comb_func
        x1_comb=storage__x1_out;
    end

    always_comb begin : x10_comb_func  // x10_comb_func
        if ((PRIMARY_WRITE_FIRST && write_in) && (signed'(32'(write_addr_in)) == 32'shA)) begin
            x10_comb=write_data_in;
        end
        else begin
            if (write2_in && (signed'(32'(write2_addr_in)) == 32'shA)) begin
                x10_comb=write2_data_in;
            end
            else begin
                x10_comb=storage__x10_out;
            end
        end
    end

    always_comb begin : x11_comb_func  // x11_comb_func
        if ((PRIMARY_WRITE_FIRST && write_in) && (signed'(32'(write_addr_in)) == 32'shB)) begin
            x11_comb=write_data_in;
        end
        else begin
            if (write2_in && (signed'(32'(write2_addr_in)) == 32'shB)) begin
                x11_comb=write2_data_in;
            end
            else begin
                x11_comb=storage__x11_out;
            end
        end
    end

    always_comb begin : x16_comb_func  // x16_comb_func
        if ((PRIMARY_WRITE_FIRST && write_in) && (signed'(32'(write_addr_in)) == 32'sh10)) begin
            x16_comb=write_data_in;
        end
        else begin
            if (write2_in && (signed'(32'(write2_addr_in)) == 32'sh10)) begin
                x16_comb=write2_data_in;
            end
            else begin
                x16_comb=storage__x16_out;
            end
        end
    end

    always_comb begin : x17_comb_func  // x17_comb_func
        if ((PRIMARY_WRITE_FIRST && write_in) && (signed'(32'(write_addr_in)) == 32'sh11)) begin
            x17_comb=write_data_in;
        end
        else begin
            if (write2_in && (signed'(32'(write2_addr_in)) == 32'sh11)) begin
                x17_comb=write2_data_in;
            end
            else begin
                x17_comb=storage__x17_out;
            end
        end
    end

    task _work (input logic reset);
    begin: _work
        if (debugen_in) begin
            $write("%m: port0: @%x(%x)%08x, port1: @%x(%x)%08x @%x(%x)%08x\n", write_addr_in, signed'(32'(write_in)), write_data_in, read_addr0_in, signed'(32'(read_in)), read_data0_out, read_addr1_in, signed'(32'(read_in)), read_data1_out);
        end
        if (write_in) begin
        end
    end
    endtask

    generate  // _assign
        assign storage__write_addr_in = write_addr_in;
        assign storage__write_in = write_in;
        assign storage__write_data_in = write_data_in;
        assign storage__write2_addr_in = write2_addr_in;
        assign storage__write2_in = write2_in;
        assign storage__write2_data_in = write2_data_in;
        assign storage__read_addr0_in = read_addr0_in;
        assign storage__read_addr1_in = read_addr1_in;
        assign storage__reset_x10_in = reset_x10_in;
        assign storage__reset_x11_in = reset_x11_in;
    endgenerate

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin

            _work(reset);

    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign read_data0_out = data0_out_comb;

    assign read_data1_out = data1_out_comb;

    assign x1_out = x1_comb;

    assign x10_out = x10_comb;

    assign x11_out = x11_comb;

    assign x16_out = x16_comb;

    assign x17_out = x17_comb;


endmodule

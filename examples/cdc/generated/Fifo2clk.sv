`default_nettype none

import Predef_pkg::*;


module Fifo2clk (
    input wire write_clk
,   input wire read_clk
,   input wire reset
,   input wire write_valid_in
,   input wire[8-1:0] write_data_in
,   output wire write_ready_out
,   input wire read_ready_in
,   output wire read_valid_out
,   output wire[8-1:0] read_data_out
);
    parameter  DEPTH = 64'h10;
    parameter  ADDR_BITS = 64'h4;
    parameter  PTR_BITS = 64'h5;


    // regs and combs
    reg[1-1:0][8-1:0] data_mem[16];
    reg[5-1:0] write_bin_reg;
    reg[5-1:0] write_gray_reg;
    reg[5-1:0] read_gray_write1_reg;
    reg[5-1:0] read_gray_write2_reg;
    reg[5-1:0] read_bin_reg;
    reg[5-1:0] read_gray_reg;
    reg[5-1:0] write_gray_read1_reg;
    reg[5-1:0] write_gray_read2_reg;
    logic write_ready_comb;
    logic read_valid_comb;
    logic[8-1:0] read_data_comb;

    // members

    // tmp variables
    logic[5-1:0] write_bin_reg_tmp;
    logic[5-1:0] write_gray_reg_tmp;
    logic[5-1:0] read_gray_write1_reg_tmp;
    logic[5-1:0] read_gray_write2_reg_tmp;
    logic[5-1:0] read_bin_reg_tmp;
    logic[5-1:0] read_gray_reg_tmp;
    logic[5-1:0] write_gray_read1_reg_tmp;
    logic[5-1:0] write_gray_read2_reg_tmp;


    always_comb begin : write_ready_comb_func  // write_ready_comb_func
        logic[5-1:0] full_gray;
        full_gray = unsigned'(PTR_BITS'(read_gray_write2_reg ^ unsigned'(PTR_BITS'(unsigned'(PTR_BITS'(((('h1 <<< ADDR_BITS)) | (('h1 <<< ((ADDR_BITS - 'h1)))))))))));
        write_ready_comb=write_gray_reg != full_gray;
    end

    always_comb begin : read_valid_comb_func  // read_valid_comb_func
        read_valid_comb=read_gray_reg != write_gray_read2_reg;
    end

    always_comb begin : read_data_comb_func  // read_data_comb_func
        read_data_comb = unsigned'(8'(data_mem[unsigned'(32'(read_bin_reg)) & unsigned'(32'(((DEPTH - 'h1))))]));
    end

    task _work_write_clk (input logic reset);
    begin: _work_write_clk
        logic[5-1:0] next;
        read_gray_write1_reg_tmp = read_gray_reg;
        read_gray_write2_reg_tmp = read_gray_write1_reg;
        if (write_valid_in && write_ready_comb) begin
            data_mem[unsigned'(32'(write_bin_reg)) & unsigned'(32'(((DEPTH - 'h1))))] <= write_data_in;
            next = write_bin_reg + 'h1;
            write_bin_reg_tmp = next;
            write_gray_reg_tmp = next ^ ((next >>> 'h1));
        end
        if (reset) begin
            write_bin_reg_tmp = '0;
            write_gray_reg_tmp = '0;
            read_gray_write1_reg_tmp = '0;
            read_gray_write2_reg_tmp = '0;
        end
    end
    endtask

    task _work_neg_write_clk (input logic unused);
    begin: _work_neg_write_clk
    end
    endtask

    task _work_read_clk (input logic reset);
    begin: _work_read_clk
        logic[5-1:0] next;
        write_gray_read1_reg_tmp = write_gray_reg;
        write_gray_read2_reg_tmp = write_gray_read1_reg;
        if (read_ready_in && read_valid_comb) begin
            next = read_bin_reg + 'h1;
            read_bin_reg_tmp = next;
            read_gray_reg_tmp = next ^ ((next >>> 'h1));
        end
        if (reset) begin
            read_bin_reg_tmp = '0;
            read_gray_reg_tmp = '0;
            write_gray_read1_reg_tmp = '0;
            write_gray_read2_reg_tmp = '0;
        end
    end
    endtask

    task _work_neg_read_clk (input logic unused);
    begin: _work_neg_read_clk
    end
    endtask

    generate  // _assign
    endgenerate

    always_ff @(posedge write_clk) begin
        write_bin_reg_tmp = write_bin_reg;
        write_gray_reg_tmp = write_gray_reg;
        read_gray_write1_reg_tmp = read_gray_write1_reg;
        read_gray_write2_reg_tmp = read_gray_write2_reg;

        _work_write_clk(reset);

        write_bin_reg <= write_bin_reg_tmp;
        write_gray_reg <= write_gray_reg_tmp;
        read_gray_write1_reg <= read_gray_write1_reg_tmp;
        read_gray_write2_reg <= read_gray_write2_reg_tmp;
    end

    always_ff @(negedge write_clk) begin

        _work_neg_write_clk(reset);

    end

    always_ff @(posedge read_clk) begin
        read_bin_reg_tmp = read_bin_reg;
        read_gray_reg_tmp = read_gray_reg;
        write_gray_read1_reg_tmp = write_gray_read1_reg;
        write_gray_read2_reg_tmp = write_gray_read2_reg;

        _work_read_clk(reset);

        read_bin_reg <= read_bin_reg_tmp;
        read_gray_reg <= read_gray_reg_tmp;
        write_gray_read1_reg <= write_gray_read1_reg_tmp;
        write_gray_read2_reg <= write_gray_read2_reg_tmp;
    end

    always_ff @(negedge read_clk) begin

        _work_neg_read_clk(reset);

    end

    assign write_ready_out = write_ready_comb;

    assign read_valid_out = read_valid_comb;

    assign read_data_out = read_data_comb;


endmodule

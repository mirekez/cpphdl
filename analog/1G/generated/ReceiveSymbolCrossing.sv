`default_nettype none

import Predef_pkg::*;


module ReceiveSymbolCrossing (
    input wire adc_clk
,   input wire pcs_clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire[16-1:0] tag_in
,   input wire clipped_in
,   input wire valid_in
,   input wire fault_in
,   output wire[32-1:0] samples_out
,   output wire[16-1:0] tag_out
,   output wire clipped_out
,   output wire valid_out
,   output wire source_fault_out
,   output wire fault_out
);
    localparam  DEPTH = 'h8;
    localparam  POINTER_BITS = 'h4;


    // regs and combs
    reg[49-1:0] words[8];
    reg[4-1:0] write_bin;
    reg[4-1:0] write_gray;
    reg[4-1:0] read_bin;
    reg[4-1:0] read_gray;
    (* ASYNC_REG = "TRUE" *)
    logic[4-1:0] read_sync1;
    (* ASYNC_REG = "TRUE" *)
    logic[4-1:0] read_sync2;
    (* ASYNC_REG = "TRUE" *)
    logic[4-1:0] write_sync1;
    (* ASYNC_REG = "TRUE" *)
    logic[4-1:0] write_sync2;
    reg source_fault;
    (* ASYNC_REG = "TRUE" *)
    logic fault_sync1;
    (* ASYNC_REG = "TRUE" *)
    logic fault_sync2;
    reg destination_fault;
    reg valid;
    reg[49-1:0] packet;

    // members

    // tmp variables
    logic[4-1:0] write_bin_tmp;
    logic[4-1:0] write_gray_tmp;
    logic[4-1:0] read_bin_tmp;
    logic[4-1:0] read_gray_tmp;
    logic[4-1:0] read_sync1_tmp;
    logic[4-1:0] read_sync2_tmp;
    logic[4-1:0] write_sync1_tmp;
    logic[4-1:0] write_sync2_tmp;
    logic source_fault_tmp;
    logic fault_sync1_tmp;
    logic fault_sync2_tmp;
    logic destination_fault_tmp;
    logic valid_tmp;
    logic[49-1:0] packet_tmp;


    generate  // _assign
    endgenerate

    always_ff @(posedge adc_clk) begin
        write_bin_tmp = write_bin;
        write_gray_tmp = write_gray;
        read_sync1_tmp = read_sync1;
        read_sync2_tmp = read_sync2;
        source_fault_tmp = source_fault;

        begin : _work_adc_clk
            logic[unsigned'(64'(POINTER_BITS))-1:0] next;
            logic[49-1:0] word;
            logic full;
            read_sync1_tmp = read_gray;
            read_sync2_tmp = read_sync1;
            full=write_gray == ((read_sync2 ^ (unsigned'(64'(POINTER_BITS)))'(64'hC)));
            if (((valid_in && !fault_in) && !((source_fault) != '0)) && !reset) begin
                if (full) begin
                    source_fault_tmp = (('h1) != '0);
                end
                else begin
                    word = tag_in;
                    word = ((word <<< 'h20)) | samples_in;
                    if (clipped_in) begin
                        word = word | ((49'(64'h1) <<< 'h30));
                    end
                    words[64'(32'(unsigned'(32'(write_bin)) & ((DEPTH - 32'h1))))] <= word;
                    next = write_bin + 64'h1;
                    write_bin_tmp = next;
                    write_gray_tmp = next ^ ((next >>> 32'sh1));
                end
            end
            if (fault_in) begin
                source_fault_tmp = (('h1) != '0);
            end
            if (reset) begin
                write_bin_tmp = '0;
                write_gray_tmp = '0;
                read_sync1_tmp = '0;
                read_sync2_tmp = '0;
                source_fault_tmp = '0;
            end
        end

        write_bin <= write_bin_tmp;
        write_gray <= write_gray_tmp;
        read_sync1 <= read_sync1_tmp;
        read_sync2 <= read_sync2_tmp;
        source_fault <= source_fault_tmp;
    end

    always_ff @(negedge adc_clk) begin

        begin : _work_neg_adc_clk
            logic unused;
            unused = reset;
        end

    end

    always_ff @(posedge pcs_clk) begin
        read_bin_tmp = read_bin;
        read_gray_tmp = read_gray;
        write_sync1_tmp = write_sync1;
        write_sync2_tmp = write_sync2;
        fault_sync1_tmp = fault_sync1;
        fault_sync2_tmp = fault_sync2;
        destination_fault_tmp = destination_fault;
        valid_tmp = valid;
        packet_tmp = packet;

        begin : _work_pcs_clk
            logic[unsigned'(64'(POINTER_BITS))-1:0] next;
            write_sync1_tmp = write_gray;
            write_sync2_tmp = write_sync1;
            fault_sync1_tmp = source_fault;
            fault_sync2_tmp = fault_sync1;
            valid_tmp = (('h0) != '0);
            if ((!((destination_fault) != '0) && !((fault_sync2) != '0)) && (read_gray != write_sync2)) begin
                packet_tmp = words[64'(32'(unsigned'(32'(read_bin)) & ((DEPTH - 32'h1))))];
                valid_tmp = (('h1) != '0);
                next = read_bin + 64'h1;
                read_bin_tmp = next;
                read_gray_tmp = next ^ ((next >>> 32'sh1));
            end
            if (((fault_sync2) != '0)) begin
                destination_fault_tmp = (('h1) != '0);
            end
            if (reset) begin
                read_bin_tmp = '0;
                read_gray_tmp = '0;
                write_sync1_tmp = '0;
                write_sync2_tmp = '0;
                fault_sync1_tmp = '0;
                fault_sync2_tmp = '0;
                destination_fault_tmp = '0;
                valid_tmp = '0;
                packet_tmp = '0;
            end
        end

        read_bin <= read_bin_tmp;
        read_gray <= read_gray_tmp;
        write_sync1 <= write_sync1_tmp;
        write_sync2 <= write_sync2_tmp;
        fault_sync1 <= fault_sync1_tmp;
        fault_sync2 <= fault_sync2_tmp;
        destination_fault <= destination_fault_tmp;
        valid <= valid_tmp;
        packet <= packet_tmp;
    end

    always_ff @(negedge pcs_clk) begin

        begin : _work_neg_pcs_clk
            logic unused;
            unused = reset;
        end

    end

    assign samples_out = packet & 64'hFFFFFFFF;

    assign tag_out = ((packet >>> 32'sh20)) & 64'hFFFF;

    assign clipped_out = ((((packet >>> 32'sh30)) != 64'h0) != '0);

    assign valid_out = valid;

    assign source_fault_out = source_fault;

    assign fault_out = destination_fault;


endmodule

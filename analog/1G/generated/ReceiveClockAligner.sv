`default_nettype none

import Predef_pkg::*;


module ReceiveClockAligner (
    input wire ref_clk
,   input wire rx_clk
,   input wire reset
,   input wire[10-1:0] receive_in
,   output wire[10-1:0] aligned_out
,   output wire overflow_out
,   output wire fault_out
);
    localparam  DEPTH = 'h40;
    localparam  MASK = 'h3F;
    localparam  POINTER_BITS = 'h7;
    localparam  PREFILL = 'h8;


    // regs and combs
    reg[10-1:0] words[64];
    reg[7-1:0] write_bin;
    reg[7-1:0] write_gray;
    reg[7-1:0] read_bin;
    reg[7-1:0] read_gray;
    (* ASYNC_REG = "TRUE" *)
    logic[7-1:0] read_sync1;
    (* ASYNC_REG = "TRUE" *)
    logic[7-1:0] read_sync2;
    (* ASYNC_REG = "TRUE" *)
    logic[7-1:0] write_sync1;
    (* ASYNC_REG = "TRUE" *)
    logic[7-1:0] write_sync2;
    reg[7-1:0] written_frames;
    reg[7-1:0] written_frames_gray;
    reg[7-1:0] read_frames;
    reg[7-1:0] read_frames_gray;
    (* ASYNC_REG = "TRUE" *)
    logic[7-1:0] frames_sync1;
    (* ASYNC_REG = "TRUE" *)
    logic[7-1:0] frames_sync2;
    reg previous_valid;
    reg overflow_reg;
    (* ASYNC_REG = "TRUE" *)
    logic overflow_sync1;
    (* ASYNC_REG = "TRUE" *)
    logic overflow_sync2;
    reg in_frame;
    reg fault_reg;
    reg[4-1:0] gap;
    reg[10-1:0] output_reg;

    // members

    // tmp variables
    logic[7-1:0] write_bin_tmp;
    logic[7-1:0] write_gray_tmp;
    logic[7-1:0] read_bin_tmp;
    logic[7-1:0] read_gray_tmp;
    logic[7-1:0] read_sync1_tmp;
    logic[7-1:0] read_sync2_tmp;
    logic[7-1:0] write_sync1_tmp;
    logic[7-1:0] write_sync2_tmp;
    logic[7-1:0] written_frames_tmp;
    logic[7-1:0] written_frames_gray_tmp;
    logic[7-1:0] read_frames_tmp;
    logic[7-1:0] read_frames_gray_tmp;
    logic[7-1:0] frames_sync1_tmp;
    logic[7-1:0] frames_sync2_tmp;
    logic previous_valid_tmp;
    logic overflow_reg_tmp;
    logic overflow_sync1_tmp;
    logic overflow_sync2_tmp;
    logic in_frame_tmp;
    logic fault_reg_tmp;
    logic[4-1:0] gap_tmp;
    logic[10-1:0] output_reg_tmp;


    generate  // _assign
    endgenerate

    always_ff @(posedge ref_clk) begin
        read_bin_tmp = read_bin;
        read_gray_tmp = read_gray;
        write_sync1_tmp = write_sync1;
        write_sync2_tmp = write_sync2;
        read_frames_tmp = read_frames;
        read_frames_gray_tmp = read_frames_gray;
        frames_sync1_tmp = frames_sync1;
        frames_sync2_tmp = frames_sync2;
        overflow_sync1_tmp = overflow_sync1;
        overflow_sync2_tmp = overflow_sync2;
        in_frame_tmp = in_frame;
        fault_reg_tmp = fault_reg;
        gap_tmp = gap;
        output_reg_tmp = output_reg;

        begin : _work_ref_clk
            logic[10-1:0] word;
            logic empty;
            logic[unsigned'(64'(POINTER_BITS))-1:0] remote;
            logic[unsigned'(64'(POINTER_BITS))-1:0] available;
            logic[unsigned'(64'(POINTER_BITS))-1:0] next;
            write_sync1_tmp = write_gray;
            write_sync2_tmp = write_sync1;
            frames_sync1_tmp = written_frames_gray;
            frames_sync2_tmp = frames_sync1;
            overflow_sync1_tmp = overflow_reg;
            overflow_sync2_tmp = overflow_sync1;
            output_reg_tmp = 64'h0;
            remote = write_sync2;
            remote = remote ^ ((remote >>> 32'sh1));
            remote = remote ^ ((remote >>> 32'sh2));
            remote = remote ^ ((remote >>> 32'sh4));
            available = remote - read_bin;
            empty=read_gray == write_sync2;
            if (gap < 64'hC) begin
                gap_tmp = gap + 64'h1;
            end
            if (!((fault_reg) != '0) && !((overflow_sync2) != '0)) begin
                if (((in_frame) != '0) || (((gap>=64'hC && !empty) && ((available>=unsigned'(64'(PREFILL)) || (frames_sync2 != read_frames_gray)))))) begin
                    if (empty) begin
                        output_reg_tmp = 64'h300;
                        fault_reg_tmp = (('h1) != '0);
                        in_frame_tmp = (('h0) != '0);
                    end
                    else begin
                        word = unsigned'(32'(words[64'(32'(unsigned'(32'(read_bin)) & MASK))]));
                        output_reg_tmp = word;
                        in_frame_tmp = ((word & 64'h100)) != 64'h0;
                        if (((word & 64'h100)) == 64'h0) begin
                            gap_tmp = 64'h1;
                            next = read_frames + 64'h1;
                            read_frames_tmp = next;
                            read_frames_gray_tmp = next ^ ((next >>> 32'sh1));
                        end
                        next = read_bin + 64'h1;
                        read_bin_tmp = next;
                        read_gray_tmp = next ^ ((next >>> 32'sh1));
                    end
                end
            end
            if (((overflow_sync2) != '0) && !((fault_reg) != '0)) begin
                if (((in_frame) != '0)) begin
                    output_reg_tmp = 64'h300;
                end
                fault_reg_tmp = (('h1) != '0);
                in_frame_tmp = (('h0) != '0);
            end
            if (reset) begin
                read_bin_tmp = '0;
                read_gray_tmp = '0;
                write_sync1_tmp = '0;
                write_sync2_tmp = '0;
                frames_sync1_tmp = '0;
                frames_sync2_tmp = '0;
                read_frames_tmp = '0;
                read_frames_gray_tmp = '0;
                overflow_sync1_tmp = '0;
                overflow_sync2_tmp = '0;
                in_frame_tmp = '0;
                fault_reg_tmp = '0;
                gap_tmp = '0;
                output_reg_tmp = '0;
            end
        end

        read_bin <= read_bin_tmp;
        read_gray <= read_gray_tmp;
        write_sync1 <= write_sync1_tmp;
        write_sync2 <= write_sync2_tmp;
        read_frames <= read_frames_tmp;
        read_frames_gray <= read_frames_gray_tmp;
        frames_sync1 <= frames_sync1_tmp;
        frames_sync2 <= frames_sync2_tmp;
        overflow_sync1 <= overflow_sync1_tmp;
        overflow_sync2 <= overflow_sync2_tmp;
        in_frame <= in_frame_tmp;
        fault_reg <= fault_reg_tmp;
        gap <= gap_tmp;
        output_reg <= output_reg_tmp;
    end

    always_ff @(negedge ref_clk) begin

        begin : _work_neg_ref_clk
            logic unused;
            unused = reset;
        end

    end

    always_ff @(posedge rx_clk) begin
        write_bin_tmp = write_bin;
        write_gray_tmp = write_gray;
        read_sync1_tmp = read_sync1;
        read_sync2_tmp = read_sync2;
        written_frames_tmp = written_frames;
        written_frames_gray_tmp = written_frames_gray;
        previous_valid_tmp = previous_valid;
        overflow_reg_tmp = overflow_reg;

        begin : _work_rx_clk
            logic[unsigned'(64'(POINTER_BITS))-1:0] next;
            logic valid;
            logic write;
            logic full;
            read_sync1_tmp = read_gray;
            read_sync2_tmp = read_sync1;
            valid=((receive_in & 64'h100)) != 64'h0;
            write=valid || ((previous_valid) != '0);
            previous_valid_tmp = valid;
            full=write_gray == ((read_sync2 ^ (unsigned'(64'(POINTER_BITS)))'(64'h60)));
            if (write && !((overflow_reg) != '0)) begin
                if (full) begin
                    overflow_reg_tmp = (('h1) != '0);
                end
                else begin
                    words[64'(32'(unsigned'(32'(write_bin)) & MASK))] <= (valid) ? (receive_in) : (10'(64'h0));
                    next = write_bin + 64'h1;
                    write_bin_tmp = next;
                    write_gray_tmp = next ^ ((next >>> 32'sh1));
                    if (!valid) begin
                        next = written_frames + 64'h1;
                        written_frames_tmp = next;
                        written_frames_gray_tmp = next ^ ((next >>> 32'sh1));
                    end
                end
            end
            if (reset) begin
                write_bin_tmp = '0;
                write_gray_tmp = '0;
                read_sync1_tmp = '0;
                read_sync2_tmp = '0;
                previous_valid_tmp = '0;
                overflow_reg_tmp = '0;
                written_frames_tmp = '0;
                written_frames_gray_tmp = '0;
            end
        end

        write_bin <= write_bin_tmp;
        write_gray <= write_gray_tmp;
        read_sync1 <= read_sync1_tmp;
        read_sync2 <= read_sync2_tmp;
        written_frames <= written_frames_tmp;
        written_frames_gray <= written_frames_gray_tmp;
        previous_valid <= previous_valid_tmp;
        overflow_reg <= overflow_reg_tmp;
    end

    always_ff @(negedge rx_clk) begin

        begin : _work_neg_rx_clk
            logic unused;
            unused = reset;
        end

    end

    assign aligned_out = output_reg;

    assign overflow_out = overflow_reg;

    assign fault_out = fault_reg;


endmodule

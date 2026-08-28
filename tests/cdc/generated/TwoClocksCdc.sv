`default_nettype none

import Predef_pkg::*;


module TwoClocksCdc (
    input wire fast_clk
,   input wire slow_clk
,   input wire reset
,   input wire fast_enable_in
,   input wire slow_enable_in
,   input wire level_fast_in
,   input wire pulse_fast_in
,   input wire mailbox_send_fast_in
,   input wire[16-1:0] mailbox_data_fast_in
,   input wire reset_release_in
,   input wire fifo_write_valid_in
,   input wire[8-1:0] fifo_write_data_in
,   input wire fifo_read_ready_in
,   output wire[8-1:0] fast_count_out
,   output wire[8-1:0] slow_count_out
,   output wire[8-1:0] slow_gray_fast_out
,   output wire[8-1:0] fast_gray_slow_out
,   output wire level_slow_out
,   output wire pulse_slow_out
,   output wire mailbox_busy_fast_out
,   output wire mailbox_valid_slow_out
,   output wire[16-1:0] mailbox_data_slow_out
,   output wire reset_released_fast_out
,   output wire reset_released_slow_out
,   output wire[8-1:0] fast_negedge_count_out
,   output wire fifo_write_ready_out
,   output wire fifo_read_valid_out
,   output wire[8-1:0] fifo_read_data_out
);
    localparam  FIFO_DEPTH = 64'h4;
    localparam  FIFO_ADDR_BITS = 64'h2;
    localparam  FIFO_PTR_BITS = 64'h3;


    // regs and combs
    reg[8-1:0] fast_count_reg;
    reg[8-1:0] fast_gray_reg;
    (* ASYNC_REG = "TRUE" *)
    logic[8-1:0] slow_gray_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic[8-1:0] slow_gray_fast2_reg;
    reg[8-1:0] slow_count_reg;
    reg[8-1:0] slow_gray_reg;
    (* ASYNC_REG = "TRUE" *)
    logic[8-1:0] fast_gray_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic[8-1:0] fast_gray_slow2_reg;
    reg level_fast_reg;
    (* ASYNC_REG = "TRUE" *)
    logic level_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic level_slow2_reg;
    reg pulse_toggle_fast_reg;
    (* ASYNC_REG = "TRUE" *)
    logic pulse_toggle_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic pulse_toggle_slow2_reg;
    reg pulse_toggle_slow_last_reg;
    reg pulse_slow_reg;
    reg[16-1:0] mailbox_data_fast_reg;
    reg mailbox_request_fast_reg;
    (* ASYNC_REG = "TRUE" *)
    logic mailbox_ack_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic mailbox_ack_fast2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic mailbox_request_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic mailbox_request_slow2_reg;
    reg mailbox_ack_slow_reg;
    reg[16-1:0] mailbox_data_slow_reg;
    reg mailbox_valid_slow_reg;
    (* ASYNC_REG = "TRUE" *)
    logic reset_release_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic reset_release_fast2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic reset_release_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic reset_release_slow2_reg;
    reg[8-1:0] fast_negedge_count_reg;
    reg[8-1:0] fifo_data_mem[4];
    reg[3-1:0] fifo_write_bin_reg;
    reg[3-1:0] fifo_write_gray_reg;
    (* ASYNC_REG = "TRUE" *)
    logic[3-1:0] fifo_read_gray_write1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic[3-1:0] fifo_read_gray_write2_reg;
    reg[3-1:0] fifo_read_bin_reg;
    reg[3-1:0] fifo_read_gray_reg;
    (* ASYNC_REG = "TRUE" *)
    logic[3-1:0] fifo_write_gray_read1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic[3-1:0] fifo_write_gray_read2_reg;
    logic mailbox_busy_fast_comb;
    logic fifo_write_ready_comb;
    logic fifo_read_valid_comb;
    logic[8-1:0] fifo_read_data_comb;

    // members

    // tmp variables
    logic[8-1:0] fast_count_reg_tmp;
    logic[8-1:0] fast_gray_reg_tmp;
    logic[8-1:0] slow_gray_fast1_reg_tmp;
    logic[8-1:0] slow_gray_fast2_reg_tmp;
    logic[8-1:0] slow_count_reg_tmp;
    logic[8-1:0] slow_gray_reg_tmp;
    logic[8-1:0] fast_gray_slow1_reg_tmp;
    logic[8-1:0] fast_gray_slow2_reg_tmp;
    logic level_fast_reg_tmp;
    logic level_slow1_reg_tmp;
    logic level_slow2_reg_tmp;
    logic pulse_toggle_fast_reg_tmp;
    logic pulse_toggle_slow1_reg_tmp;
    logic pulse_toggle_slow2_reg_tmp;
    logic pulse_toggle_slow_last_reg_tmp;
    logic pulse_slow_reg_tmp;
    logic[16-1:0] mailbox_data_fast_reg_tmp;
    logic mailbox_request_fast_reg_tmp;
    logic mailbox_ack_fast1_reg_tmp;
    logic mailbox_ack_fast2_reg_tmp;
    logic mailbox_request_slow1_reg_tmp;
    logic mailbox_request_slow2_reg_tmp;
    logic mailbox_ack_slow_reg_tmp;
    logic[16-1:0] mailbox_data_slow_reg_tmp;
    logic mailbox_valid_slow_reg_tmp;
    logic reset_release_fast1_reg_tmp;
    logic reset_release_fast2_reg_tmp;
    logic reset_release_slow1_reg_tmp;
    logic reset_release_slow2_reg_tmp;
    logic[8-1:0] fast_negedge_count_reg_tmp;
    logic[3-1:0] fifo_write_bin_reg_tmp;
    logic[3-1:0] fifo_write_gray_reg_tmp;
    logic[3-1:0] fifo_read_gray_write1_reg_tmp;
    logic[3-1:0] fifo_read_gray_write2_reg_tmp;
    logic[3-1:0] fifo_read_bin_reg_tmp;
    logic[3-1:0] fifo_read_gray_reg_tmp;
    logic[3-1:0] fifo_write_gray_read1_reg_tmp;
    logic[3-1:0] fifo_write_gray_read2_reg_tmp;


    always_comb begin : mailbox_busy_fast_comb_func  // mailbox_busy_fast_comb_func
        mailbox_busy_fast_comb=mailbox_request_fast_reg != mailbox_ack_fast2_reg;
    end

    always_comb begin : fifo_write_ready_comb_func  // fifo_write_ready_comb_func
        logic[3-1:0] full_gray;
        full_gray = fifo_read_gray_write2_reg ^ unsigned'(FIFO_PTR_BITS'(unsigned'(FIFO_PTR_BITS'(((('h1 <<< FIFO_ADDR_BITS)) | (('h1 <<< ((FIFO_ADDR_BITS - 'h1)))))))));
        fifo_write_ready_comb=fifo_write_gray_reg != full_gray;
    end

    always_comb begin : fifo_read_valid_comb_func  // fifo_read_valid_comb_func
        fifo_read_valid_comb=fifo_read_gray_reg != fifo_write_gray_read2_reg;
    end

    always_comb begin : fifo_read_data_comb_func  // fifo_read_data_comb_func
        fifo_read_data_comb = unsigned'(8'(fifo_data_mem[unsigned'(32'(fifo_read_bin_reg)) & unsigned'(32'(((FIFO_DEPTH - 'h1))))]));
    end

    task _work_fast_clk (input logic reset);
    begin: _work_fast_clk
        logic[8-1:0] next_count;
        logic[3-1:0] next_fifo_write;
        slow_gray_fast1_reg_tmp = slow_gray_reg;
        slow_gray_fast2_reg_tmp = slow_gray_fast1_reg;
        mailbox_ack_fast1_reg_tmp = mailbox_ack_slow_reg;
        mailbox_ack_fast2_reg_tmp = mailbox_ack_fast1_reg;
        fifo_read_gray_write1_reg_tmp = fifo_read_gray_reg;
        fifo_read_gray_write2_reg_tmp = fifo_read_gray_write1_reg;
        level_fast_reg_tmp = unsigned'(1'(level_fast_in));
        if (fast_enable_in) begin
            next_count = fast_count_reg + 'h1;
            fast_count_reg_tmp = next_count;
            fast_gray_reg_tmp = next_count ^ ((next_count >>> 'h1));
        end
        if (pulse_fast_in) begin
            pulse_toggle_fast_reg_tmp = unsigned'(1'(!pulse_toggle_fast_reg));
        end
        if (mailbox_send_fast_in && !mailbox_busy_fast_comb) begin
            mailbox_data_fast_reg_tmp = mailbox_data_fast_in;
            mailbox_request_fast_reg_tmp = unsigned'(1'(!mailbox_request_fast_reg));
        end
        if (!reset_release_in) begin
            reset_release_fast1_reg_tmp = '0;
            reset_release_fast2_reg_tmp = '0;
        end
        else begin
            reset_release_fast1_reg_tmp = unsigned'(1'h1);
            reset_release_fast2_reg_tmp = reset_release_fast1_reg;
        end
        if (fifo_write_valid_in && fifo_write_ready_comb) begin
            fifo_data_mem[unsigned'(32'(fifo_write_bin_reg)) & unsigned'(32'(((FIFO_DEPTH - 'h1))))] <= fifo_write_data_in;
            next_fifo_write = fifo_write_bin_reg + 'h1;
            fifo_write_bin_reg_tmp = next_fifo_write;
            fifo_write_gray_reg_tmp = next_fifo_write ^ ((next_fifo_write >>> 'h1));
        end
        if (reset) begin
            fast_count_reg_tmp = '0;
            fast_gray_reg_tmp = '0;
            slow_gray_fast1_reg_tmp = '0;
            slow_gray_fast2_reg_tmp = '0;
            level_fast_reg_tmp = '0;
            pulse_toggle_fast_reg_tmp = '0;
            mailbox_data_fast_reg_tmp = '0;
            mailbox_request_fast_reg_tmp = '0;
            mailbox_ack_fast1_reg_tmp = '0;
            mailbox_ack_fast2_reg_tmp = '0;
            reset_release_fast1_reg_tmp = '0;
            reset_release_fast2_reg_tmp = '0;
            fifo_write_bin_reg_tmp = '0;
            fifo_write_gray_reg_tmp = '0;
            fifo_read_gray_write1_reg_tmp = '0;
            fifo_read_gray_write2_reg_tmp = '0;
        end
    end
    endtask

    task _work_neg_fast_clk (input logic reset);
    begin: _work_neg_fast_clk
        fast_negedge_count_reg_tmp = fast_negedge_count_reg + 'h1;
        if (reset) begin
            fast_negedge_count_reg_tmp = '0;
        end
    end
    endtask

    task _work_slow_clk (input logic reset);
    begin: _work_slow_clk
        logic[8-1:0] next_count;
        logic[3-1:0] next_fifo_read;
        fast_gray_slow1_reg_tmp = fast_gray_reg;
        fast_gray_slow2_reg_tmp = fast_gray_slow1_reg;
        level_slow1_reg_tmp = level_fast_reg;
        level_slow2_reg_tmp = level_slow1_reg;
        pulse_toggle_slow1_reg_tmp = pulse_toggle_fast_reg;
        pulse_toggle_slow2_reg_tmp = pulse_toggle_slow1_reg;
        pulse_slow_reg_tmp = unsigned'(1'(pulse_toggle_slow2_reg != pulse_toggle_slow_last_reg));
        pulse_toggle_slow_last_reg_tmp = pulse_toggle_slow2_reg;
        mailbox_request_slow1_reg_tmp = mailbox_request_fast_reg;
        mailbox_request_slow2_reg_tmp = mailbox_request_slow1_reg;
        mailbox_valid_slow_reg_tmp = unsigned'(1'h0);
        fifo_write_gray_read1_reg_tmp = fifo_write_gray_reg;
        fifo_write_gray_read2_reg_tmp = fifo_write_gray_read1_reg;
        if (slow_enable_in) begin
            next_count = slow_count_reg + 'h1;
            slow_count_reg_tmp = next_count;
            slow_gray_reg_tmp = next_count ^ ((next_count >>> 'h1));
        end
        if (mailbox_request_slow2_reg != mailbox_ack_slow_reg) begin
            mailbox_data_slow_reg_tmp = mailbox_data_fast_reg;
            mailbox_valid_slow_reg_tmp = unsigned'(1'h1);
            mailbox_ack_slow_reg_tmp = mailbox_request_slow2_reg;
        end
        if (!reset_release_in) begin
            reset_release_slow1_reg_tmp = '0;
            reset_release_slow2_reg_tmp = '0;
        end
        else begin
            reset_release_slow1_reg_tmp = unsigned'(1'h1);
            reset_release_slow2_reg_tmp = reset_release_slow1_reg;
        end
        if (fifo_read_ready_in && fifo_read_valid_comb) begin
            next_fifo_read = fifo_read_bin_reg + 'h1;
            fifo_read_bin_reg_tmp = next_fifo_read;
            fifo_read_gray_reg_tmp = next_fifo_read ^ ((next_fifo_read >>> 'h1));
        end
        if (reset) begin
            slow_count_reg_tmp = '0;
            slow_gray_reg_tmp = '0;
            fast_gray_slow1_reg_tmp = '0;
            fast_gray_slow2_reg_tmp = '0;
            level_slow1_reg_tmp = '0;
            level_slow2_reg_tmp = '0;
            pulse_toggle_slow1_reg_tmp = '0;
            pulse_toggle_slow2_reg_tmp = '0;
            pulse_toggle_slow_last_reg_tmp = '0;
            pulse_slow_reg_tmp = '0;
            mailbox_request_slow1_reg_tmp = '0;
            mailbox_request_slow2_reg_tmp = '0;
            mailbox_ack_slow_reg_tmp = '0;
            mailbox_data_slow_reg_tmp = '0;
            mailbox_valid_slow_reg_tmp = '0;
            reset_release_slow1_reg_tmp = '0;
            reset_release_slow2_reg_tmp = '0;
            fifo_read_bin_reg_tmp = '0;
            fifo_read_gray_reg_tmp = '0;
            fifo_write_gray_read1_reg_tmp = '0;
            fifo_write_gray_read2_reg_tmp = '0;
        end
    end
    endtask

    task _work_neg_slow_clk (input logic unused);
    begin: _work_neg_slow_clk
    end
    endtask

    generate  // _assign
    endgenerate

    always_ff @(posedge fast_clk) begin
        fast_count_reg_tmp = fast_count_reg;
        fast_gray_reg_tmp = fast_gray_reg;
        slow_gray_fast1_reg_tmp = slow_gray_fast1_reg;
        slow_gray_fast2_reg_tmp = slow_gray_fast2_reg;
        level_fast_reg_tmp = level_fast_reg;
        pulse_toggle_fast_reg_tmp = pulse_toggle_fast_reg;
        mailbox_data_fast_reg_tmp = mailbox_data_fast_reg;
        mailbox_request_fast_reg_tmp = mailbox_request_fast_reg;
        mailbox_ack_fast1_reg_tmp = mailbox_ack_fast1_reg;
        mailbox_ack_fast2_reg_tmp = mailbox_ack_fast2_reg;
        reset_release_fast1_reg_tmp = reset_release_fast1_reg;
        reset_release_fast2_reg_tmp = reset_release_fast2_reg;
        fifo_write_bin_reg_tmp = fifo_write_bin_reg;
        fifo_write_gray_reg_tmp = fifo_write_gray_reg;
        fifo_read_gray_write1_reg_tmp = fifo_read_gray_write1_reg;
        fifo_read_gray_write2_reg_tmp = fifo_read_gray_write2_reg;

        _work_fast_clk(reset);

        fast_count_reg <= fast_count_reg_tmp;
        fast_gray_reg <= fast_gray_reg_tmp;
        slow_gray_fast1_reg <= slow_gray_fast1_reg_tmp;
        slow_gray_fast2_reg <= slow_gray_fast2_reg_tmp;
        level_fast_reg <= level_fast_reg_tmp;
        pulse_toggle_fast_reg <= pulse_toggle_fast_reg_tmp;
        mailbox_data_fast_reg <= mailbox_data_fast_reg_tmp;
        mailbox_request_fast_reg <= mailbox_request_fast_reg_tmp;
        mailbox_ack_fast1_reg <= mailbox_ack_fast1_reg_tmp;
        mailbox_ack_fast2_reg <= mailbox_ack_fast2_reg_tmp;
        reset_release_fast1_reg <= reset_release_fast1_reg_tmp;
        reset_release_fast2_reg <= reset_release_fast2_reg_tmp;
        fifo_write_bin_reg <= fifo_write_bin_reg_tmp;
        fifo_write_gray_reg <= fifo_write_gray_reg_tmp;
        fifo_read_gray_write1_reg <= fifo_read_gray_write1_reg_tmp;
        fifo_read_gray_write2_reg <= fifo_read_gray_write2_reg_tmp;
    end

    always_ff @(negedge fast_clk) begin
        fast_negedge_count_reg_tmp = fast_negedge_count_reg;

        _work_neg_fast_clk(reset);

        fast_negedge_count_reg <= fast_negedge_count_reg_tmp;
    end

    always_ff @(posedge slow_clk) begin
        slow_count_reg_tmp = slow_count_reg;
        slow_gray_reg_tmp = slow_gray_reg;
        fast_gray_slow1_reg_tmp = fast_gray_slow1_reg;
        fast_gray_slow2_reg_tmp = fast_gray_slow2_reg;
        level_slow1_reg_tmp = level_slow1_reg;
        level_slow2_reg_tmp = level_slow2_reg;
        pulse_toggle_slow1_reg_tmp = pulse_toggle_slow1_reg;
        pulse_toggle_slow2_reg_tmp = pulse_toggle_slow2_reg;
        pulse_toggle_slow_last_reg_tmp = pulse_toggle_slow_last_reg;
        pulse_slow_reg_tmp = pulse_slow_reg;
        mailbox_request_slow1_reg_tmp = mailbox_request_slow1_reg;
        mailbox_request_slow2_reg_tmp = mailbox_request_slow2_reg;
        mailbox_ack_slow_reg_tmp = mailbox_ack_slow_reg;
        mailbox_data_slow_reg_tmp = mailbox_data_slow_reg;
        mailbox_valid_slow_reg_tmp = mailbox_valid_slow_reg;
        reset_release_slow1_reg_tmp = reset_release_slow1_reg;
        reset_release_slow2_reg_tmp = reset_release_slow2_reg;
        fifo_read_bin_reg_tmp = fifo_read_bin_reg;
        fifo_read_gray_reg_tmp = fifo_read_gray_reg;
        fifo_write_gray_read1_reg_tmp = fifo_write_gray_read1_reg;
        fifo_write_gray_read2_reg_tmp = fifo_write_gray_read2_reg;

        _work_slow_clk(reset);

        slow_count_reg <= slow_count_reg_tmp;
        slow_gray_reg <= slow_gray_reg_tmp;
        fast_gray_slow1_reg <= fast_gray_slow1_reg_tmp;
        fast_gray_slow2_reg <= fast_gray_slow2_reg_tmp;
        level_slow1_reg <= level_slow1_reg_tmp;
        level_slow2_reg <= level_slow2_reg_tmp;
        pulse_toggle_slow1_reg <= pulse_toggle_slow1_reg_tmp;
        pulse_toggle_slow2_reg <= pulse_toggle_slow2_reg_tmp;
        pulse_toggle_slow_last_reg <= pulse_toggle_slow_last_reg_tmp;
        pulse_slow_reg <= pulse_slow_reg_tmp;
        mailbox_request_slow1_reg <= mailbox_request_slow1_reg_tmp;
        mailbox_request_slow2_reg <= mailbox_request_slow2_reg_tmp;
        mailbox_ack_slow_reg <= mailbox_ack_slow_reg_tmp;
        mailbox_data_slow_reg <= mailbox_data_slow_reg_tmp;
        mailbox_valid_slow_reg <= mailbox_valid_slow_reg_tmp;
        reset_release_slow1_reg <= reset_release_slow1_reg_tmp;
        reset_release_slow2_reg <= reset_release_slow2_reg_tmp;
        fifo_read_bin_reg <= fifo_read_bin_reg_tmp;
        fifo_read_gray_reg <= fifo_read_gray_reg_tmp;
        fifo_write_gray_read1_reg <= fifo_write_gray_read1_reg_tmp;
        fifo_write_gray_read2_reg <= fifo_write_gray_read2_reg_tmp;
    end

    always_ff @(negedge slow_clk) begin

        _work_neg_slow_clk(reset);

    end

    assign fast_count_out = fast_count_reg;

    assign slow_count_out = slow_count_reg;

    assign slow_gray_fast_out = slow_gray_fast2_reg;

    assign fast_gray_slow_out = fast_gray_slow2_reg;

    assign level_slow_out = level_slow2_reg;

    assign pulse_slow_out = pulse_slow_reg;

    assign mailbox_busy_fast_out = mailbox_busy_fast_comb;

    assign mailbox_valid_slow_out = mailbox_valid_slow_reg;

    assign mailbox_data_slow_out = mailbox_data_slow_reg;

    assign reset_released_fast_out = reset_release_fast2_reg;

    assign reset_released_slow_out = reset_release_slow2_reg;

    assign fast_negedge_count_out = fast_negedge_count_reg;

    assign fifo_write_ready_out = fifo_write_ready_comb;

    assign fifo_read_valid_out = fifo_read_valid_comb;

    assign fifo_read_data_out = fifo_read_data_comb;


endmodule

`default_nettype none

import Predef_pkg::*;


module L1MemFastToSlowCdc #(
    parameter PORT_BITWIDTH = 256
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire fast_in__read_in
,   input wire fast_in__write_in
,   input wire[31:0] fast_in__addr_in
,   input wire[31:0] fast_in__write_data_in
,   input wire[7:0] fast_in__write_mask_in
,   input wire fast_in__cache_disable_in
,   output wire[PORT_BITWIDTH-1:0] fast_in__read_data_out
,   output wire fast_in__wait_out
,   output wire slow_out__read_out
,   output wire slow_out__write_out
,   output wire[31:0] slow_out__addr_out
,   output wire[31:0] slow_out__write_data_out
,   output wire[7:0] slow_out__write_mask_out
,   output wire slow_out__cache_disable_out
,   input wire[PORT_BITWIDTH-1:0] slow_out__read_data_in
,   input wire slow_out__wait_in
);


    // regs and combs
    reg read_fast_reg;
    reg write_fast_reg;
    reg[32-1:0] addr_fast_reg;
    reg[32-1:0] write_data_fast_reg;
    reg[8-1:0] write_mask_fast_reg;
    reg cache_disable_fast_reg;
    reg request_fast_reg;
    reg request_active_fast_reg;
    reg request_orphaned_fast_reg;
    (* ASYNC_REG = "TRUE" *)
    logic response_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic response_fast2_reg;
    reg response_ack_fast_reg;
    reg[PORT_BITWIDTH-1:0] read_data_fast_reg;
    (* ASYNC_REG = "TRUE" *)
    logic request_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic request_slow2_reg;
    reg request_seen_slow_reg;
    reg request_active_slow_reg;
    reg read_slow_reg;
    reg write_slow_reg;
    reg[32-1:0] addr_slow_reg;
    reg[32-1:0] write_data_slow_reg;
    reg[8-1:0] write_mask_slow_reg;
    reg cache_disable_slow_reg;
    reg[PORT_BITWIDTH-1:0] read_data_slow_reg;
    reg response_slow_reg;

    // members

    // tmp variables
    logic read_fast_reg_tmp;
    logic write_fast_reg_tmp;
    logic[32-1:0] addr_fast_reg_tmp;
    logic[32-1:0] write_data_fast_reg_tmp;
    logic[8-1:0] write_mask_fast_reg_tmp;
    logic cache_disable_fast_reg_tmp;
    logic request_fast_reg_tmp;
    logic request_active_fast_reg_tmp;
    logic request_orphaned_fast_reg_tmp;
    logic response_fast1_reg_tmp;
    logic response_fast2_reg_tmp;
    logic response_ack_fast_reg_tmp;
    logic[PORT_BITWIDTH-1:0] read_data_fast_reg_tmp;
    logic request_slow1_reg_tmp;
    logic request_slow2_reg_tmp;
    logic request_seen_slow_reg_tmp;
    logic request_active_slow_reg_tmp;
    logic read_slow_reg_tmp;
    logic write_slow_reg_tmp;
    logic[32-1:0] addr_slow_reg_tmp;
    logic[32-1:0] write_data_slow_reg_tmp;
    logic[8-1:0] write_mask_slow_reg_tmp;
    logic cache_disable_slow_reg_tmp;
    logic[PORT_BITWIDTH-1:0] read_data_slow_reg_tmp;
    logic response_slow_reg_tmp;


    generate  // _assign
        assign fast_in__read_data_out = read_data_fast_reg;
        assign fast_in__wait_out = ((((fast_in__read_in || fast_in__write_in)) && !(((((request_active_fast_reg) != '0) && !((request_orphaned_fast_reg) != '0)) && (response_fast2_reg != response_ack_fast_reg)))) != '0);
        assign slow_out__read_out = (((((request_active_slow_reg) != '0) && ((read_slow_reg) != '0))) != '0);
        assign slow_out__write_out = (((((request_active_slow_reg) != '0) && ((write_slow_reg) != '0))) != '0);
        assign slow_out__addr_out = addr_slow_reg;
        assign slow_out__write_data_out = write_data_slow_reg;
        assign slow_out__write_mask_out = write_mask_slow_reg;
        assign slow_out__cache_disable_out = cache_disable_slow_reg;
    endgenerate

    task work_clk_func (input logic reset);
    begin: work_clk_func
        logic request;
        request=fast_in__read_in || fast_in__write_in;
        response_fast1_reg_tmp = response_slow_reg;
        response_fast2_reg_tmp = response_fast1_reg;
        if (response_fast1_reg != response_fast2_reg) begin
            read_data_fast_reg_tmp = read_data_slow_reg;
        end
        if (((request_active_fast_reg) != '0) && (response_fast2_reg != response_ack_fast_reg)) begin
            response_ack_fast_reg_tmp = response_fast2_reg;
            request_active_fast_reg_tmp = 0;
            request_orphaned_fast_reg_tmp = 0;
        end
        else begin
            if (!((request_active_fast_reg) != '0) && request) begin
                read_fast_reg_tmp = fast_in__read_in;
                write_fast_reg_tmp = fast_in__write_in;
                addr_fast_reg_tmp = fast_in__addr_in;
                write_data_fast_reg_tmp = fast_in__write_data_in;
                write_mask_fast_reg_tmp = fast_in__write_mask_in;
                cache_disable_fast_reg_tmp = fast_in__cache_disable_in;
                request_fast_reg_tmp = !((request_fast_reg) != '0);
                request_active_fast_reg_tmp = 1;
                request_orphaned_fast_reg_tmp = 0;
            end
            else begin
                if (((request_active_fast_reg) != '0) && !request) begin
                    request_orphaned_fast_reg_tmp = 1;
                end
            end
        end
        if (reset) begin
            read_fast_reg_tmp = '0;
            write_fast_reg_tmp = '0;
            addr_fast_reg_tmp = '0;
            write_data_fast_reg_tmp = '0;
            write_mask_fast_reg_tmp = '0;
            cache_disable_fast_reg_tmp = '0;
            request_fast_reg_tmp = '0;
            request_active_fast_reg_tmp = '0;
            request_orphaned_fast_reg_tmp = '0;
            response_fast1_reg_tmp = '0;
            response_fast2_reg_tmp = '0;
            response_ack_fast_reg_tmp = '0;
            read_data_fast_reg_tmp = '0;
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        work_clk_func(reset);
    end
    endtask

    task _work_clk (input logic reset);
    begin: _work_clk
        work_clk_func(reset);
    end
    endtask

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
        request_slow1_reg_tmp = request_fast_reg;
        request_slow2_reg_tmp = request_slow1_reg;
        if (!((request_active_slow_reg) != '0) && (request_slow2_reg != request_seen_slow_reg)) begin
            request_seen_slow_reg_tmp = request_slow2_reg;
            read_slow_reg_tmp = read_fast_reg;
            write_slow_reg_tmp = write_fast_reg;
            addr_slow_reg_tmp = addr_fast_reg;
            write_data_slow_reg_tmp = write_data_fast_reg;
            write_mask_slow_reg_tmp = write_mask_fast_reg;
            cache_disable_slow_reg_tmp = cache_disable_fast_reg;
            request_active_slow_reg_tmp = 1;
        end
        else begin
            if (((request_active_slow_reg) != '0) && !slow_out__wait_in) begin
                read_data_slow_reg_tmp = slow_out__read_data_in;
                response_slow_reg_tmp = !((response_slow_reg) != '0);
                request_active_slow_reg_tmp = 0;
            end
        end
        if (reset) begin
            request_slow1_reg_tmp = '0;
            request_slow2_reg_tmp = '0;
            request_seen_slow_reg_tmp = '0;
            request_active_slow_reg_tmp = '0;
            read_slow_reg_tmp = '0;
            write_slow_reg_tmp = '0;
            addr_slow_reg_tmp = '0;
            write_data_slow_reg_tmp = '0;
            write_mask_slow_reg_tmp = '0;
            cache_disable_slow_reg_tmp = '0;
            read_data_slow_reg_tmp = '0;
            response_slow_reg_tmp = '0;
        end
    end
    endtask

    always_ff @(posedge clk) begin
        read_fast_reg_tmp = read_fast_reg;
        write_fast_reg_tmp = write_fast_reg;
        addr_fast_reg_tmp = addr_fast_reg;
        write_data_fast_reg_tmp = write_data_fast_reg;
        write_mask_fast_reg_tmp = write_mask_fast_reg;
        cache_disable_fast_reg_tmp = cache_disable_fast_reg;
        request_fast_reg_tmp = request_fast_reg;
        request_active_fast_reg_tmp = request_active_fast_reg;
        request_orphaned_fast_reg_tmp = request_orphaned_fast_reg;
        response_fast1_reg_tmp = response_fast1_reg;
        response_fast2_reg_tmp = response_fast2_reg;
        response_ack_fast_reg_tmp = response_ack_fast_reg;
        read_data_fast_reg_tmp = read_data_fast_reg;

            _work_clk(reset);

        read_fast_reg <= read_fast_reg_tmp;
        write_fast_reg <= write_fast_reg_tmp;
        addr_fast_reg <= addr_fast_reg_tmp;
        write_data_fast_reg <= write_data_fast_reg_tmp;
        write_mask_fast_reg <= write_mask_fast_reg_tmp;
        cache_disable_fast_reg <= cache_disable_fast_reg_tmp;
        request_fast_reg <= request_fast_reg_tmp;
        request_active_fast_reg <= request_active_fast_reg_tmp;
        request_orphaned_fast_reg <= request_orphaned_fast_reg_tmp;
        response_fast1_reg <= response_fast1_reg_tmp;
        response_fast2_reg <= response_fast2_reg_tmp;
        response_ack_fast_reg <= response_ack_fast_reg_tmp;
        read_data_fast_reg <= read_data_fast_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin
        request_slow1_reg_tmp = request_slow1_reg;
        request_slow2_reg_tmp = request_slow2_reg;
        request_seen_slow_reg_tmp = request_seen_slow_reg;
        request_active_slow_reg_tmp = request_active_slow_reg;
        read_slow_reg_tmp = read_slow_reg;
        write_slow_reg_tmp = write_slow_reg;
        addr_slow_reg_tmp = addr_slow_reg;
        write_data_slow_reg_tmp = write_data_slow_reg;
        write_mask_slow_reg_tmp = write_mask_slow_reg;
        cache_disable_slow_reg_tmp = cache_disable_slow_reg;
        read_data_slow_reg_tmp = read_data_slow_reg;
        response_slow_reg_tmp = response_slow_reg;

            _work_l2_clock(reset);

        request_slow1_reg <= request_slow1_reg_tmp;
        request_slow2_reg <= request_slow2_reg_tmp;
        request_seen_slow_reg <= request_seen_slow_reg_tmp;
        request_active_slow_reg <= request_active_slow_reg_tmp;
        read_slow_reg <= read_slow_reg_tmp;
        write_slow_reg <= write_slow_reg_tmp;
        addr_slow_reg <= addr_slow_reg_tmp;
        write_data_slow_reg <= write_data_slow_reg_tmp;
        write_mask_slow_reg <= write_mask_slow_reg_tmp;
        cache_disable_slow_reg <= cache_disable_slow_reg_tmp;
        read_data_slow_reg <= read_data_slow_reg_tmp;
        response_slow_reg <= response_slow_reg_tmp;
    end


endmodule

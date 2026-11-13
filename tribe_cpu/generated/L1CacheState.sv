`default_nettype none

import Predef_pkg::*;
import L1CachePerf_pkg::*;
import L1RequestState_pkg::*;
import L1RefillState_pkg::*;
import L1HeldResponse_pkg::*;
import L1LookupState_pkg::*;
import L1SelectedLineState_pkg::*;


module L1CacheState #(
    parameter TOTAL_CACHE_SIZE = 'h400
,   parameter CACHE_LINE_SIZE = 'h20
,   parameter WAYS = 'h2
,   parameter DCACHE = 'h0
,   parameter ADDR_BITS = 'h20
,   parameter PORT_BITWIDTH = 'h20
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire write_in
,   input wire[31:0] write_data_in
,   input wire[7:0] write_mask_in
,   input wire read_in
,   input wire[31:0] addr_in
,   output wire[31:0] read_data_out
,   output wire[31:0] read_addr_out
,   output wire read_valid_out
,   output wire busy_out
,   input wire stall_in
,   input wire flush_in
,   input wire invalidate_in
,   input wire invalidate_line_in
,   input wire[31:0] invalidate_addr_in
,   input wire cache_disable_in
,   output wire mem_out__read_out
,   output wire mem_out__write_out
,   output wire[31:0] mem_out__addr_out
,   output wire[31:0] mem_out__write_data_out
,   output wire[7:0] mem_out__write_mask_out
,   output wire mem_out__cache_disable_out
,   input wire[PORT_BITWIDTH-1:0] mem_out__read_data_in
,   input wire mem_out__wait_in
,   output wire L1CachePerf perf_out
,   input wire debugen_in
);
    localparam  LINE_WORDS = unsigned'(64'(CACHE_LINE_SIZE))/64'h4;
    localparam  SETS = (unsigned'(64'(TOTAL_CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS));
    localparam  SET_BITS = unsigned'(64'($clog2(unsigned'(32'(SETS)))));
    localparam  LINE_BITS = unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))));
    localparam  HALF_LINE_BITS = CACHE_LINE_SIZE*64'h4;
    localparam  PORT_BYTES = unsigned'(64'(PORT_BITWIDTH))/64'h8;
    localparam  PORT_WORDS = unsigned'(64'(PORT_BITWIDTH))/64'h20;
    localparam  REFILL_BEATS = unsigned'(64'(CACHE_LINE_SIZE))/unsigned'(64'(PORT_BYTES));
    localparam  TAG_BITS = (ADDR_BITS - SET_BITS) - LINE_BITS;
    localparam  WAY_BITS = 64'(unsigned'(32'((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS)))))));
    localparam  WORD_BITS = unsigned'(64'($clog2(unsigned'(32'(LINE_WORDS)))));
    localparam  REFILL_BEAT_BITS = 64'(unsigned'(32'((REFILL_BEATS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(REFILL_BEATS)))))));


    // regs and combs
    reg[3-1:0] state_reg;
    L1RequestState req_reg;
    reg tag_epoch_reg;
    reg epoch_wrap_pending_reg;
    reg[SETS-1:0][8-1:0] tag_set_epoch_reg;
    L1RefillState refill_reg;
    reg[WAY_BITS-1:0] victim_reg;
    reg[SET_BITS-1:0] init_set_reg;
    L1HeldResponse response_reg;
    L1LookupState lookup_reg;
    L1SelectedLineState selected_line_reg;
    reg[WAYS-1:0][TAG_BITS + 64'hA-1:0] tag_entries_reg;

    // members
    genvar __i;
    wire[unsigned'(64'($clog2(unsigned'(32'(SETS)))))-1:0] even_ram__addr_in[WAYS];
    wire[HALF_LINE_BITS-1:0] even_ram__data_in[WAYS];
    wire even_ram__wr_in[WAYS];
    wire even_ram__rd_in[WAYS];
    wire[HALF_LINE_BITS-1:0] even_ram__q_out[WAYS];
    wire signed[31:0] even_ram__id_in[WAYS];
    generate
    for (__i=0; __i < WAYS; __i = __i + 1) begin
        RAM #(
        HALF_LINE_BITS
,       SETS
        ) even_ram (
            .clk(clk)
        ,           .l2_clock(l2_clock)
        ,           .reset(reset)
        ,           .addr_in(even_ram__addr_in[__i])
        ,           .data_in(even_ram__data_in[__i])
        ,           .wr_in(even_ram__wr_in[__i])
        ,           .rd_in(even_ram__rd_in[__i])
        ,           .q_out(even_ram__q_out[__i])
        ,           .id_in(even_ram__id_in[__i])
        );
    end
    endgenerate
    wire[unsigned'(64'($clog2(unsigned'(32'(SETS)))))-1:0] odd_ram__addr_in[WAYS];
    wire[HALF_LINE_BITS-1:0] odd_ram__data_in[WAYS];
    wire odd_ram__wr_in[WAYS];
    wire odd_ram__rd_in[WAYS];
    wire[HALF_LINE_BITS-1:0] odd_ram__q_out[WAYS];
    wire signed[31:0] odd_ram__id_in[WAYS];
    generate
    for (__i=0; __i < WAYS; __i = __i + 1) begin
        RAM #(
        HALF_LINE_BITS
,       SETS
        ) odd_ram (
            .clk(clk)
        ,           .l2_clock(l2_clock)
        ,           .reset(reset)
        ,           .addr_in(odd_ram__addr_in[__i])
        ,           .data_in(odd_ram__data_in[__i])
        ,           .wr_in(odd_ram__wr_in[__i])
        ,           .rd_in(odd_ram__rd_in[__i])
        ,           .q_out(odd_ram__q_out[__i])
        ,           .id_in(odd_ram__id_in[__i])
        );
    end
    endgenerate
    wire[unsigned'(64'($clog2(unsigned'(32'(SETS)))))-1:0] tag_ram__addr_in[WAYS];
    wire[TAG_BITS + 64'hA-1:0] tag_ram__data_in[WAYS];
    wire tag_ram__wr_in[WAYS];
    wire tag_ram__rd_in[WAYS];
    wire[TAG_BITS + 64'hA-1:0] tag_ram__q_out[WAYS];
    wire signed[31:0] tag_ram__id_in[WAYS];
    generate
    for (__i=0; __i < WAYS; __i = __i + 1) begin
        RAM #(
        TAG_BITS + 64'hA
,       SETS
        ) tag_ram (
            .clk(clk)
        ,           .l2_clock(l2_clock)
        ,           .reset(reset)
        ,           .addr_in(tag_ram__addr_in[__i])
        ,           .data_in(tag_ram__data_in[__i])
        ,           .wr_in(tag_ram__wr_in[__i])
        ,           .rd_in(tag_ram__rd_in[__i])
        ,           .q_out(tag_ram__q_out[__i])
        ,           .id_in(tag_ram__id_in[__i])
        );
    end
    endgenerate

    // tmp variables
    logic[3-1:0] state_reg_tmp;
    L1RequestState req_reg_tmp;
    logic tag_epoch_reg_tmp;
    logic epoch_wrap_pending_reg_tmp;
    logic[SETS-1:0][8-1:0] tag_set_epoch_reg_tmp;
    L1RefillState refill_reg_tmp;
    logic[WAY_BITS-1:0] victim_reg_tmp;
    logic[SET_BITS-1:0] init_set_reg_tmp;
    L1HeldResponse response_reg_tmp;
    L1LookupState lookup_reg_tmp;
    L1SelectedLineState selected_line_reg_tmp;
    logic[WAYS-1:0][TAG_BITS + 64'hA-1:0] tag_entries_reg_tmp;


    task _work_clk (input logic reset);
    begin: _work_clk
    end
    endtask

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin

            _work_clk(reset);

    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end


endmodule

`default_nettype none

import Predef_pkg::*;
import L2HitLookupComb_pkg::*;
import L2EvictCandidateComb_pkg::*;
import L2WordPairComb_pkg::*;
import CacheRequest_pkg::*;
import L2ActiveRequestComb_pkg::*;
import Axi4WriteResponse4_pkg::*;
import Axi4ReadData4_256_pkg::*;
import CacheResponse_pkg::*;
import Axi4WriteAddress32_4_pkg::*;
import Axi4ReadAddress32_4_pkg::*;


module L2CacheState #(
    parameter CACHE_SIZE = 'h4000
,   parameter PORT_BITWIDTH = 'h100
,   parameter CACHE_LINE_SIZE = 'h20
,   parameter WAYS = 'h4
,   parameter ADDR_BITS = 'h20
,   parameter MEM_ADDR_BITS = ADDR_BITS
,   parameter MEM_PORTS = 'h1
,   parameter CPU_PORTS = 'h1
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire i_mem_in__read_in[CPU_PORTS]
,   input wire i_mem_in__write_in[CPU_PORTS]
,   input wire[31:0] i_mem_in__addr_in[CPU_PORTS]
,   input wire[31:0] i_mem_in__write_data_in[CPU_PORTS]
,   input wire[7:0] i_mem_in__write_mask_in[CPU_PORTS]
,   input wire i_mem_in__cache_disable_in[CPU_PORTS]
,   output wire[PORT_BITWIDTH-1:0] i_mem_in__read_data_out[CPU_PORTS]
,   output wire i_mem_in__wait_out[CPU_PORTS]
,   input wire d_mem_in__read_in[CPU_PORTS]
,   input wire d_mem_in__write_in[CPU_PORTS]
,   input wire[31:0] d_mem_in__addr_in[CPU_PORTS]
,   input wire[31:0] d_mem_in__write_data_in[CPU_PORTS]
,   input wire[7:0] d_mem_in__write_mask_in[CPU_PORTS]
,   input wire d_mem_in__cache_disable_in[CPU_PORTS]
,   output wire[PORT_BITWIDTH-1:0] d_mem_in__read_data_out[CPU_PORTS]
,   output wire d_mem_in__wait_out[CPU_PORTS]
,   input wire[31:0] memory_base_in
,   input wire[31:0] memory_size_in
,   input wire[31:0] mem_region_size_in[MEM_PORTS]
,   input wire mem_region_uncached_in[MEM_PORTS]
,   input wire axi_in__awvalid_in[MEM_PORTS]
,   output wire axi_in__awready_out[MEM_PORTS]
,   input wire[ADDR_BITS-1:0] axi_in__awaddr_in[MEM_PORTS]
,   input wire[4-1:0] axi_in__awid_in[MEM_PORTS]
,   input wire axi_in__wvalid_in[MEM_PORTS]
,   output wire axi_in__wready_out[MEM_PORTS]
,   input wire[PORT_BITWIDTH-1:0] axi_in__wdata_in[MEM_PORTS]
,   input wire[unsigned'(64'(PORT_BITWIDTH))/64'h8-1:0] axi_in__wstrb_in[MEM_PORTS]
,   input wire axi_in__wlast_in[MEM_PORTS]
,   output wire axi_in__bvalid_out[MEM_PORTS]
,   input wire axi_in__bready_in[MEM_PORTS]
,   output wire[4-1:0] axi_in__bid_out[MEM_PORTS]
,   input wire axi_in__arvalid_in[MEM_PORTS]
,   output wire axi_in__arready_out[MEM_PORTS]
,   input wire[ADDR_BITS-1:0] axi_in__araddr_in[MEM_PORTS]
,   input wire[4-1:0] axi_in__arid_in[MEM_PORTS]
,   output wire axi_in__rvalid_out[MEM_PORTS]
,   input wire axi_in__rready_in[MEM_PORTS]
,   output wire[PORT_BITWIDTH-1:0] axi_in__rdata_out[MEM_PORTS]
,   output wire axi_in__rlast_out[MEM_PORTS]
,   output wire[4-1:0] axi_in__rid_out[MEM_PORTS]
,   output wire axi_out__awvalid_out[MEM_PORTS]
,   input wire axi_out__awready_in[MEM_PORTS]
,   output wire[MEM_ADDR_BITS-1:0] axi_out__awaddr_out[MEM_PORTS]
,   output wire[4-1:0] axi_out__awid_out[MEM_PORTS]
,   output wire axi_out__wvalid_out[MEM_PORTS]
,   input wire axi_out__wready_in[MEM_PORTS]
,   output wire[PORT_BITWIDTH-1:0] axi_out__wdata_out[MEM_PORTS]
,   output wire[unsigned'(64'(PORT_BITWIDTH))/64'h8-1:0] axi_out__wstrb_out[MEM_PORTS]
,   output wire axi_out__wlast_out[MEM_PORTS]
,   input wire axi_out__bvalid_in[MEM_PORTS]
,   output wire axi_out__bready_out[MEM_PORTS]
,   input wire[4-1:0] axi_out__bid_in[MEM_PORTS]
,   output wire axi_out__arvalid_out[MEM_PORTS]
,   input wire axi_out__arready_in[MEM_PORTS]
,   output wire[MEM_ADDR_BITS-1:0] axi_out__araddr_out[MEM_PORTS]
,   output wire[4-1:0] axi_out__arid_out[MEM_PORTS]
,   input wire axi_out__rvalid_in[MEM_PORTS]
,   output wire axi_out__rready_out[MEM_PORTS]
,   input wire[PORT_BITWIDTH-1:0] axi_out__rdata_in[MEM_PORTS]
,   input wire axi_out__rlast_in[MEM_PORTS]
,   input wire[4-1:0] axi_out__rid_in[MEM_PORTS]
,   input wire dma_line_valid_in
,   input wire[ADDR_BITS-1:0] dma_line_addr_in
,   input wire[CACHE_LINE_SIZE*64'h8-1:0] dma_line_data_in
,   input wire[CACHE_LINE_SIZE-1:0] dma_line_keep_in
,   output wire dma_line_ready_out
,   input wire debugen_in
);
    localparam  LINE_WORDS = unsigned'(64'(CACHE_LINE_SIZE))/64'h4;
    localparam  PORT_BYTES = unsigned'(64'(PORT_BITWIDTH))/64'h8;
    localparam  PORT_WORDS = unsigned'(64'(PORT_BITWIDTH))/64'h20;
    localparam  LINE_BEATS = unsigned'(64'(CACHE_LINE_SIZE))/unsigned'(64'(PORT_BYTES));
    localparam  SETS = (unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS));
    localparam  SET_BITS = unsigned'(64'($clog2(unsigned'(32'(SETS)))));
    localparam  LINE_BITS = unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))));
    localparam  TAG_BITS = (ADDR_BITS - SET_BITS) - LINE_BITS;
    localparam  DATA_BANKS = WAYS*LINE_WORDS;
    localparam  CPU_RESPONSE_BASE = 'h8;
    localparam  RESPONSE_SLOTS = 'h10;
    localparam  MEM_ADDR_MASK64 = unsigned'(64'(((MEM_ADDR_BITS>=64'h40)) ? (~64'h0) : ((((64'h1 <<< MEM_ADDR_BITS)) - 64'h1))));
    localparam  LINE_BEAT_BITS = 64'(unsigned'(32'((LINE_BEATS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(LINE_BEATS)))))));
    localparam  WORD_BITS = unsigned'(64'($clog2(unsigned'(32'(LINE_WORDS)))));
    localparam  WAY_BITS = 64'(unsigned'(32'((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS)))))));
    localparam  TAG_RAM_BITS = (((((TAG_BITS + 64'h2) + 64'h7))/64'h8))*64'h8;
    localparam  MEM_PORT_BITS = unsigned'(64'($clog2(unsigned'(32'(MEM_PORTS)))));


    // regs and combs
    reg[DATA_BANKS-1:0][32-1:0] lookup_data_reg;
    reg[WAYS-1:0][(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] lookup_tag_reg;
    L2HitLookupComb lookup_hit_reg;
    L2EvictCandidateComb lookup_evict_reg;
    L2WordPairComb lookup_write_pair_reg;
    reg[5-1:0] state_reg;
    CacheRequest req_reg;
    L2ActiveRequestComb request_pipe_reg;
    reg request_pipe_valid_reg;
    reg[3-1:0] cpu_rr_reg;
    reg[64'(unsigned'(32'(((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS))))))))-1:0] victim_reg;
    reg[64'(unsigned'(32'(((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS))))))))-1:0] fill_way_reg;
    reg[unsigned'(64'($clog2(unsigned'(32'((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))))))-1:0] init_set_reg;
    CacheResponse[16-1:0] response_reg;
    reg[PORT_BITWIDTH-1:0] cross_low_reg;
    reg[PORT_BITWIDTH-1:0] cross_high_reg;
    reg[PORT_BITWIDTH-1:0] refill_data_reg;
    reg[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] fill_beat_reg;
    reg[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] evict_beat_reg;
    reg[(ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))-1:0] evict_tag_reg;
    reg[CACHE_LINE_SIZE*64'h8-1:0] evict_line_reg;
    Axi4WriteAddress32_4[8-1:0] slave_aw_reg;
    Axi4WriteAddress32_4[8-1:0] slave_aw_seen_reg;
    Axi4ReadAddress32_4[8-1:0] slave_ar_seen_reg;
    reg[8-1:0] slave_aw_novelty_reg;
    reg[8-1:0] slave_ar_novelty_reg;

    // members
    genvar __i;
    wire[unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))-1:0] data_ram__addr_in[32];
    wire data_ram__write_in[32];
    wire data_ram__read_in[32];
    wire[64'h20-1:0] data_ram__write_data_in[32];
    wire[64'h20-1:0] data_ram__read_data_out[32];
    generate
    for (__i=0; __i < 32; __i = __i + 1) begin
        L2CacheRamBank #(
        64'h20
,       ((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))
        ) data_ram (
            .clk(clk)
        ,           .l2_clock(l2_clock)
        ,           .reset(reset)
        ,           .addr_in(data_ram__addr_in[__i])
        ,           .write_in(data_ram__write_in[__i])
        ,           .read_in(data_ram__read_in[__i])
        ,           .write_data_in(data_ram__write_data_in[__i])
        ,           .read_data_out(data_ram__read_data_out[__i])
        );
    end
    endgenerate
    wire[unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))-1:0] tag_ram__addr_in[4];
    wire tag_ram__write_in[4];
    wire tag_ram__read_in[4];
    wire[(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] tag_ram__write_data_in[4];
    wire[(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] tag_ram__read_data_out[4];
    generate
    for (__i=0; __i < 4; __i = __i + 1) begin
        L2CacheRamBank #(
        (((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8
,       ((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))
        ) tag_ram (
            .clk(clk)
        ,           .l2_clock(l2_clock)
        ,           .reset(reset)
        ,           .addr_in(tag_ram__addr_in[__i])
        ,           .write_in(tag_ram__write_in[__i])
        ,           .read_in(tag_ram__read_in[__i])
        ,           .write_data_in(tag_ram__write_data_in[__i])
        ,           .read_data_out(tag_ram__read_data_out[__i])
        );
    end
    endgenerate

    // tmp variables
    logic[DATA_BANKS-1:0][32-1:0] lookup_data_reg_tmp;
    logic[WAYS-1:0][(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] lookup_tag_reg_tmp;
    L2HitLookupComb lookup_hit_reg_tmp;
    L2EvictCandidateComb lookup_evict_reg_tmp;
    L2WordPairComb lookup_write_pair_reg_tmp;
    logic[5-1:0] state_reg_tmp;
    CacheRequest req_reg_tmp;
    L2ActiveRequestComb request_pipe_reg_tmp;
    logic request_pipe_valid_reg_tmp;
    logic[3-1:0] cpu_rr_reg_tmp;
    logic[64'(unsigned'(32'(((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS))))))))-1:0] victim_reg_tmp;
    logic[64'(unsigned'(32'(((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS))))))))-1:0] fill_way_reg_tmp;
    logic[unsigned'(64'($clog2(unsigned'(32'((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))))))-1:0] init_set_reg_tmp;
    CacheResponse[16-1:0] response_reg_tmp;
    logic[PORT_BITWIDTH-1:0] cross_low_reg_tmp;
    logic[PORT_BITWIDTH-1:0] cross_high_reg_tmp;
    logic[PORT_BITWIDTH-1:0] refill_data_reg_tmp;
    logic[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] fill_beat_reg_tmp;
    logic[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] evict_beat_reg_tmp;
    logic[(ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))-1:0] evict_tag_reg_tmp;
    logic[CACHE_LINE_SIZE*64'h8-1:0] evict_line_reg_tmp;
    Axi4WriteAddress32_4[8-1:0] slave_aw_reg_tmp;
    Axi4WriteAddress32_4[8-1:0] slave_aw_seen_reg_tmp;
    Axi4ReadAddress32_4[8-1:0] slave_ar_seen_reg_tmp;
    logic[8-1:0] slave_aw_novelty_reg_tmp;
    logic[8-1:0] slave_ar_novelty_reg_tmp;


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

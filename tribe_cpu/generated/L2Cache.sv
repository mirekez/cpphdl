`default_nettype none

import Predef_pkg::*;
import Axi4WriteAddressReady_pkg::*;
import Axi4WriteDataReady_pkg::*;
import Axi4WriteResponse4_pkg::*;
import Axi4ReadAddressReady_pkg::*;
import Axi4ReadData4_256_pkg::*;
import Axi4Responder4_256_pkg::*;
import Axi4WriteAddress32_4_pkg::*;
import Axi4WriteData256_pkg::*;
import Axi4WriteResponseReady_pkg::*;
import Axi4ReadAddress32_4_pkg::*;
import Axi4ReadDataReady_pkg::*;
import Axi4Driver32_4_256_pkg::*;
import L2RamControlsComb_pkg::*;
import CacheRequest_pkg::*;
import L2ActiveRequestComb_pkg::*;
import L2CacheFsmState_pkg::*;
import L2RequestGeometryComb_pkg::*;
import L2HitLookupComb_pkg::*;
import L2WordPairComb_pkg::*;
import L2EvictCandidateComb_pkg::*;
import L2CpuWaitComb_pkg::*;
import L2IoWritePayloadComb_pkg::*;
import L2AxiRouteComb_pkg::*;
import L2AxiRequestNoveltyComb_pkg::*;
import CacheResponse_pkg::*;


module L2Cache #(
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
    Axi4Responder4_256 axi_in_comb[MEM_PORTS];
    Axi4Driver32_4_256 axi_out_comb[MEM_PORTS];
    L2RamControlsComb l2_ram_controls_comb;
    logic[unsigned'(64'($clog2(unsigned'(32'((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))))))-1:0] l2_ram_addr_comb;
    logic l2_ram_read_comb;
    logic[(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] l2_tag_data_comb;
    L2CpuWaitComb L2CacheWait___cpu_wait_comb[CPU_PORTS];
    L2HitLookupComb L2CacheTagData___hit_lookup_comb;
    L2WordPairComb L2CacheTagData___hit_write_pair_comb;
    L2WordPairComb L2CacheTagData___fill_write_pair_comb;
    logic[PORT_BITWIDTH-1:0] L2CacheTagData___cross_read_data_comb;
    logic[((((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8)-1:0] L2CacheTagData___tag_write_data_comb;
    logic[PORT_BITWIDTH-1:0] L2CacheTagData___read_data_comb[CPU_PORTS];
    L2IoWritePayloadComb L2CacheMemory___io_write_payload_comb;
    L2AxiRouteComb L2CacheMemory___axi_route_comb;
    Axi4Driver32_4_256 L2CacheMemory___axi_out_driver_comb;
    Axi4Responder4_256 L2CacheMemory___axi_out_selected_resp_comb;
    L2EvictCandidateComb L2CacheMemory___evict_candidate_comb;
    logic[PORT_BITWIDTH-1:0] L2CacheMemory___evict_line_comb;
    logic L2CacheMemory___req_uncached_region_comb;
    L2AxiRequestNoveltyComb L2CacheRequest___slave_request_novelty_comb;
    L2ActiveRequestComb L2CacheRequest___active_request_comb;
    L2RequestGeometryComb L2CacheRequest___request_geometry_comb;
    reg[DATA_BANKS-1:0][32-1:0] L2CacheState___lookup_data_reg;
    reg[WAYS-1:0][(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] L2CacheState___lookup_tag_reg;
    L2HitLookupComb L2CacheState___lookup_hit_reg;
    L2EvictCandidateComb L2CacheState___lookup_evict_reg;
    L2WordPairComb L2CacheState___lookup_write_pair_reg;
    reg[5-1:0] L2CacheState___state_reg;
    CacheRequest L2CacheState___req_reg;
    L2ActiveRequestComb L2CacheState___request_pipe_reg;
    reg L2CacheState___request_pipe_valid_reg;
    reg[3-1:0] L2CacheState___cpu_rr_reg;
    reg[64'(unsigned'(32'(((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS))))))))-1:0] L2CacheState___victim_reg;
    reg[64'(unsigned'(32'(((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS))))))))-1:0] L2CacheState___fill_way_reg;
    reg[unsigned'(64'($clog2(unsigned'(32'((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))))))-1:0] L2CacheState___init_set_reg;
    CacheResponse[16-1:0] L2CacheState___response_reg;
    reg[PORT_BITWIDTH-1:0] L2CacheState___cross_low_reg;
    reg[PORT_BITWIDTH-1:0] L2CacheState___cross_high_reg;
    reg[PORT_BITWIDTH-1:0] L2CacheState___refill_data_reg;
    reg[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] L2CacheState___fill_beat_reg;
    reg[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] L2CacheState___evict_beat_reg;
    reg[(ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))-1:0] L2CacheState___evict_tag_reg;
    reg[CACHE_LINE_SIZE*64'h8-1:0] L2CacheState___evict_line_reg;
    Axi4WriteAddress32_4[8-1:0] L2CacheState___slave_aw_reg;
    Axi4WriteAddress32_4[8-1:0] L2CacheState___slave_aw_seen_reg;
    Axi4ReadAddress32_4[8-1:0] L2CacheState___slave_ar_seen_reg;
    reg[8-1:0] L2CacheState___slave_aw_novelty_reg;
    reg[8-1:0] L2CacheState___slave_ar_novelty_reg;

    // members
    genvar __i;
    wire[unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))-1:0] L2CacheState___data_ram__addr_in[32];
    wire L2CacheState___data_ram__write_in[32];
    wire L2CacheState___data_ram__read_in[32];
    wire[64'h20-1:0] L2CacheState___data_ram__write_data_in[32];
    wire[64'h20-1:0] L2CacheState___data_ram__read_data_out[32];
    generate
    for (__i=0; __i < 32; __i = __i + 1) begin
        L2CacheRamBank #(
        64'h20
,       ((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))
        ) L2CacheState___data_ram (
            .clk(clk)
        ,           .l2_clock(l2_clock)
        ,           .reset(reset)
        ,           .addr_in(L2CacheState___data_ram__addr_in[__i])
        ,           .write_in(L2CacheState___data_ram__write_in[__i])
        ,           .read_in(L2CacheState___data_ram__read_in[__i])
        ,           .write_data_in(L2CacheState___data_ram__write_data_in[__i])
        ,           .read_data_out(L2CacheState___data_ram__read_data_out[__i])
        );
    end
    endgenerate
    wire[unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))-1:0] L2CacheState___tag_ram__addr_in[4];
    wire L2CacheState___tag_ram__write_in[4];
    wire L2CacheState___tag_ram__read_in[4];
    wire[(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] L2CacheState___tag_ram__write_data_in[4];
    wire[(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] L2CacheState___tag_ram__read_data_out[4];
    generate
    for (__i=0; __i < 4; __i = __i + 1) begin
        L2CacheRamBank #(
        (((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8
,       ((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))
        ) L2CacheState___tag_ram (
            .clk(clk)
        ,           .l2_clock(l2_clock)
        ,           .reset(reset)
        ,           .addr_in(L2CacheState___tag_ram__addr_in[__i])
        ,           .write_in(L2CacheState___tag_ram__write_in[__i])
        ,           .read_in(L2CacheState___tag_ram__read_in[__i])
        ,           .write_data_in(L2CacheState___tag_ram__write_data_in[__i])
        ,           .read_data_out(L2CacheState___tag_ram__read_data_out[__i])
        );
    end
    endgenerate

    // tmp variables
    logic[DATA_BANKS-1:0][32-1:0] L2CacheState___lookup_data_reg_tmp;
    logic[WAYS-1:0][(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] L2CacheState___lookup_tag_reg_tmp;
    L2HitLookupComb L2CacheState___lookup_hit_reg_tmp;
    L2EvictCandidateComb L2CacheState___lookup_evict_reg_tmp;
    L2WordPairComb L2CacheState___lookup_write_pair_reg_tmp;
    logic[5-1:0] L2CacheState___state_reg_tmp;
    CacheRequest L2CacheState___req_reg_tmp;
    L2ActiveRequestComb L2CacheState___request_pipe_reg_tmp;
    logic L2CacheState___request_pipe_valid_reg_tmp;
    logic[3-1:0] L2CacheState___cpu_rr_reg_tmp;
    logic[64'(unsigned'(32'(((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS))))))))-1:0] L2CacheState___victim_reg_tmp;
    logic[64'(unsigned'(32'(((WAYS<=64'h1) ? (32'h1) : ($clog2(unsigned'(32'(WAYS))))))))-1:0] L2CacheState___fill_way_reg_tmp;
    logic[unsigned'(64'($clog2(unsigned'(32'((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS)))))))-1:0] L2CacheState___init_set_reg_tmp;
    logic[PORT_BITWIDTH-1:0] L2CacheState___cross_low_reg_tmp;
    logic[PORT_BITWIDTH-1:0] L2CacheState___cross_high_reg_tmp;
    logic[PORT_BITWIDTH-1:0] L2CacheState___refill_data_reg_tmp;
    logic[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] L2CacheState___fill_beat_reg_tmp;
    logic[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] L2CacheState___evict_beat_reg_tmp;
    logic[(ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))-1:0] L2CacheState___evict_tag_reg_tmp;
    logic[CACHE_LINE_SIZE*64'h8-1:0] L2CacheState___evict_line_reg_tmp;
    logic[8-1:0] L2CacheState___slave_aw_novelty_reg_tmp;
    logic[8-1:0] L2CacheState___slave_ar_novelty_reg_tmp;


    always_comb begin : L2CacheRequest___slave_request_novelty_comb_func  // L2CacheRequest___slave_request_novelty_comb_func
        logic[31:0] index;
        L2CacheRequest___slave_request_novelty_comb = 0;
        for (index=32'h0;64'(index) < MEM_PORTS;index=index+1) begin
            L2CacheRequest___slave_request_novelty_comb.aw[64'(index)] = L2CacheState___slave_aw_novelty_reg[64'(index)];
            L2CacheRequest___slave_request_novelty_comb.ar[64'(index)] = L2CacheState___slave_ar_novelty_reg[64'(index)];
        end
    end

    always_comb begin : L2CacheRequest___active_request_comb_func  // L2CacheRequest___active_request_comb_func
        logic[31:0] port_index;
        logic[31:0] cpu_index;
        logic[63:0] byte_index;
        logic[63:0] word_index;
        logic[31:0] selected_slave;
        logic[31:0] selected_cpu;
        logic[31:0] candidate_cpu;
        logic[31:0] slave_addr;
        logic[31:0] lane;
        logic[31:0] _byte;
        logic[31:0] word;
        logic slave_write_pending;
        logic slave_read_pending;
        logic cpu_request_pending;
        L2CacheRequest___active_request_comb = 0;
        selected_slave=32'h0;
        selected_cpu=32'h0;
        candidate_cpu=32'h0;
        slave_addr=32'h0;
        lane=32'h0;
        _byte=32'h0;
        word=32'h0;
        slave_write_pending=0;
        slave_read_pending=0;
        cpu_request_pending=0;
        for (port_index=32'h0;64'(port_index) < MEM_PORTS;port_index=port_index+1) begin
            if (((((L2CacheState___slave_aw_reg[64'(port_index)].valid && axi_in__wvalid_in[port_index])) || (((axi_in__awvalid_in[port_index] && L2CacheRequest___slave_request_novelty_comb.aw[64'(port_index)]) && axi_in__wvalid_in[port_index])))) && ((!L2CacheState___response_reg[64'(port_index)].b.valid || axi_in__bready_in[port_index]))) begin
                slave_write_pending=1;
            end
            if ((axi_in__arvalid_in[port_index] && L2CacheRequest___slave_request_novelty_comb.ar[64'(port_index)]) && ((!L2CacheState___response_reg[64'(port_index)].r.valid || axi_in__rready_in[port_index]))) begin
                slave_read_pending=1;
            end
        end
        for (port_index=32'h0;64'(port_index) < MEM_PORTS;port_index=port_index+1) begin
            if (((!slave_write_pending && axi_in__arvalid_in[port_index]) && L2CacheRequest___slave_request_novelty_comb.ar[64'(port_index)]) && ((!L2CacheState___response_reg[64'(port_index)].r.valid || axi_in__rready_in[port_index]))) begin
                selected_slave=port_index;
            end
            if (((((L2CacheState___slave_aw_reg[64'(port_index)].valid && axi_in__wvalid_in[port_index])) || (((axi_in__awvalid_in[port_index] && L2CacheRequest___slave_request_novelty_comb.aw[64'(port_index)]) && axi_in__wvalid_in[port_index])))) && ((!L2CacheState___response_reg[64'(port_index)].b.valid || axi_in__bready_in[port_index]))) begin
                selected_slave=port_index;
            end
        end
        for (cpu_index=32'h0;64'(cpu_index) < CPU_PORTS;cpu_index=cpu_index+1) begin
            candidate_cpu=64'(32'(((unsigned'(32'(L2CacheState___cpu_rr_reg)) + cpu_index)))) % CPU_PORTS;
            if (!cpu_request_pending && ((((d_mem_in__write_in[candidate_cpu] || d_mem_in__read_in[candidate_cpu]) || i_mem_in__write_in[candidate_cpu]) || i_mem_in__read_in[candidate_cpu]))) begin
                selected_cpu=candidate_cpu;
                cpu_request_pending=1;
            end
        end
        L2CacheRequest___active_request_comb.request.from_slave = slave_write_pending || slave_read_pending;
        L2CacheRequest___active_request_comb.request.cpu_index = 64'(selected_cpu);
        L2CacheRequest___active_request_comb.request.port = (!((L2CacheRequest___active_request_comb.request.from_slave) != '0) && cpu_request_pending) && ((d_mem_in__write_in[selected_cpu] || d_mem_in__read_in[selected_cpu]));
        L2CacheRequest___active_request_comb.request.read = (((((L2CacheRequest___active_request_comb.request.from_slave) != '0) && !slave_write_pending)) || (((!((L2CacheRequest___active_request_comb.request.from_slave) != '0) && cpu_request_pending) && d_mem_in__read_in[selected_cpu]))) || (((((!((L2CacheRequest___active_request_comb.request.from_slave) != '0) && cpu_request_pending) && !d_mem_in__write_in[selected_cpu]) && !d_mem_in__read_in[selected_cpu]) && i_mem_in__read_in[selected_cpu]));
        L2CacheRequest___active_request_comb.request.write = (((((L2CacheRequest___active_request_comb.request.from_slave) != '0) && slave_write_pending)) || (((!((L2CacheRequest___active_request_comb.request.from_slave) != '0) && cpu_request_pending) && d_mem_in__write_in[selected_cpu]))) || (((((!((L2CacheRequest___active_request_comb.request.from_slave) != '0) && cpu_request_pending) && !d_mem_in__read_in[selected_cpu]) && !d_mem_in__write_in[selected_cpu]) && i_mem_in__write_in[selected_cpu]));
        L2CacheRequest___active_request_comb.request.addr = unsigned'(32'((((L2CacheRequest___active_request_comb.request.port) != '0)) ? (d_mem_in__addr_in[selected_cpu]) : (i_mem_in__addr_in[selected_cpu])));
        L2CacheRequest___active_request_comb.request.write_data = unsigned'(32'((((L2CacheRequest___active_request_comb.request.port) != '0)) ? (d_mem_in__write_data_in[selected_cpu]) : (i_mem_in__write_data_in[selected_cpu])));
        L2CacheRequest___active_request_comb.request.write_mask = unsigned'(8'((((L2CacheRequest___active_request_comb.request.from_slave) != '0)) ? (8'hF) : (((((L2CacheRequest___active_request_comb.request.port) != '0)) ? (d_mem_in__write_mask_in[selected_cpu]) : (i_mem_in__write_mask_in[selected_cpu])))));
        L2CacheRequest___active_request_comb.request.cache_disable = !((L2CacheRequest___active_request_comb.request.from_slave) != '0) && ((((L2CacheRequest___active_request_comb.request.port) != '0)) ? (d_mem_in__cache_disable_in[selected_cpu]) : (i_mem_in__cache_disable_in[selected_cpu]));
        L2CacheRequest___active_request_comb.request.slave_index = selected_slave;
        for (port_index=32'h0;64'(port_index) < MEM_PORTS;port_index=port_index+1) begin
            if (((L2CacheRequest___active_request_comb.request.from_slave) != '0) && (selected_slave == port_index)) begin
                slave_addr=(slave_write_pending) ? (((L2CacheState___slave_aw_reg[64'(port_index)].valid) ? (unsigned'(32'(L2CacheState___slave_aw_reg[64'(port_index)].addr))) : (unsigned'(32'(axi_in__awaddr_in[port_index]))))) : (unsigned'(32'(axi_in__araddr_in[port_index])));
                L2CacheRequest___active_request_comb.request.addr = unsigned'(32'((slave_addr < memory_base_in) ? (slave_addr + memory_base_in) : (slave_addr)));
                L2CacheRequest___active_request_comb.request.slave_id = (slave_write_pending) ? (((L2CacheState___slave_aw_reg[64'(port_index)].valid) ? (L2CacheState___slave_aw_reg[64'(port_index)].id) : (axi_in__awid_in[port_index]))) : (axi_in__arid_in[port_index]);
                if (slave_write_pending) begin
                    lane=((64'(unsigned'(32'(((L2CacheState___slave_aw_reg[64'(port_index)].valid) ? (unsigned'(32'(L2CacheState___slave_aw_reg[64'(port_index)].addr))) : (unsigned'(32'(axi_in__awaddr_in[port_index]))))))) % unsigned'(64'(PORT_BYTES))))/64'h4;
                    L2CacheRequest___active_request_comb.request.write_data = (axi_in__wdata_in[port_index] >> (lane*'h20));
                    L2CacheRequest___active_request_comb.request.write_beat = axi_in__wdata_in[port_index];
                    L2CacheRequest___active_request_comb.request.write_strobe = axi_in__wstrb_in[port_index];
                end
            end
        end
        if (!((L2CacheRequest___active_request_comb.request.from_slave) != '0)) begin
            _byte=unsigned'(32'(L2CacheRequest___active_request_comb.request.addr)) % 'h4;
            word=((64'(unsigned'(32'(L2CacheRequest___active_request_comb.request.addr))) % unsigned'(64'(PORT_BYTES))))/64'h4;
            for (byte_index=64'h0;byte_index < 64'h4;byte_index=byte_index+1) begin
                if ((((L2CacheRequest___active_request_comb.request.write_mask & 64'(32'((('h1 <<< byte_index)))))) != 64'h0) && ((64'(32'(((word*'h4) + _byte))) + byte_index) < unsigned'(64'(PORT_BYTES)))) begin
                    L2CacheRequest___active_request_comb.request.write_strobe[64'(32'(((word*'h4) + _byte))) + byte_index] = 64'h1;
                end
            end
        end
        for (word_index=64'h0;word_index < unsigned'(64'(PORT_WORDS));word_index=word_index+1) begin
            for (byte_index=64'h0;byte_index < 64'h4;byte_index=byte_index+1) begin
                if (L2CacheRequest___active_request_comb.request.write_strobe[(word_index*64'h4) + byte_index]) begin
                    L2CacheRequest___active_request_comb.request.write_word_mask[word_index] = 64'h1;
                end
            end
        end
        L2CacheRequest___active_request_comb.cache_set = 64'(32'(((unsigned'(32'(L2CacheRequest___active_request_comb.request.addr)) >>> unsigned'(64'(LINE_BITS)))))) & ((SETS - 64'h1));
        _byte=unsigned'(32'(L2CacheRequest___active_request_comb.request.addr)) & 'h3;
        word=64'(32'(((unsigned'(32'(L2CacheRequest___active_request_comb.request.addr)) >>> 32'sh2)))) & ((LINE_WORDS - 64'h1));
        L2CacheRequest___active_request_comb.valid = ((L2CacheRequest___active_request_comb.request.read) != '0) || ((L2CacheRequest___active_request_comb.request.write) != '0);
        L2CacheRequest___active_request_comb.cross_line_read = (((((L2CacheRequest___active_request_comb.request.read) != '0) && !((L2CacheRequest___active_request_comb.request.from_slave) != '0)) && !((L2CacheRequest___active_request_comb.request.port) != '0)) && (_byte != 32'h0)) && (64'(word) == (LINE_WORDS - 64'h1));
    end

    always_comb begin : axi_in_comb_func  // axi_in_comb_func
        logic[31:0] index;
        L2ActiveRequestComb active_request;
        active_request = L2CacheRequest___active_request_comb;
        for (index=32'h0;64'(index) < MEM_PORTS;index=index+1) begin
            axi_in_comb[index].aw.ready=(((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && !((L2CacheState___request_pipe_valid_reg) != '0)) && !L2CacheState___slave_aw_reg[64'(index)].valid) && L2CacheRequest___slave_request_novelty_comb.aw[64'(index)]) && ((!L2CacheState___response_reg[64'(index)].b.valid || axi_in__bready_in[index]))) && axi_in__awvalid_in[index];
            axi_in_comb[index].w.ready=((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && !((L2CacheState___request_pipe_valid_reg) != '0)) && ((active_request.request.from_slave) != '0)) && ((active_request.request.write) != '0)) && (active_request.request.slave_index == 64'(index));
            axi_in_comb[index].b = L2CacheState___response_reg[64'(index)].b;
            axi_in_comb[index].ar.ready=(((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && !((L2CacheState___request_pipe_valid_reg) != '0)) && L2CacheRequest___slave_request_novelty_comb.ar[64'(index)]) && ((active_request.request.from_slave) != '0)) && ((active_request.request.read) != '0)) && (active_request.request.slave_index == 64'(index));
            axi_in_comb[index].r = L2CacheState___response_reg[64'(index)].r;
        end
    end

    always_comb begin : L2CacheRequest___request_geometry_comb_func  // L2CacheRequest___request_geometry_comb_func
        logic[31:0] _byte;
        logic[31:0] word;
        logic[31:0] _local;
        logic[31:0] size;
        logic[31:0] i;
        L2CacheRequest___request_geometry_comb = 0;
        _byte=unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3;
        word=64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) >>> 32'sh2)))) & ((LINE_WORDS - 64'h1));
        _local=unsigned'(32'(L2CacheState___req_reg.addr)) - memory_base_in;
        size=memory_size_in;
        L2CacheRequest___request_geometry_comb.set = 64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) >>> unsigned'(64'(LINE_BITS)))))) & ((SETS - 64'h1));
        L2CacheRequest___request_geometry_comb.word = word;
        L2CacheRequest___request_geometry_comb.beat = ((64'(unsigned'(32'(L2CacheState___req_reg.addr))) & ((CACHE_LINE_SIZE - 64'h1))))/unsigned'(64'(PORT_BYTES));
        L2CacheRequest___request_geometry_comb.tag = 32'(unsigned'(32'(L2CacheState___req_reg.addr)) >>> ((LINE_BITS + SET_BITS)));
        L2CacheRequest___request_geometry_comb.cross_beat_read = ((((L2CacheState___req_reg.read) != '0) && !((L2CacheState___req_reg.from_slave) != '0)) && (_byte != 32'h0)) && (((((64'(unsigned'(32'(L2CacheState___req_reg.addr))) % unsigned'(64'(PORT_BYTES))))/64'h4)) + 64'h1)>=unsigned'(64'(PORT_WORDS));
        L2CacheRequest___request_geometry_comb.cross_write_data = unsigned'(32'((_byte == 32'h0) ? (32'h0) : (unsigned'(32'(L2CacheState___req_reg.write_data)) >>> ((32'h20 - (_byte*'h8))))));
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (((((L2CacheState___req_reg.write_mask & 64'(32'((('h1 <<< i))))))) != '0) && (i + _byte)>=32'h4) begin
                L2CacheRequest___request_geometry_comb.cross_write_mask |= 8'(64'(32'('h1 <<< (((i + _byte) - 32'h4)))));
                if ((((L2CacheState___req_reg.write) != '0) && (_byte != 32'h0)) && (64'(word) == (LINE_WORDS - 64'h1))) begin
                    L2CacheRequest___request_geometry_comb.cross_line_write = 1;
                end
            end
        end
        L2CacheRequest___request_geometry_comb.addr_in_memory = (L2CacheState___req_reg.addr>=memory_base_in && (size != 32'h0)) && (_local < size);
    end

    always_comb begin : L2CacheMemory___axi_route_comb_func  // L2CacheMemory___axi_route_comb_func
        logic[31:0] i;
        logic[63:0] base;
        logic[31:0] ar_total_local;
        logic[31:0] ar_region_base;
        logic[31:0] aw_total_local;
        logic[31:0] aw_region_base;
        L2CacheMemory___axi_route_comb.ar_full_addr = 64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) & ~32'(((CACHE_LINE_SIZE - 64'h1))))))) + ((64'(unsigned'(32'(L2CacheState___fill_beat_reg)))*PORT_BYTES));
        if ((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_AR))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_R)))) begin
            L2CacheMemory___axi_route_comb.ar_full_addr = L2CacheState___req_reg.addr;
        end
        if ((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_AR0))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_R0)))) begin
            L2CacheMemory___axi_route_comb.ar_full_addr = 64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) & ~32'(((CACHE_LINE_SIZE - 64'h1))))))) + ((64'(unsigned'(32'(L2CacheRequest___request_geometry_comb.beat)))*PORT_BYTES));
        end
        if ((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_AR1))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_R1)))) begin
            L2CacheMemory___axi_route_comb.ar_full_addr = 64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) & ~32'(((PORT_BYTES - 64'h1))))))) + PORT_BYTES;
        end
        ar_total_local=unsigned'(32'(L2CacheMemory___axi_route_comb.ar_full_addr)) - memory_base_in;
        base=64'h0;
        ar_region_base=32'h0;
        L2CacheMemory___axi_route_comb.ar_sel = MEM_PORTS - 64'h1;
        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
            if (ar_total_local>=base && (64'(ar_total_local) < (base + unsigned'(64'(mem_region_size_in[i]))))) begin
                L2CacheMemory___axi_route_comb.ar_sel = i;
                ar_region_base=base;
            end
            base+=unsigned'(64'(mem_region_size_in[i]));
        end
        L2CacheMemory___axi_route_comb.ar_local_addr = ((64'(32'(((ar_total_local - ar_region_base))))) & MEM_ADDR_MASK64);
        L2CacheMemory___axi_route_comb.aw_full_addr = 64'(32'(((((unsigned'(32'(L2CacheState___evict_tag_reg)) <<< ((SET_BITS + LINE_BITS)))) | ((unsigned'(32'(L2CacheRequest___request_geometry_comb.set)) <<< LINE_BITS)))))) + ((64'(unsigned'(32'(L2CacheState___evict_beat_reg)))*PORT_BYTES));
        if (((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_AW))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_W)))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_B)))) begin
            L2CacheMemory___axi_route_comb.aw_full_addr = L2CacheState___req_reg.addr;
        end
        aw_total_local=unsigned'(32'(L2CacheMemory___axi_route_comb.aw_full_addr)) - memory_base_in;
        base=64'h0;
        aw_region_base=32'h0;
        L2CacheMemory___axi_route_comb.aw_sel = MEM_PORTS - 64'h1;
        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
            if (aw_total_local>=base && (64'(aw_total_local) < (base + unsigned'(64'(mem_region_size_in[i]))))) begin
                L2CacheMemory___axi_route_comb.aw_sel = i;
                aw_region_base=base;
            end
            base+=unsigned'(64'(mem_region_size_in[i]));
        end
        L2CacheMemory___axi_route_comb.aw_local_addr = ((64'(32'(((aw_total_local - aw_region_base))))) & MEM_ADDR_MASK64);
    end

    always_comb begin : L2CacheMemory___io_write_payload_comb_func  // L2CacheMemory___io_write_payload_comb_func
        logic[31:0] _byte;
        logic[31:0] word;
        logic[31:0] i;
        L2CacheMemory___io_write_payload_comb = 0;
        if (((L2CacheState___req_reg.from_slave) != '0)) begin
            L2CacheMemory___io_write_payload_comb.data = L2CacheState___req_reg.write_beat;
            L2CacheMemory___io_write_payload_comb.strobe = L2CacheState___req_reg.write_strobe;
            disable L2CacheMemory___io_write_payload_comb_func;
        end
        _byte=unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3;
        word=((64'(unsigned'(32'(L2CacheState___req_reg.addr))) % unsigned'(64'(PORT_BYTES))))/64'h4;
        L2CacheMemory___io_write_payload_comb.data[word*32'h20 +:32] = 64'(32'(unsigned'(32'(L2CacheState___req_reg.write_data)) <<< ((_byte*'h8))));
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if ((((L2CacheState___req_reg.write_mask & 64'(32'((('h1 <<< i)))))) != 64'h0) && (64'(32'((((word*'h4) + _byte) + i))) < unsigned'(64'(PORT_BYTES)))) begin
                L2CacheMemory___io_write_payload_comb.strobe[64'(32'(((word*'h4) + _byte) + i))] = 64'h1;
            end
        end
    end

    always_comb begin : L2CacheMemory___evict_line_comb_func  // L2CacheMemory___evict_line_comb_func
        logic[31:0] word;
        logic[63:0] beat_word;
        word=32'h0;
        beat_word=64'h0;
        L2CacheMemory___evict_line_comb = 'h0;
        for (beat_word=64'h0;beat_word < unsigned'(64'(PORT_WORDS));beat_word=beat_word+1) begin
            word=(64'(unsigned'(32'(L2CacheState___evict_beat_reg)))*PORT_WORDS) + beat_word;
            if (64'(word) < unsigned'(64'(LINE_WORDS))) begin
                L2CacheMemory___evict_line_comb[beat_word*64'h20 +:32] = L2CacheState___evict_line_reg[word*32'h20 +:32];
            end
        end
    end

    always_comb begin : L2CacheMemory___axi_out_driver_comb_func  // L2CacheMemory___axi_out_driver_comb_func
        L2CacheMemory___axi_out_driver_comb.aw.valid=((L2CacheRequest___request_geometry_comb.addr_in_memory) != '0) && (((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_EVICT_AW))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_AW)))));
        L2CacheMemory___axi_out_driver_comb.aw.addr = 64'(unsigned'(32'(L2CacheMemory___axi_route_comb.aw_local_addr)));
        L2CacheMemory___axi_out_driver_comb.aw.id = 64'h0;
        L2CacheMemory___axi_out_driver_comb.w.valid=((L2CacheRequest___request_geometry_comb.addr_in_memory) != '0) && (((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_EVICT_W))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_W)))));
        L2CacheMemory___axi_out_driver_comb.w.data = (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_W))) ? (L2CacheMemory___io_write_payload_comb.data) : (unsigned'(256'(L2CacheMemory___evict_line_comb)));
        L2CacheMemory___axi_out_driver_comb.w.strb = (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_W))) ? (L2CacheMemory___io_write_payload_comb.strobe) : (unsigned'(32'($bits(32'h0)'(~(32'h0)))));
        L2CacheMemory___axi_out_driver_comb.w.last=L2CacheMemory___axi_out_driver_comb.w.valid;
        L2CacheMemory___axi_out_driver_comb.b.ready=L2CacheMemory___axi_route_comb.aw_sel < MEM_PORTS;
        L2CacheMemory___axi_out_driver_comb.ar.valid=((L2CacheRequest___request_geometry_comb.addr_in_memory) != '0) && (((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_AXI_AR))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_AR0)))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_AR1)))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_AR)))));
        L2CacheMemory___axi_out_driver_comb.ar.addr = 64'(unsigned'(32'(L2CacheMemory___axi_route_comb.ar_local_addr)));
        L2CacheMemory___axi_out_driver_comb.ar.id = 64'h0;
        L2CacheMemory___axi_out_driver_comb.r.ready=(((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_AXI_R))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_R0)))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_R1)))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_R)));
    end

    always_comb begin : axi_out_comb_func  // axi_out_comb_func
        logic[31:0] index;
        for (index=32'h0;64'(index) < MEM_PORTS;index=index+1) begin
            axi_out_comb[index].aw.valid=L2CacheMemory___axi_out_driver_comb.aw.valid && (unsigned'(32'(L2CacheMemory___axi_route_comb.aw_sel)) == index);
            axi_out_comb[index].aw.addr = L2CacheMemory___axi_out_driver_comb.aw.addr;
            axi_out_comb[index].aw.id = L2CacheMemory___axi_out_driver_comb.aw.id;
            axi_out_comb[index].w.valid=L2CacheMemory___axi_out_driver_comb.w.valid && (unsigned'(32'(L2CacheMemory___axi_route_comb.aw_sel)) == index);
            axi_out_comb[index].w.data = L2CacheMemory___axi_out_driver_comb.w.data;
            axi_out_comb[index].w.strb = L2CacheMemory___axi_out_driver_comb.w.strb;
            axi_out_comb[index].w.last=L2CacheMemory___axi_out_driver_comb.w.last && (unsigned'(32'(L2CacheMemory___axi_route_comb.aw_sel)) == index);
            axi_out_comb[index].b.ready=L2CacheMemory___axi_out_driver_comb.b.ready && (unsigned'(32'(L2CacheMemory___axi_route_comb.aw_sel)) == index);
            axi_out_comb[index].ar.valid=L2CacheMemory___axi_out_driver_comb.ar.valid && (unsigned'(32'(L2CacheMemory___axi_route_comb.ar_sel)) == index);
            axi_out_comb[index].ar.addr = L2CacheMemory___axi_out_driver_comb.ar.addr;
            axi_out_comb[index].ar.id = L2CacheMemory___axi_out_driver_comb.ar.id;
            axi_out_comb[index].r.ready=L2CacheMemory___axi_out_driver_comb.r.ready && (unsigned'(32'(L2CacheMemory___axi_route_comb.ar_sel)) == index);
        end
    end

    task send_slave_read_response (
        input logic[3-1:0] index
,       input logic[4-1:0] id
,       input logic[256-1:0] data
    );
    begin: send_slave_read_response
        L2CacheState___response_reg[index].r.valid<=1;
        L2CacheState___response_reg[index].r.id <= id;
        L2CacheState___response_reg[index].r.data <= data;
        L2CacheState___response_reg[index].r.last<=1;
    end
    endtask

    task send_slave_write_response (
        input logic[3-1:0] index
,       input logic[4-1:0] id
    );
    begin: send_slave_write_response
        L2CacheState___response_reg[index].b.valid<=1;
        L2CacheState___response_reg[index].b.id <= id;
    end
    endtask

    task send_cpu_response (input logic[256-1:0] data);
    begin: send_cpu_response
        L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].valid <= 1;
        L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].read <= L2CacheState___req_reg.read;
        L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].write <= L2CacheState___req_reg.write;
        L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].data_port <= L2CacheState___req_reg.port;
        L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].addr <= L2CacheState___req_reg.addr;
        L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].r.data <= data;
    end
    endtask

    always_comb begin : L2CacheTagData___hit_lookup_comb_func  // L2CacheTagData___hit_lookup_comb_func
        logic[31:0] i;
        logic[31:0] way;
        logic[63:0] word_index;
        logic[63:0] beat_word;
        logic[31:0] _byte;
        logic[31:0] word_data;
        L2CacheTagData___hit_lookup_comb = 0;
        way=32'h0;
        word_index=64'h0;
        beat_word=64'h0;
        _byte=unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3;
        word_data=32'h0;
        for (i=32'h0;64'(i) < WAYS;i=i+1) begin
            if (L2CacheState___lookup_tag_reg[64'(i)][(TAG_BITS + 64'h1)] && (L2CacheState___lookup_tag_reg[64'(i)]['h0 +:TAG_BITS] == L2CacheRequest___request_geometry_comb.tag)) begin
                L2CacheTagData___hit_lookup_comb.hit = 1;
                L2CacheTagData___hit_lookup_comb.way = i;
            end
        end
        for (i=32'h0;64'(i) < unsigned'(64'(DATA_BANKS));i=i+1) begin
            way=64'(i)/unsigned'(64'(LINE_WORDS));
            word_index=64'(i) % unsigned'(64'(LINE_WORDS));
            if (((L2CacheTagData___hit_lookup_comb.hit) != '0) && (unsigned'(32'(L2CacheTagData___hit_lookup_comb.way)) == way)) begin
                word_data=L2CacheState___lookup_data_reg[64'(i)];
                if (L2CacheRequest___request_geometry_comb.word == word_index) begin
                    L2CacheTagData___hit_lookup_comb.aligned_word = word_data;
                    L2CacheTagData___hit_lookup_comb.read_word |= 32'(64'(32'(word_data >>> ((_byte*'h8)))));
                end
                if (64'(32'((unsigned'(32'(L2CacheRequest___request_geometry_comb.word)) + 'h1))) == word_index) begin
                    L2CacheTagData___hit_lookup_comb.aligned_next_word = word_data;
                    if (_byte != 32'h0) begin
                        L2CacheTagData___hit_lookup_comb.read_word |= 32'(64'(32'(word_data <<< (('h20 - (_byte*'h8))))));
                    end
                end
                if (word_index>=(64'(unsigned'(32'(L2CacheRequest___request_geometry_comb.beat)))*PORT_WORDS) && (word_index < (64'(32'(((unsigned'(32'(L2CacheRequest___request_geometry_comb.beat)) + 'h1))))*PORT_WORDS))) begin
                    beat_word=word_index - (64'(unsigned'(32'(L2CacheRequest___request_geometry_comb.beat)))*PORT_WORDS);
                    L2CacheTagData___hit_lookup_comb.beat[beat_word*64'h20 +:32] = L2CacheState___lookup_data_reg[64'(i)];
                end
            end
        end
    end

    always_comb begin : L2CacheTagData___hit_write_pair_comb_func  // L2CacheTagData___hit_write_pair_comb_func
        logic[31:0] i;
        logic[31:0] _byte;
        logic[31:0] word_mask;
        logic[31:0] next_word_mask;
        logic[31:0] word_data;
        logic[31:0] next_word_data;
        _byte=unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3;
        word_mask=32'h0;
        next_word_mask=32'h0;
        word_data=unsigned'(32'(L2CacheState___req_reg.write_data)) <<< ((_byte*'h8));
        next_word_data=(_byte == 32'h0) ? (32'h0) : (unsigned'(32'(L2CacheState___req_reg.write_data)) >>> (('h20 - (_byte*'h8))));
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (((((L2CacheState___req_reg.write_mask & 64'(32'((('h1 <<< i))))))) != '0) && ((i + _byte) < 32'h4)) begin
                word_mask|='hFF <<< ((((i + _byte))*'h8));
            end
            if (((((L2CacheState___req_reg.write_mask & 64'(32'((('h1 <<< i))))))) != '0) && (i + _byte)>=32'h4) begin
                next_word_mask|='hFF <<< (((((i + _byte) - 'h4))*'h8));
            end
        end
        L2CacheTagData___hit_write_pair_comb.word = 32'(((unsigned'(32'(L2CacheTagData___hit_lookup_comb.aligned_word)) & ~word_mask)) | ((word_data & word_mask)));
        L2CacheTagData___hit_write_pair_comb.next_word = 32'(((unsigned'(32'(L2CacheTagData___hit_lookup_comb.aligned_next_word)) & ~next_word_mask)) | ((next_word_data & next_word_mask)));
    end

    always_comb begin : L2CacheTagData___fill_write_pair_comb_func  // L2CacheTagData___fill_write_pair_comb_func
        logic[31:0] i;
        logic[31:0] _byte;
        logic[31:0] word;
        logic[31:0] next_word;
        logic[31:0] old_word;
        logic[31:0] old_next_word;
        logic[31:0] word_mask;
        logic[31:0] next_word_mask;
        logic[31:0] word_data;
        logic[31:0] next_word_data;
        _byte=unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3;
        word=64'(unsigned'(32'(L2CacheRequest___request_geometry_comb.word))) % unsigned'(64'(PORT_WORDS));
        next_word=64'(32'(((unsigned'(32'(L2CacheRequest___request_geometry_comb.word)) + 'h1)))) % unsigned'(64'(PORT_WORDS));
        old_word=(L2CacheState___refill_data_reg >> (word*'h20));
        old_next_word=32'h0;
        if (64'(32'((unsigned'(32'(L2CacheRequest___request_geometry_comb.word)) + 'h1))) < unsigned'(64'(LINE_WORDS))) begin
            old_next_word=(L2CacheState___refill_data_reg >> (next_word*'h20));
        end
        word_mask=32'h0;
        next_word_mask=32'h0;
        word_data=unsigned'(32'(L2CacheState___req_reg.write_data)) <<< ((_byte*'h8));
        next_word_data=(_byte == 32'h0) ? (32'h0) : (unsigned'(32'(L2CacheState___req_reg.write_data)) >>> (('h20 - (_byte*'h8))));
        if (((L2CacheState___req_reg.write) != '0)) begin
            for (i=32'h0;i < 32'h4;i=i+1) begin
                if (((((L2CacheState___req_reg.write_mask & 64'(32'((('h1 <<< i))))))) != '0) && ((i + _byte) < 32'h4)) begin
                    word_mask|='hFF <<< ((((i + _byte))*'h8));
                end
                if (((((L2CacheState___req_reg.write_mask & 64'(32'((('h1 <<< i))))))) != '0) && (i + _byte)>=32'h4) begin
                    next_word_mask|='hFF <<< (((((i + _byte) - 'h4))*'h8));
                end
            end
        end
        L2CacheTagData___fill_write_pair_comb.word = 32'(((old_word & ~word_mask)) | ((word_data & word_mask)));
        L2CacheTagData___fill_write_pair_comb.next_word = 32'(((old_next_word & ~next_word_mask)) | ((next_word_data & next_word_mask)));
    end

    always_comb begin : L2CacheTagData___tag_write_data_comb_func  // L2CacheTagData___tag_write_data_comb_func
        if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_INIT))) begin
            L2CacheTagData___tag_write_data_comb = 'h0;
        end
        else begin
            L2CacheTagData___tag_write_data_comb = (((64'h1 <<< ((TAG_BITS + 64'h1)))) | ((unsigned'(64'(L2CacheState___req_reg.write)) <<< TAG_BITS))) | unsigned'(64'(L2CacheRequest___request_geometry_comb.tag));
        end
    end

    always_comb begin : l2_ram_controls_comb_func  // l2_ram_controls_comb_func
        logic[31:0] bank;
        logic[31:0] way;
        logic[31:0] dma_set;
        logic[31:0] dma_tag;
        logic[31:0] dma_way;
        logic[31:0] dma_word;
        logic[31:0] dma_byte;
        logic dma_line_fire;
        logic bank_write;
        L2RequestGeometryComb request_geometry;
        L2HitLookupComb hit_lookup;
        L2WordPairComb hit_write_pair;
        L2WordPairComb fill_write_pair;
        logic[(((((((ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((CACHE_SIZE/CACHE_LINE_SIZE)/WAYS))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))) + 64'h2) + 64'h7))/64'h8))*64'h8-1:0] tag_data;
        l2_ram_controls_comb = 0;
        dma_word=32'h0;
        tag_data = 'h0;
        request_geometry = L2CacheRequest___request_geometry_comb;
        hit_lookup = L2CacheTagData___hit_lookup_comb;
        if ((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_RESULT))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_RESULT)))) begin
            hit_lookup = L2CacheState___lookup_hit_reg;
        end
        hit_write_pair = L2CacheTagData___hit_write_pair_comb;
        fill_write_pair = L2CacheTagData___fill_write_pair_comb;
        dma_line_fire=dma_line_valid_in && dma_line_ready_out;
        dma_set=64'(32'(((unsigned'(32'(dma_line_addr_in)) >>> unsigned'(64'(LINE_BITS)))))) & ((SETS - 64'h1));
        dma_tag=unsigned'(32'(dma_line_addr_in)) >>> ((LINE_BITS + SET_BITS));
        dma_way=(WAYS<=64'h1) ? (64'h0) : (64'(dma_tag) % WAYS);
        l2_ram_controls_comb.addr = unsigned'(32'((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_INIT))) ? (unsigned'(32'(L2CacheState___init_set_reg))) : (((dma_line_fire) ? (dma_set) : ((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && ((L2CacheState___request_pipe_valid_reg) != '0)) ? (unsigned'(32'(L2CacheState___request_pipe_reg.cache_set))) : (unsigned'(32'(request_geometry.set)))))))));
        l2_ram_controls_comb.read = (((((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && ((L2CacheState___request_pipe_valid_reg) != '0)) && !((L2CacheState___request_pipe_reg.cross_line_read) != '0))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_READ)))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_READ))))) && !dma_line_fire;
        for (bank=32'h0;64'(bank) < unsigned'(64'(DATA_BANKS));bank=bank+1) begin
            bank_write=((((dma_line_fire && (64'(dma_way) == ((64'(bank)/unsigned'(64'(LINE_WORDS))))))) || (((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_AXI_R_WRITE))) && (L2CacheState___fill_way_reg == ((64'(bank)/unsigned'(64'(LINE_WORDS)))))) && (64'(bank) % unsigned'(64'(LINE_WORDS)))>=(64'(unsigned'(32'(L2CacheState___fill_beat_reg)))*PORT_WORDS)) && (((64'(bank) % unsigned'(64'(LINE_WORDS)))) < (64'(32'(((unsigned'(32'(L2CacheState___fill_beat_reg)) + 'h1))))*PORT_WORDS))))) || (((((((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_RESULT))) && ((L2CacheState___req_reg.from_slave) != '0)) && ((L2CacheState___req_reg.write) != '0)) && ((L2CacheState___lookup_hit_reg.hit) != '0)) && (L2CacheState___lookup_hit_reg.way == ((64'(bank)/unsigned'(64'(LINE_WORDS)))))) && (64'(bank) % unsigned'(64'(LINE_WORDS)))>=(64'(unsigned'(32'(request_geometry.beat)))*PORT_WORDS)) && (((64'(bank) % unsigned'(64'(LINE_WORDS)))) < (64'(32'(((unsigned'(32'(request_geometry.beat)) + 'h1))))*PORT_WORDS))) && L2CacheState___req_reg.write_word_mask[(((64'(bank) % unsigned'(64'(LINE_WORDS)))) % unsigned'(64'(PORT_WORDS)))]))) || (((((((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_RESULT))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_RESULT))))) && ((L2CacheState___req_reg.write) != '0)) && ((L2CacheState___lookup_hit_reg.hit) != '0)) && !((L2CacheState___req_reg.from_slave) != '0)) && (L2CacheState___lookup_hit_reg.way == ((64'(bank)/unsigned'(64'(LINE_WORDS)))))) && (((request_geometry.word == ((64'(bank) % unsigned'(64'(LINE_WORDS))))) || (((((unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3)) != 32'h0) && (64'(32'((unsigned'(32'(request_geometry.word)) + 32'h1))) == ((64'(bank) % unsigned'(64'(LINE_WORDS)))))))))));
            l2_ram_controls_comb.data_write[64'(bank)] = 64'(bank_write);
            if (dma_line_fire) begin
                dma_word=(dma_line_data_in >> (((64'(bank) % unsigned'(64'(LINE_WORDS))))*64'h20));
                for (dma_byte=32'h0;dma_byte < 32'h4;dma_byte=dma_byte+1) begin
                    if (!dma_line_keep_in[(((64'(bank) % unsigned'(64'(LINE_WORDS))))*64'h4) + 64'(dma_byte)]) begin
                        dma_word&=~('hFF <<< ((dma_byte*32'h8)));
                    end
                end
                l2_ram_controls_comb.data[64'(bank)] = dma_word;
            end
            else begin
                l2_ram_controls_comb.data[64'(bank)] = unsigned'(32'((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_RESULT))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_RESULT))))) ? (((((L2CacheState___req_reg.from_slave) != '0)) ? (unsigned'(32'((L2CacheState___req_reg.write_beat >> ((((64'(bank) % unsigned'(64'(PORT_WORDS))))*64'h20)))))) : (((((((unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3)) != 32'h0) && (64'(32'((unsigned'(32'(request_geometry.word)) + 32'h1))) == ((64'(bank) % unsigned'(64'(LINE_WORDS))))))) ? (unsigned'(32'(L2CacheState___lookup_write_pair_reg.next_word))) : (unsigned'(32'(L2CacheState___lookup_write_pair_reg.word))))))) : (((((((((L2CacheState___req_reg.from_slave) != '0) && ((L2CacheState___req_reg.write) != '0)) && (request_geometry.beat == L2CacheState___fill_beat_reg)) && (64'(bank) % unsigned'(64'(LINE_WORDS)))>=(64'(unsigned'(32'(L2CacheState___fill_beat_reg)))*PORT_WORDS)) && (((64'(bank) % unsigned'(64'(LINE_WORDS)))) < (64'(32'(((unsigned'(32'(L2CacheState___fill_beat_reg)) + 'h1))))*PORT_WORDS)))) ? (((L2CacheState___req_reg.write_word_mask[((64'(bank) % unsigned'(64'(LINE_WORDS)))) % unsigned'(64'(PORT_WORDS))]) ? (unsigned'(32'((L2CacheState___req_reg.write_beat >> ((((64'(bank) % unsigned'(64'(PORT_WORDS))))*64'h20)))))) : (unsigned'(32'((L2CacheState___refill_data_reg >> ((((((64'(bank) % unsigned'(64'(LINE_WORDS)))) % unsigned'(64'(PORT_WORDS))))*64'h20)))))))) : (((((L2CacheState___req_reg.write) != '0) && (request_geometry.word == ((64'(bank) % unsigned'(64'(LINE_WORDS))))))) ? (unsigned'(32'(fill_write_pair.word))) : ((((((L2CacheState___req_reg.write) != '0) && (((unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3)) != 32'h0)) && (64'(32'((unsigned'(32'(request_geometry.word)) + 32'h1))) == ((64'(bank) % unsigned'(64'(LINE_WORDS))))))) ? (unsigned'(32'(fill_write_pair.next_word))) : (unsigned'(32'((L2CacheState___refill_data_reg >> ((((((64'(bank) % unsigned'(64'(LINE_WORDS)))) % unsigned'(64'(PORT_WORDS))))*64'h20))))))))))));
            end
        end
        tag_data = L2CacheTagData___tag_write_data_comb;
        if (dma_line_fire) begin
            tag_data = (((64'h1 <<< ((TAG_BITS + 64'h1)))) | ((64'h1 <<< TAG_BITS))) | 64'(dma_tag);
        end
        l2_ram_controls_comb.tag_data = tag_data;
        for (way=32'h0;64'(way) < WAYS;way=way+1) begin
            l2_ram_controls_comb.tag_write[64'(way)] = 64'(((((dma_line_fire && (dma_way == way))) || ((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_INIT))))) || ((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_AXI_R_WRITE))) && (L2CacheState___fill_beat_reg == (LINE_BEATS - 64'h1))) && (L2CacheState___fill_way_reg == 64'(way))))) || (((((((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_RESULT))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_RESULT))))) && ((L2CacheState___req_reg.write) != '0)) && ((L2CacheState___lookup_hit_reg.hit) != '0)) && (L2CacheState___lookup_hit_reg.way == 64'(way)))));
        end
    end

    always_comb begin : l2_ram_addr_comb_func  // l2_ram_addr_comb_func
        l2_ram_addr_comb = l2_ram_controls_comb.addr;
    end

    always_comb begin : l2_ram_read_comb_func  // l2_ram_read_comb_func
        l2_ram_read_comb=((l2_ram_controls_comb.read) != '0);
    end

    always_comb begin : l2_tag_data_comb_func  // l2_tag_data_comb_func
        l2_tag_data_comb = l2_ram_controls_comb.tag_data;
    end

    always_comb begin : L2CacheTagData___read_data_comb_func  // L2CacheTagData___read_data_comb_func
        logic[31:0] index;
        for (index=32'h0;64'(index) < CPU_PORTS;index=index+1) begin
            L2CacheTagData___read_data_comb[index] = (((L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + index))].valid) != '0)) ? (unsigned'(PORT_BITWIDTH'(L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + index))].r.data))) : (PORT_BITWIDTH'('h0));
        end
    end

    always_comb begin : L2CacheWait___cpu_wait_comb_func  // L2CacheWait___cpu_wait_comb_func
        logic[31:0] index;
        logic done_i_read;
        logic done_d_read;
        logic done_d_write;
        for (index=32'h0;64'(index) < CPU_PORTS;index=index+1) begin
            done_i_read=(((((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].valid) != '0) && !((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].data_port) != '0)) && ((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].read) != '0)) && i_mem_in__read_in[index]) && (unsigned'(32'(i_mem_in__addr_in[index])) == unsigned'(32'(L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].addr)));
            done_d_read=(((((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].valid) != '0) && ((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].data_port) != '0)) && ((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].read) != '0)) && d_mem_in__read_in[index]) && (unsigned'(32'(d_mem_in__addr_in[index])) == unsigned'(32'(L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].addr)));
            done_d_write=(((((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].valid) != '0) && ((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].data_port) != '0)) && ((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].write) != '0)) && d_mem_in__write_in[index]) && (unsigned'(32'(d_mem_in__addr_in[index])) == unsigned'(32'(L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + index)))].addr)));
            L2CacheWait___cpu_wait_comb[index] = 0;
            if (i_mem_in__read_in[index]) begin
                L2CacheWait___cpu_wait_comb[index].instruction = !done_i_read;
            end
            if (d_mem_in__write_in[index]) begin
                L2CacheWait___cpu_wait_comb[index].data = !done_d_write;
            end
            if (d_mem_in__read_in[index]) begin
                L2CacheWait___cpu_wait_comb[index].data = !done_d_read;
            end
            if ((L2CacheState___state_reg != unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && !done_i_read) begin
                L2CacheWait___cpu_wait_comb[index].instruction = 1;
            end
            if ((L2CacheState___state_reg != unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && !((done_d_read || done_d_write))) begin
                L2CacheWait___cpu_wait_comb[index].data = 1;
            end
        end
    end

    always_comb begin : L2CacheMemory___req_uncached_region_comb_func  // L2CacheMemory___req_uncached_region_comb_func
        logic[31:0] _local;
        logic[63:0] base;
        logic[31:0] i;
        _local=unsigned'(32'(L2CacheState___req_reg.addr)) - memory_base_in;
        base=64'h0;
        L2CacheMemory___req_uncached_region_comb=0;
        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
            if (_local>=base && (64'(_local) < (base + unsigned'(64'(mem_region_size_in[i]))))) begin
                L2CacheMemory___req_uncached_region_comb=mem_region_uncached_in[i];
            end
            base+=unsigned'(64'(mem_region_size_in[i]));
        end
        L2CacheMemory___req_uncached_region_comb=((L2CacheRequest___request_geometry_comb.addr_in_memory) != '0) && ((((L2CacheState___req_reg.cache_disable) != '0) || L2CacheMemory___req_uncached_region_comb));
    end

    generate  // _assign
        genvar gi;
        for (gi=32'h0;64'(gi) < CPU_PORTS;gi=gi+1) begin
            assign i_mem_in__read_data_out[gi] = L2CacheTagData___read_data_comb[gi];
            assign i_mem_in__wait_out[gi] = L2CacheWait___cpu_wait_comb[gi].instruction;
            assign d_mem_in__read_data_out[gi] = L2CacheTagData___read_data_comb[gi];
            assign d_mem_in__wait_out[gi] = L2CacheWait___cpu_wait_comb[gi].data;
        end
        for (gi=32'h0;64'(gi) < MEM_PORTS;gi=gi+1) begin
            assign axi_in__awready_out[gi] = ((axi_in_comb[gi].aw.ready) != '0);
            assign axi_in__wready_out[gi] = ((axi_in_comb[gi].w.ready) != '0);
            assign axi_in__bvalid_out[gi] = ((axi_in_comb[gi].b.valid) != '0);
            assign axi_in__bid_out[gi] = 4'(unsigned'(64'(axi_in_comb[gi].b.id)));
            assign axi_in__arready_out[gi] = ((axi_in_comb[gi].ar.ready) != '0);
            assign axi_in__rvalid_out[gi] = ((axi_in_comb[gi].r.valid) != '0);
            assign axi_in__rdata_out[gi] = unsigned'(256'(axi_in_comb[gi].r.data));
            assign axi_in__rlast_out[gi] = ((axi_in_comb[gi].r.last) != '0);
            assign axi_in__rid_out[gi] = 4'(unsigned'(64'(axi_in_comb[gi].r.id)));
            assign axi_out__awvalid_out[gi] = ((axi_out_comb[gi].aw.valid) != '0);
            assign axi_out__awaddr_out[gi] = 23'(unsigned'(64'(axi_out_comb[gi].aw.addr)));
            assign axi_out__awid_out[gi] = 4'(unsigned'(64'(axi_out_comb[gi].aw.id)));
            assign axi_out__wvalid_out[gi] = ((axi_out_comb[gi].w.valid) != '0);
            assign axi_out__wdata_out[gi] = unsigned'(256'(axi_out_comb[gi].w.data));
            assign axi_out__wstrb_out[gi] = unsigned'(32'(axi_out_comb[gi].w.strb));
            assign axi_out__wlast_out[gi] = ((axi_out_comb[gi].w.last) != '0);
            assign axi_out__bready_out[gi] = ((axi_out_comb[gi].b.ready) != '0);
            assign axi_out__arvalid_out[gi] = ((axi_out_comb[gi].ar.valid) != '0);
            assign axi_out__araddr_out[gi] = 23'(unsigned'(64'(axi_out_comb[gi].ar.addr)));
            assign axi_out__arid_out[gi] = 4'(unsigned'(64'(axi_out_comb[gi].ar.id)));
            assign axi_out__rready_out[gi] = ((axi_out_comb[gi].r.ready) != '0);
        end
        assign L2CacheState___data_ram__addr_in['h0] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h0] = ((l2_ram_controls_comb.data_write[64'h0]) != '0);
        assign L2CacheState___data_ram__read_in['h0] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h0] = l2_ram_controls_comb.data[64'h0];
        assign L2CacheState___data_ram__addr_in['h1] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h1] = ((l2_ram_controls_comb.data_write[64'h1]) != '0);
        assign L2CacheState___data_ram__read_in['h1] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h1] = l2_ram_controls_comb.data[64'h1];
        assign L2CacheState___data_ram__addr_in['h2] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h2] = ((l2_ram_controls_comb.data_write[64'h2]) != '0);
        assign L2CacheState___data_ram__read_in['h2] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h2] = l2_ram_controls_comb.data[64'h2];
        assign L2CacheState___data_ram__addr_in['h3] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h3] = ((l2_ram_controls_comb.data_write[64'h3]) != '0);
        assign L2CacheState___data_ram__read_in['h3] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h3] = l2_ram_controls_comb.data[64'h3];
        assign L2CacheState___data_ram__addr_in['h4] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h4] = ((l2_ram_controls_comb.data_write[64'h4]) != '0);
        assign L2CacheState___data_ram__read_in['h4] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h4] = l2_ram_controls_comb.data[64'h4];
        assign L2CacheState___data_ram__addr_in['h5] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h5] = ((l2_ram_controls_comb.data_write[64'h5]) != '0);
        assign L2CacheState___data_ram__read_in['h5] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h5] = l2_ram_controls_comb.data[64'h5];
        assign L2CacheState___data_ram__addr_in['h6] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h6] = ((l2_ram_controls_comb.data_write[64'h6]) != '0);
        assign L2CacheState___data_ram__read_in['h6] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h6] = l2_ram_controls_comb.data[64'h6];
        assign L2CacheState___data_ram__addr_in['h7] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h7] = ((l2_ram_controls_comb.data_write[64'h7]) != '0);
        assign L2CacheState___data_ram__read_in['h7] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h7] = l2_ram_controls_comb.data[64'h7];
        assign L2CacheState___data_ram__addr_in['h8] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h8] = ((l2_ram_controls_comb.data_write[64'h8]) != '0);
        assign L2CacheState___data_ram__read_in['h8] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h8] = l2_ram_controls_comb.data[64'h8];
        assign L2CacheState___data_ram__addr_in['h9] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h9] = ((l2_ram_controls_comb.data_write[64'h9]) != '0);
        assign L2CacheState___data_ram__read_in['h9] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h9] = l2_ram_controls_comb.data[64'h9];
        assign L2CacheState___data_ram__addr_in['hA] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['hA] = ((l2_ram_controls_comb.data_write[64'hA]) != '0);
        assign L2CacheState___data_ram__read_in['hA] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['hA] = l2_ram_controls_comb.data[64'hA];
        assign L2CacheState___data_ram__addr_in['hB] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['hB] = ((l2_ram_controls_comb.data_write[64'hB]) != '0);
        assign L2CacheState___data_ram__read_in['hB] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['hB] = l2_ram_controls_comb.data[64'hB];
        assign L2CacheState___data_ram__addr_in['hC] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['hC] = ((l2_ram_controls_comb.data_write[64'hC]) != '0);
        assign L2CacheState___data_ram__read_in['hC] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['hC] = l2_ram_controls_comb.data[64'hC];
        assign L2CacheState___data_ram__addr_in['hD] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['hD] = ((l2_ram_controls_comb.data_write[64'hD]) != '0);
        assign L2CacheState___data_ram__read_in['hD] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['hD] = l2_ram_controls_comb.data[64'hD];
        assign L2CacheState___data_ram__addr_in['hE] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['hE] = ((l2_ram_controls_comb.data_write[64'hE]) != '0);
        assign L2CacheState___data_ram__read_in['hE] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['hE] = l2_ram_controls_comb.data[64'hE];
        assign L2CacheState___data_ram__addr_in['hF] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['hF] = ((l2_ram_controls_comb.data_write[64'hF]) != '0);
        assign L2CacheState___data_ram__read_in['hF] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['hF] = l2_ram_controls_comb.data[64'hF];
        assign L2CacheState___data_ram__addr_in['h10] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h10] = ((l2_ram_controls_comb.data_write[64'h10]) != '0);
        assign L2CacheState___data_ram__read_in['h10] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h10] = l2_ram_controls_comb.data[64'h10];
        assign L2CacheState___data_ram__addr_in['h11] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h11] = ((l2_ram_controls_comb.data_write[64'h11]) != '0);
        assign L2CacheState___data_ram__read_in['h11] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h11] = l2_ram_controls_comb.data[64'h11];
        assign L2CacheState___data_ram__addr_in['h12] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h12] = ((l2_ram_controls_comb.data_write[64'h12]) != '0);
        assign L2CacheState___data_ram__read_in['h12] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h12] = l2_ram_controls_comb.data[64'h12];
        assign L2CacheState___data_ram__addr_in['h13] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h13] = ((l2_ram_controls_comb.data_write[64'h13]) != '0);
        assign L2CacheState___data_ram__read_in['h13] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h13] = l2_ram_controls_comb.data[64'h13];
        assign L2CacheState___data_ram__addr_in['h14] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h14] = ((l2_ram_controls_comb.data_write[64'h14]) != '0);
        assign L2CacheState___data_ram__read_in['h14] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h14] = l2_ram_controls_comb.data[64'h14];
        assign L2CacheState___data_ram__addr_in['h15] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h15] = ((l2_ram_controls_comb.data_write[64'h15]) != '0);
        assign L2CacheState___data_ram__read_in['h15] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h15] = l2_ram_controls_comb.data[64'h15];
        assign L2CacheState___data_ram__addr_in['h16] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h16] = ((l2_ram_controls_comb.data_write[64'h16]) != '0);
        assign L2CacheState___data_ram__read_in['h16] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h16] = l2_ram_controls_comb.data[64'h16];
        assign L2CacheState___data_ram__addr_in['h17] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h17] = ((l2_ram_controls_comb.data_write[64'h17]) != '0);
        assign L2CacheState___data_ram__read_in['h17] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h17] = l2_ram_controls_comb.data[64'h17];
        assign L2CacheState___data_ram__addr_in['h18] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h18] = ((l2_ram_controls_comb.data_write[64'h18]) != '0);
        assign L2CacheState___data_ram__read_in['h18] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h18] = l2_ram_controls_comb.data[64'h18];
        assign L2CacheState___data_ram__addr_in['h19] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h19] = ((l2_ram_controls_comb.data_write[64'h19]) != '0);
        assign L2CacheState___data_ram__read_in['h19] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h19] = l2_ram_controls_comb.data[64'h19];
        assign L2CacheState___data_ram__addr_in['h1A] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h1A] = ((l2_ram_controls_comb.data_write[64'h1A]) != '0);
        assign L2CacheState___data_ram__read_in['h1A] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h1A] = l2_ram_controls_comb.data[64'h1A];
        assign L2CacheState___data_ram__addr_in['h1B] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h1B] = ((l2_ram_controls_comb.data_write[64'h1B]) != '0);
        assign L2CacheState___data_ram__read_in['h1B] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h1B] = l2_ram_controls_comb.data[64'h1B];
        assign L2CacheState___data_ram__addr_in['h1C] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h1C] = ((l2_ram_controls_comb.data_write[64'h1C]) != '0);
        assign L2CacheState___data_ram__read_in['h1C] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h1C] = l2_ram_controls_comb.data[64'h1C];
        assign L2CacheState___data_ram__addr_in['h1D] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h1D] = ((l2_ram_controls_comb.data_write[64'h1D]) != '0);
        assign L2CacheState___data_ram__read_in['h1D] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h1D] = l2_ram_controls_comb.data[64'h1D];
        assign L2CacheState___data_ram__addr_in['h1E] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h1E] = ((l2_ram_controls_comb.data_write[64'h1E]) != '0);
        assign L2CacheState___data_ram__read_in['h1E] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h1E] = l2_ram_controls_comb.data[64'h1E];
        assign L2CacheState___data_ram__addr_in['h1F] = l2_ram_addr_comb;
        assign L2CacheState___data_ram__write_in['h1F] = ((l2_ram_controls_comb.data_write[64'h1F]) != '0);
        assign L2CacheState___data_ram__read_in['h1F] = l2_ram_read_comb;
        assign L2CacheState___data_ram__write_data_in['h1F] = l2_ram_controls_comb.data[64'h1F];
        assign L2CacheState___tag_ram__addr_in['h0] = l2_ram_addr_comb;
        assign L2CacheState___tag_ram__write_in['h0] = ((l2_ram_controls_comb.tag_write[64'h0]) != '0);
        assign L2CacheState___tag_ram__read_in['h0] = l2_ram_read_comb;
        assign L2CacheState___tag_ram__write_data_in['h0] = l2_tag_data_comb;
        assign L2CacheState___tag_ram__addr_in['h1] = l2_ram_addr_comb;
        assign L2CacheState___tag_ram__write_in['h1] = ((l2_ram_controls_comb.tag_write[64'h1]) != '0);
        assign L2CacheState___tag_ram__read_in['h1] = l2_ram_read_comb;
        assign L2CacheState___tag_ram__write_data_in['h1] = l2_tag_data_comb;
        assign L2CacheState___tag_ram__addr_in['h2] = l2_ram_addr_comb;
        assign L2CacheState___tag_ram__write_in['h2] = ((l2_ram_controls_comb.tag_write[64'h2]) != '0);
        assign L2CacheState___tag_ram__read_in['h2] = l2_ram_read_comb;
        assign L2CacheState___tag_ram__write_data_in['h2] = l2_tag_data_comb;
        assign L2CacheState___tag_ram__addr_in['h3] = l2_ram_addr_comb;
        assign L2CacheState___tag_ram__write_in['h3] = ((l2_ram_controls_comb.tag_write[64'h3]) != '0);
        assign L2CacheState___tag_ram__read_in['h3] = l2_ram_read_comb;
        assign L2CacheState___tag_ram__write_data_in['h3] = l2_tag_data_comb;
        assign dma_line_ready_out = (((((((((((((((((64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_READ)))) || (((64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP))) && ((!((L2CacheState___req_reg.write) != '0) || L2CacheMemory___req_uncached_region_comb))))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_CAPTURE)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_RESULT)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_CAPTURE)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_RESULT)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_AXI_AR)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_EVICT_AW)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_EVICT_W)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_EVICT_B)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_AW)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_W)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_B)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_AR)))) || (64'(unsigned'(32'(L2CacheState___state_reg))) == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_R)))) != '0);
    endgenerate

    always_comb begin : L2CacheMemory___evict_candidate_comb_func  // L2CacheMemory___evict_candidate_comb_func
        logic[31:0] i;
        logic[31:0] way;
        logic[31:0] word;
        L2CacheMemory___evict_candidate_comb = 0;
        way=32'h0;
        word=32'h0;
        L2CacheMemory___evict_candidate_comb.way = unsigned'(32'(((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP)))) ? (unsigned'(32'(L2CacheState___victim_reg))) : (unsigned'(32'(L2CacheState___fill_way_reg)))));
        for (i=32'h0;64'(i) < WAYS;i=i+1) begin
            if (unsigned'(32'(L2CacheMemory___evict_candidate_comb.way)) == i) begin
                L2CacheMemory___evict_candidate_comb.valid = ((L2CacheState___lookup_tag_reg[64'(i)][TAG_BITS + 64'h1]) != '0);
                L2CacheMemory___evict_candidate_comb.dirty = ((L2CacheState___lookup_tag_reg[64'(i)][TAG_BITS]) != '0);
                L2CacheMemory___evict_candidate_comb.tag = unsigned'(64'(L2CacheState___lookup_tag_reg[64'(i)]['h0 +:TAG_BITS]));
            end
        end
        for (i=32'h0;64'(i) < unsigned'(64'(DATA_BANKS));i=i+1) begin
            way=64'(i)/unsigned'(64'(LINE_WORDS));
            word=64'(i) % unsigned'(64'(LINE_WORDS));
            if (unsigned'(32'(L2CacheMemory___evict_candidate_comb.way)) == way) begin
                L2CacheMemory___evict_candidate_comb.line[word*32'h20 +:32] = L2CacheState___lookup_data_reg[64'(i)];
            end
        end
    end

    always_comb begin : L2CacheMemory___axi_out_selected_resp_comb_func  // L2CacheMemory___axi_out_selected_resp_comb_func
        logic[31:0] i;
        L2CacheMemory___axi_out_selected_resp_comb.aw.ready=0;
        L2CacheMemory___axi_out_selected_resp_comb.w.ready=0;
        L2CacheMemory___axi_out_selected_resp_comb.b.valid=0;
        L2CacheMemory___axi_out_selected_resp_comb.b.id = 64'h0;
        L2CacheMemory___axi_out_selected_resp_comb.ar.ready=0;
        L2CacheMemory___axi_out_selected_resp_comb.r.valid=0;
        L2CacheMemory___axi_out_selected_resp_comb.r.data = 'h0;
        L2CacheMemory___axi_out_selected_resp_comb.r.last=0;
        L2CacheMemory___axi_out_selected_resp_comb.r.id = 64'h0;
        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
            if (unsigned'(32'(L2CacheMemory___axi_route_comb.aw_sel)) == i) begin
                L2CacheMemory___axi_out_selected_resp_comb.aw.ready=axi_out__awready_in[i];
                L2CacheMemory___axi_out_selected_resp_comb.w.ready=axi_out__wready_in[i];
                L2CacheMemory___axi_out_selected_resp_comb.b.valid=axi_out__bvalid_in[i];
                L2CacheMemory___axi_out_selected_resp_comb.b.id = axi_out__bid_in[i];
            end
            if (unsigned'(32'(L2CacheMemory___axi_route_comb.ar_sel)) == i) begin
                L2CacheMemory___axi_out_selected_resp_comb.ar.ready=axi_out__arready_in[i];
                L2CacheMemory___axi_out_selected_resp_comb.r.valid=axi_out__rvalid_in[i];
                L2CacheMemory___axi_out_selected_resp_comb.r.data = axi_out__rdata_in[i];
                L2CacheMemory___axi_out_selected_resp_comb.r.last=axi_out__rlast_in[i];
                L2CacheMemory___axi_out_selected_resp_comb.r.id = axi_out__rid_in[i];
            end
        end
    end

    always_comb begin : L2CacheTagData___cross_read_data_comb_func  // L2CacheTagData___cross_read_data_comb_func
        logic[31:0] low_word;
        logic[31:0] _byte;
        logic[31:0] low;
        logic[31:0] high;
        logic[31:0] data;
        L2CacheTagData___cross_read_data_comb = L2CacheState___cross_low_reg;
        _byte=unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3;
        low_word=(64'(unsigned'(32'(L2CacheState___req_reg.addr))) % unsigned'(64'(PORT_BYTES)))/64'h4;
        low=L2CacheState___cross_low_reg[low_word*32'h20 +:32];
        high=L2CacheState___cross_high_reg['h0 +:32];
        data=((low >>> ((_byte*'h8)))) | ((high <<< (('h20 - (_byte*'h8)))));
        L2CacheTagData___cross_read_data_comb = 'h0;
        L2CacheTagData___cross_read_data_comb['h0 +:32] = 64'(data);
    end

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
        logic[31:0] i;
        logic[31:0] way;
        logic dma_line_fire;
        logic[31:0] trace_line;
        logic trace_line_enabled;
        logic trace_req_line;
        logic trace_active_line;
        logic[31:0] trace_word0;
        logic[31:0] trace_word1;
        logic active_request_completed;
        L2ActiveRequestComb active_request;
        L2RequestGeometryComb request_geometry;
        L2EvictCandidateComb evict_candidate;
        L2HitLookupComb hit_lookup;
        L2WordPairComb hit_write_pair;
        L2WordPairComb fill_write_pair;
        logic[256-1:0] completion_data;
        active_request = L2CacheRequest___active_request_comb;
        active_request_completed=((((!((active_request.request.from_slave) != '0) && ((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(active_request.request.cpu_index)))))].valid) != '0)) && (L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(active_request.request.cpu_index)))))].data_port == active_request.request.port)) && (L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(active_request.request.cpu_index)))))].read == active_request.request.read)) && (L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(active_request.request.cpu_index)))))].write == active_request.request.write)) && (L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(active_request.request.cpu_index)))))].addr == active_request.request.addr);
        request_geometry = L2CacheRequest___request_geometry_comb;
        evict_candidate = L2CacheMemory___evict_candidate_comb;
        hit_lookup = L2CacheTagData___hit_lookup_comb;
        if ((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_RESULT))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_RESULT)))) begin
            evict_candidate = L2CacheState___lookup_evict_reg;
            hit_lookup = L2CacheState___lookup_hit_reg;
        end
        hit_write_pair = L2CacheTagData___hit_write_pair_comb;
        fill_write_pair = L2CacheTagData___fill_write_pair_comb;
        completion_data = 'h0;
        trace_line=32'h0;
        trace_line_enabled=0;
        trace_req_line=0;
        trace_active_line=0;
        if (debugen_in) begin
            trace_line_enabled=1;
            trace_line='h400;
            trace_req_line=(((unsigned'(32'(L2CacheState___req_reg.addr)) & ~32'(((CACHE_LINE_SIZE - 64'h1))))) == trace_line);
            trace_active_line=(((unsigned'(32'(active_request.request.addr)) & ~32'(((CACHE_LINE_SIZE - 64'h1))))) == trace_line);
        end
        trace_word0=32'h0;
        trace_word1=32'h0;
        dma_line_fire=dma_line_valid_in && dma_line_ready_out;
        for (i=32'h0;64'(i) < unsigned'(64'(DATA_BANKS));i=i+1) begin
        end
        for (way=32'h0;64'(way) < WAYS;way=way+1) begin
        end
        for (i=32'h0;64'(i) < CPU_PORTS;i=i+1) begin
            L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + i))].valid <= 0;
        end
        if ((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_CAPTURE))) || (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_CAPTURE)))) begin
            for (i=32'h0;64'(i) < unsigned'(64'(DATA_BANKS));i=i+1) begin
                L2CacheState___lookup_data_reg_tmp[64'(i)] = L2CacheState___data_ram__read_data_out[i];
            end
            for (way=32'h0;64'(way) < WAYS;way=way+1) begin
                L2CacheState___lookup_tag_reg_tmp[64'(way)] = L2CacheState___tag_ram__read_data_out[way];
            end
        end
        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
            L2CacheState___slave_aw_novelty_reg_tmp[64'(i)] = 64'((!L2CacheState___slave_aw_seen_reg[64'(i)].valid || (L2CacheState___slave_aw_seen_reg[64'(i)].addr != axi_in__awaddr_in[i])) || (L2CacheState___slave_aw_seen_reg[64'(i)].id != axi_in__awid_in[i]));
            L2CacheState___slave_ar_novelty_reg_tmp[64'(i)] = 64'((!L2CacheState___slave_ar_seen_reg[64'(i)].valid || (L2CacheState___slave_ar_seen_reg[64'(i)].addr != axi_in__araddr_in[i])) || (L2CacheState___slave_ar_seen_reg[64'(i)].id != axi_in__arid_in[i]));
            if (!axi_in__awvalid_in[i]) begin
                L2CacheState___slave_aw_seen_reg[64'(i)].valid<=0;
            end
            if (!axi_in__arvalid_in[i]) begin
                L2CacheState___slave_ar_seen_reg[64'(i)].valid<=0;
            end
            if (L2CacheState___response_reg[64'(i)].b.valid && axi_in__bready_in[i]) begin
                L2CacheState___response_reg[64'(i)].b.valid<=0;
            end
            if (L2CacheState___response_reg[64'(i)].r.valid && axi_in__rready_in[i]) begin
                L2CacheState___response_reg[64'(i)].r.valid<=0;
                L2CacheState___response_reg[64'(i)].r.last<=0;
            end
            if (((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && axi_in__awvalid_in[i]) && axi_in__awready_out[i]) begin
                L2CacheState___slave_aw_reg[64'(i)].valid<=1;
                L2CacheState___slave_aw_reg[64'(i)].addr <= axi_in__awaddr_in[i];
                L2CacheState___slave_aw_reg[64'(i)].id <= axi_in__awid_in[i];
                L2CacheState___slave_aw_seen_reg[64'(i)].valid<=1;
                L2CacheState___slave_aw_seen_reg[64'(i)].addr <= axi_in__awaddr_in[i];
                L2CacheState___slave_aw_seen_reg[64'(i)].id <= axi_in__awid_in[i];
            end
            if (((L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) && axi_in__arvalid_in[i]) && axi_in__arready_out[i]) begin
                L2CacheState___slave_ar_seen_reg[64'(i)].valid<=1;
                L2CacheState___slave_ar_seen_reg[64'(i)].addr <= axi_in__araddr_in[i];
                L2CacheState___slave_ar_seen_reg[64'(i)].id <= axi_in__arid_in[i];
            end
        end
        if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_INIT))) begin
            if (L2CacheState___init_set_reg == (SETS - 64'h1)) begin
                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
            end
            else begin
                L2CacheState___init_set_reg_tmp = L2CacheState___init_set_reg + 64'h1;
            end
        end
        else begin
            if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IDLE))) begin
                if (((L2CacheState___request_pipe_valid_reg) != '0)) begin
                    L2CacheState___request_pipe_valid_reg_tmp = 0;
                    if (!(((((((L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___request_pipe_reg.request.cpu_index)))))].valid) != '0) && !((L2CacheState___request_pipe_reg.request.from_slave) != '0)) && (L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___request_pipe_reg.request.cpu_index)))))].data_port == L2CacheState___request_pipe_reg.request.port)) && (L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___request_pipe_reg.request.cpu_index)))))].read == L2CacheState___request_pipe_reg.request.read)) && (L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___request_pipe_reg.request.cpu_index)))))].write == L2CacheState___request_pipe_reg.request.write)) && (L2CacheState___response_reg[64'(32'((CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___request_pipe_reg.request.cpu_index)))))].addr == L2CacheState___request_pipe_reg.request.addr))) begin
                        if (trace_active_line) begin
                            $write("trace-l2 cycle=%x cpu=%x accept addr=%08x rd=%x wr=%x wdata=%08x mask=%02x slave=%x dport=%x victim=%x\n", $time, unsigned'(32'(L2CacheState___request_pipe_reg.request.cpu_index)), unsigned'(32'(L2CacheState___request_pipe_reg.request.addr)), ((L2CacheState___request_pipe_reg.request.read) != '0), ((L2CacheState___request_pipe_reg.request.write) != '0), unsigned'(32'(L2CacheState___request_pipe_reg.request.write_data)), unsigned'(32'(L2CacheState___request_pipe_reg.request.write_mask)), ((L2CacheState___request_pipe_reg.request.from_slave) != '0), ((L2CacheState___request_pipe_reg.request.port) != '0), unsigned'(32'(L2CacheState___victim_reg)));
                        end
                        L2CacheState___req_reg_tmp = L2CacheState___request_pipe_reg.request;
                        if (!((L2CacheState___request_pipe_reg.request.from_slave) != '0)) begin
                            L2CacheState___cpu_rr_reg_tmp = unsigned'(32'((L2CacheState___request_pipe_reg.request.cpu_index == (CPU_PORTS - 64'h1)) ? (32'h0) : (unsigned'(32'(L2CacheState___request_pipe_reg.request.cpu_index)) + 'h1)));
                        end
                        L2CacheState___state_reg_tmp = (((L2CacheState___request_pipe_reg.cross_line_read) != '0)) ? (L2CacheFsmState_pkg::ST_CROSS_AR0) : (((dma_line_fire) ? (L2CacheFsmState_pkg::ST_READ) : (L2CacheFsmState_pkg::ST_LOOKUP_CAPTURE)));
                    end
                end
                else begin
                    if (((active_request.valid) != '0) && !active_request_completed) begin
                        L2CacheState___request_pipe_reg_tmp = active_request;
                        L2CacheState___request_pipe_valid_reg_tmp = 1;
                        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                            if ((((active_request.request.from_slave) != '0) && ((active_request.request.write) != '0)) && (active_request.request.slave_index == 64'(i))) begin
                                L2CacheState___slave_aw_reg[64'(i)].valid<=0;
                            end
                        end
                    end
                end
            end
            else begin
                if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_READ))) begin
                    if (!dma_line_fire) begin
                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_LOOKUP_CAPTURE;
                    end
                end
                else begin
                    if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_CAPTURE))) begin
                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_LOOKUP;
                    end
                    else begin
                        if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP))) begin
                            L2CacheState___lookup_hit_reg_tmp = hit_lookup;
                            L2CacheState___lookup_evict_reg_tmp = evict_candidate;
                            L2CacheState___lookup_write_pair_reg_tmp = hit_write_pair;
                            L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_LOOKUP_RESULT;
                        end
                        else begin
                            if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_LOOKUP_RESULT))) begin
                                if (!((request_geometry.addr_in_memory) != '0)) begin
                                    if (trace_req_line) begin
                                        $write("trace-l2 cycle=%x lookup-outside addr=%08x rd=%x wr=%x\n", $time, unsigned'(32'(L2CacheState___req_reg.addr)), ((L2CacheState___req_reg.read) != '0), ((L2CacheState___req_reg.write) != '0));
                                    end
                                    if (((L2CacheState___req_reg.from_slave) != '0)) begin
                                        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                                            if (L2CacheState___req_reg.slave_index == 64'(i)) begin
                                                if (((L2CacheState___req_reg.read) != '0)) begin
                                                    send_slave_read_response(3'(64'(i)), L2CacheState___req_reg.slave_id, 256'h0);
                                                end
                                                if (((L2CacheState___req_reg.write) != '0)) begin
                                                    send_slave_write_response(3'(64'(i)), L2CacheState___req_reg.slave_id);
                                                end
                                            end
                                        end
                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                    end
                                    else begin
                                        send_cpu_response(256'h0);
                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                    end
                                end
                                else begin
                                    if (L2CacheMemory___req_uncached_region_comb) begin
                                        if (trace_req_line) begin
                                            $write("trace-l2 cycle=%x lookup-uncached addr=%08x rd=%x wr=%x\n", $time, unsigned'(32'(L2CacheState___req_reg.addr)), ((L2CacheState___req_reg.read) != '0), ((L2CacheState___req_reg.write) != '0));
                                        end
                                        L2CacheState___state_reg_tmp = (((L2CacheState___req_reg.read) != '0)) ? (L2CacheFsmState_pkg::ST_IO_AR) : (L2CacheFsmState_pkg::ST_IO_AW);
                                    end
                                    else begin
                                        if (((hit_lookup.hit) != '0)) begin
                                            if (trace_req_line) begin
                                                trace_word0=hit_lookup.beat;
                                                trace_word1=(unsigned'(64'(PORT_WORDS)) > 64'h1) ? (unsigned'(32'((hit_lookup.beat >> 'h20)))) : (32'h0);
                                                $write("trace-l2 cycle=%x lookup-hit addr=%08x rd=%x wr=%x way=%x word=%x hit_word=%08x beat0=%08x beat1=%08x wdata=%08x mask=%02x\n", $time, unsigned'(32'(L2CacheState___req_reg.addr)), ((L2CacheState___req_reg.read) != '0), ((L2CacheState___req_reg.write) != '0), unsigned'(32'(hit_lookup.way)), unsigned'(32'(request_geometry.word)), unsigned'(32'(hit_lookup.read_word)), trace_word0, trace_word1, unsigned'(32'(L2CacheState___req_reg.write_data)), unsigned'(32'(L2CacheState___req_reg.write_mask)));
                                            end
                                            if (((L2CacheState___req_reg.from_slave) != '0)) begin
                                                for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                                                    if (L2CacheState___req_reg.slave_index == 64'(i)) begin
                                                        if (((L2CacheState___req_reg.read) != '0)) begin
                                                            send_slave_read_response(3'(64'(i)), L2CacheState___req_reg.slave_id, hit_lookup.beat);
                                                        end
                                                        if (((L2CacheState___req_reg.write) != '0) && !((request_geometry.cross_line_write) != '0)) begin
                                                            send_slave_write_response(3'(64'(i)), L2CacheState___req_reg.slave_id);
                                                        end
                                                    end
                                                end
                                            end
                                            if (((request_geometry.cross_line_write) != '0)) begin
                                                L2CacheState___req_reg_tmp.addr = 64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) & ~32'(((CACHE_LINE_SIZE - 64'h1))))))) + CACHE_LINE_SIZE;
                                                L2CacheState___req_reg_tmp.write_data = request_geometry.cross_write_data;
                                                L2CacheState___req_reg_tmp.write_mask = request_geometry.cross_write_mask;
                                                L2CacheState___req_reg_tmp.write_strobe = active_request.request.write_strobe;
                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_WRITE_READ;
                                            end
                                            else begin
                                                if (!((L2CacheState___req_reg.from_slave) != '0)) begin
                                                    completion_data = 'h0;
                                                    if (((L2CacheState___req_reg.read) != '0) && ((request_geometry.cross_beat_read) != '0)) begin
                                                        completion_data['h0 +:32] = hit_lookup.read_word;
                                                    end
                                                    else begin
                                                        if (((L2CacheState___req_reg.read) != '0)) begin
                                                            completion_data = hit_lookup.beat;
                                                        end
                                                    end
                                                    send_cpu_response(completion_data);
                                                end
                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                            end
                                        end
                                        else begin
                                            if (trace_req_line) begin
                                                $write("trace-l2 cycle=%x lookup-miss addr=%08x rd=%x wr=%x victim=%x evict_valid=%x evict_dirty=%x evict_tag=%08x\n", $time, unsigned'(32'(L2CacheState___req_reg.addr)), ((L2CacheState___req_reg.read) != '0), ((L2CacheState___req_reg.write) != '0), unsigned'(32'(L2CacheState___victim_reg)), ((evict_candidate.valid) != '0), ((evict_candidate.dirty) != '0), unsigned'(32'(evict_candidate.tag)));
                                            end
                                            L2CacheState___fill_way_reg_tmp = L2CacheState___victim_reg;
                                            L2CacheState___fill_beat_reg_tmp = 64'h0;
                                            L2CacheState___evict_beat_reg_tmp = 64'h0;
                                            L2CacheState___evict_tag_reg_tmp = evict_candidate.tag;
                                            L2CacheState___evict_line_reg_tmp = evict_candidate.line;
                                            L2CacheState___state_reg_tmp = ((!((L2CacheState___req_reg.from_slave) != '0) && ((request_geometry.cross_beat_read) != '0))) ? (L2CacheFsmState_pkg::ST_CROSS_AR0) : ((((((evict_candidate.valid) != '0) && ((evict_candidate.dirty) != '0))) ? (L2CacheFsmState_pkg::ST_EVICT_AW) : (L2CacheFsmState_pkg::ST_AXI_AR)));
                                        end
                                    end
                                end
                            end
                            else begin
                                if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_READ))) begin
                                    if (!dma_line_fire) begin
                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_WRITE_CAPTURE;
                                    end
                                end
                                else begin
                                    if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_CAPTURE))) begin
                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_WRITE_LOOKUP;
                                    end
                                    else begin
                                        if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_LOOKUP))) begin
                                            L2CacheState___lookup_hit_reg_tmp = hit_lookup;
                                            L2CacheState___lookup_evict_reg_tmp = evict_candidate;
                                            L2CacheState___lookup_write_pair_reg_tmp = hit_write_pair;
                                            L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_WRITE_RESULT;
                                        end
                                        else begin
                                            if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_WRITE_RESULT))) begin
                                                if (!((request_geometry.addr_in_memory) != '0)) begin
                                                    if (((L2CacheState___req_reg.from_slave) != '0)) begin
                                                        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                                                            if (L2CacheState___req_reg.slave_index == 64'(i)) begin
                                                                send_slave_write_response(3'(64'(i)), L2CacheState___req_reg.slave_id);
                                                            end
                                                        end
                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                    end
                                                    else begin
                                                        send_cpu_response(256'h0);
                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                    end
                                                end
                                                else begin
                                                    if (((hit_lookup.hit) != '0)) begin
                                                        if (((L2CacheState___req_reg.from_slave) != '0)) begin
                                                            for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                                                                if (L2CacheState___req_reg.slave_index == 64'(i)) begin
                                                                    send_slave_write_response(3'(64'(i)), L2CacheState___req_reg.slave_id);
                                                                end
                                                            end
                                                            L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                        end
                                                        else begin
                                                            send_cpu_response(256'h0);
                                                            L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                        end
                                                    end
                                                    else begin
                                                        L2CacheState___fill_way_reg_tmp = L2CacheState___victim_reg;
                                                        L2CacheState___fill_beat_reg_tmp = 64'h0;
                                                        L2CacheState___evict_beat_reg_tmp = 64'h0;
                                                        L2CacheState___evict_tag_reg_tmp = evict_candidate.tag;
                                                        L2CacheState___evict_line_reg_tmp = evict_candidate.line;
                                                        L2CacheState___state_reg_tmp = ((((evict_candidate.valid) != '0) && ((evict_candidate.dirty) != '0))) ? (L2CacheFsmState_pkg::ST_EVICT_AW) : (L2CacheFsmState_pkg::ST_AXI_AR);
                                                    end
                                                end
                                            end
                                            else begin
                                                if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_EVICT_AW))) begin
                                                    if (L2CacheMemory___axi_out_driver_comb.aw.valid && L2CacheMemory___axi_out_selected_resp_comb.aw.ready) begin
                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_EVICT_W;
                                                    end
                                                end
                                                else begin
                                                    if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_EVICT_W))) begin
                                                        if (L2CacheMemory___axi_out_driver_comb.w.valid && L2CacheMemory___axi_out_selected_resp_comb.w.ready) begin
                                                            if (trace_line_enabled && ((((L2CacheMemory___axi_route_comb.aw_full_addr & 64'(32'(~32'(((CACHE_LINE_SIZE - 64'h1))))))) == 64'(trace_line)))) begin
                                                                trace_word0=L2CacheMemory___evict_line_comb;
                                                                trace_word1=(unsigned'(64'(PORT_WORDS)) > 64'h1) ? (unsigned'(32'((L2CacheMemory___evict_line_comb >> 'h20)))) : (32'h0);
                                                                $write("trace-l2 cycle=%x evict addr=%08x beat=%x data0=%08x data1=%08x way=%x\n", $time, unsigned'(32'(L2CacheMemory___axi_route_comb.aw_full_addr)), unsigned'(32'(L2CacheState___evict_beat_reg)), trace_word0, trace_word1, unsigned'(32'(evict_candidate.way)));
                                                            end
                                                            L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_EVICT_B;
                                                        end
                                                    end
                                                    else begin
                                                        if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_EVICT_B))) begin
                                                            if (L2CacheMemory___axi_out_selected_resp_comb.b.valid) begin
                                                                if (L2CacheState___evict_beat_reg == (LINE_BEATS - 64'h1)) begin
                                                                    L2CacheState___fill_beat_reg_tmp = 64'h0;
                                                                    L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_AXI_AR;
                                                                end
                                                                else begin
                                                                    L2CacheState___evict_beat_reg_tmp = L2CacheState___evict_beat_reg + 64'h1;
                                                                    L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_EVICT_AW;
                                                                end
                                                            end
                                                        end
                                                        else begin
                                                            if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_AXI_AR))) begin
                                                                if (L2CacheMemory___axi_out_driver_comb.ar.valid && L2CacheMemory___axi_out_selected_resp_comb.ar.ready) begin
                                                                    L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_AXI_R;
                                                                end
                                                            end
                                                            else begin
                                                                if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_AXI_R))) begin
                                                                    if (L2CacheMemory___axi_out_selected_resp_comb.r.valid && L2CacheMemory___axi_out_driver_comb.r.ready) begin
                                                                        L2CacheState___refill_data_reg_tmp = L2CacheMemory___axi_out_selected_resp_comb.r.data;
                                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_AXI_R_WRITE;
                                                                    end
                                                                end
                                                                else begin
                                                                    if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_AXI_R_WRITE))) begin
                                                                        if (trace_req_line) begin
                                                                            trace_word0=L2CacheState___refill_data_reg;
                                                                            trace_word1=(unsigned'(64'(PORT_WORDS)) > 64'h1) ? (unsigned'(32'((L2CacheState___refill_data_reg >> 'h20)))) : (32'h0);
                                                                            $write("trace-l2 cycle=%x fill addr=%08x beat=%x data0=%08x data1=%08x req_word=%x req_beat=%x\n", $time, unsigned'(32'(L2CacheMemory___axi_route_comb.ar_full_addr)), unsigned'(32'(L2CacheState___fill_beat_reg)), trace_word0, trace_word1, unsigned'(32'(request_geometry.word)), unsigned'(32'(request_geometry.beat)));
                                                                        end
                                                                        if (((L2CacheState___req_reg.read) != '0) && (L2CacheState___fill_beat_reg == request_geometry.beat)) begin
                                                                            L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].r.data <= L2CacheState___refill_data_reg;
                                                                        end
                                                                        if (L2CacheState___fill_beat_reg == (LINE_BEATS - 64'h1)) begin
                                                                            L2CacheState___victim_reg_tmp = ((L2CacheState___victim_reg == (WAYS - 64'h1))) ? (64'h0) : (L2CacheState___victim_reg + 64'h1);
                                                                            if (((request_geometry.cross_line_write) != '0)) begin
                                                                                L2CacheState___req_reg_tmp.addr = 64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) & ~32'(((CACHE_LINE_SIZE - 64'h1))))))) + CACHE_LINE_SIZE;
                                                                                L2CacheState___req_reg_tmp.write_data = request_geometry.cross_write_data;
                                                                                L2CacheState___req_reg_tmp.write_mask = request_geometry.cross_write_mask;
                                                                                L2CacheState___req_reg_tmp.write_strobe = active_request.request.write_strobe;
                                                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_WRITE_READ;
                                                                            end
                                                                            else begin
                                                                                if (((L2CacheState___req_reg.from_slave) != '0)) begin
                                                                                    for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                                                                                        if (L2CacheState___req_reg.slave_index == 64'(i)) begin
                                                                                            if (((L2CacheState___req_reg.read) != '0)) begin
                                                                                                send_slave_read_response(3'(64'(i)), L2CacheState___req_reg.slave_id, ((L2CacheState___fill_beat_reg == request_geometry.beat)) ? (L2CacheState___refill_data_reg) : (L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].r.data));
                                                                                            end
                                                                                            if (((L2CacheState___req_reg.write) != '0)) begin
                                                                                                send_slave_write_response(3'(64'(i)), L2CacheState___req_reg.slave_id);
                                                                                            end
                                                                                        end
                                                                                    end
                                                                                    L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                end
                                                                                else begin
                                                                                    completion_data = (((L2CacheState___req_reg.read) != '0)) ? ((((L2CacheState___fill_beat_reg == request_geometry.beat)) ? (L2CacheState___refill_data_reg) : (L2CacheState___response_reg[64'(32'(CPU_RESPONSE_BASE + unsigned'(32'(L2CacheState___req_reg.cpu_index))))].r.data))) : (256'h0);
                                                                                    send_cpu_response(completion_data);
                                                                                    L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                end
                                                                            end
                                                                        end
                                                                        else begin
                                                                            L2CacheState___fill_beat_reg_tmp = L2CacheState___fill_beat_reg + 64'h1;
                                                                            L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_AXI_AR;
                                                                        end
                                                                    end
                                                                    else begin
                                                                        if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_AR0))) begin
                                                                            if (L2CacheMemory___axi_out_driver_comb.ar.valid && L2CacheMemory___axi_out_selected_resp_comb.ar.ready) begin
                                                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_R0;
                                                                            end
                                                                        end
                                                                        else begin
                                                                            if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_R0))) begin
                                                                                if (L2CacheMemory___axi_out_selected_resp_comb.r.valid && L2CacheMemory___axi_out_driver_comb.r.ready) begin
                                                                                    L2CacheState___cross_low_reg_tmp = L2CacheMemory___axi_out_selected_resp_comb.r.data;
                                                                                    L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_AR1;
                                                                                end
                                                                            end
                                                                            else begin
                                                                                if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_AR1))) begin
                                                                                    if (L2CacheMemory___axi_out_driver_comb.ar.valid && L2CacheMemory___axi_out_selected_resp_comb.ar.ready) begin
                                                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_R1;
                                                                                    end
                                                                                end
                                                                                else begin
                                                                                    if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_R1))) begin
                                                                                        if (L2CacheMemory___axi_out_selected_resp_comb.r.valid && L2CacheMemory___axi_out_driver_comb.r.ready) begin
                                                                                            L2CacheState___cross_high_reg_tmp = L2CacheMemory___axi_out_selected_resp_comb.r.data;
                                                                                            L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_CROSS_DONE;
                                                                                        end
                                                                                    end
                                                                                    else begin
                                                                                        if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_CROSS_DONE))) begin
                                                                                            if (((L2CacheState___req_reg.from_slave) != '0)) begin
                                                                                                for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                                                                                                    if (L2CacheState___req_reg.slave_index == 64'(i)) begin
                                                                                                        send_slave_read_response(3'(64'(i)), L2CacheState___req_reg.slave_id, L2CacheTagData___cross_read_data_comb);
                                                                                                    end
                                                                                                end
                                                                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                            end
                                                                                            else begin
                                                                                                send_cpu_response(L2CacheTagData___cross_read_data_comb);
                                                                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                            end
                                                                                        end
                                                                                        else begin
                                                                                            if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_AW))) begin
                                                                                                if (L2CacheMemory___axi_out_driver_comb.aw.valid && L2CacheMemory___axi_out_selected_resp_comb.aw.ready) begin
                                                                                                    L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IO_W;
                                                                                                end
                                                                                            end
                                                                                            else begin
                                                                                                if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_W))) begin
                                                                                                    if (L2CacheMemory___axi_out_driver_comb.w.valid && L2CacheMemory___axi_out_selected_resp_comb.w.ready) begin
                                                                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IO_B;
                                                                                                    end
                                                                                                end
                                                                                                else begin
                                                                                                    if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_B))) begin
                                                                                                        if (L2CacheMemory___axi_out_selected_resp_comb.b.valid) begin
                                                                                                            if (((L2CacheState___req_reg.from_slave) != '0)) begin
                                                                                                                for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                                                                                                                    if (L2CacheState___req_reg.slave_index == 64'(i)) begin
                                                                                                                        send_slave_write_response(3'(64'(i)), L2CacheState___req_reg.slave_id);
                                                                                                                    end
                                                                                                                end
                                                                                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                                            end
                                                                                                            else begin
                                                                                                                send_cpu_response(256'h0);
                                                                                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                                            end
                                                                                                        end
                                                                                                    end
                                                                                                    else begin
                                                                                                        if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_AR))) begin
                                                                                                            if (L2CacheMemory___axi_out_driver_comb.ar.valid && L2CacheMemory___axi_out_selected_resp_comb.ar.ready) begin
                                                                                                                L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IO_R;
                                                                                                            end
                                                                                                        end
                                                                                                        else begin
                                                                                                            if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_R))) begin
                                                                                                                if (L2CacheMemory___axi_out_selected_resp_comb.r.valid && L2CacheMemory___axi_out_driver_comb.r.ready) begin
                                                                                                                    L2CacheState___refill_data_reg_tmp = L2CacheMemory___axi_out_selected_resp_comb.r.data;
                                                                                                                    L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IO_R_RESULT;
                                                                                                                end
                                                                                                            end
                                                                                                            else begin
                                                                                                                if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_IO_R_RESULT))) begin
                                                                                                                    if (((L2CacheState___req_reg.from_slave) != '0)) begin
                                                                                                                        for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                                                                                                                            if (L2CacheState___req_reg.slave_index == 64'(i)) begin
                                                                                                                                send_slave_read_response(3'(64'(i)), L2CacheState___req_reg.slave_id, unsigned'(256'(L2CacheState___refill_data_reg)));
                                                                                                                            end
                                                                                                                        end
                                                                                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                                                    end
                                                                                                                    else begin
                                                                                                                        send_cpu_response(unsigned'(256'(L2CacheState___refill_data_reg)));
                                                                                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                                                    end
                                                                                                                end
                                                                                                                else begin
                                                                                                                    if (L2CacheState___state_reg == unsigned'(64'(L2CacheFsmState_pkg::ST_DONE))) begin
                                                                                                                        L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_IDLE;
                                                                                                                    end
                                                                                                                end
                                                                                                            end
                                                                                                        end
                                                                                                    end
                                                                                                end
                                                                                            end
                                                                                        end
                                                                                    end
                                                                                end
                                                                            end
                                                                        end
                                                                    end
                                                                end
                                                            end
                                                        end
                                                    end
                                                end
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
        if (reset) begin
            L2CacheState___state_reg_tmp = '0;
            L2CacheState___req_reg_tmp = '0;
            L2CacheState___request_pipe_reg_tmp = '0;
            L2CacheState___request_pipe_valid_reg_tmp = '0;
            L2CacheState___cpu_rr_reg_tmp = '0;
            L2CacheState___victim_reg_tmp = '0;
            L2CacheState___fill_way_reg_tmp = '0;
            L2CacheState___init_set_reg_tmp = '0;
            L2CacheState___cross_low_reg_tmp = '0;
            L2CacheState___cross_high_reg_tmp = '0;
            L2CacheState___refill_data_reg_tmp = '0;
            L2CacheState___fill_beat_reg_tmp = '0;
            L2CacheState___evict_beat_reg_tmp = '0;
            L2CacheState___evict_tag_reg_tmp = '0;
            L2CacheState___evict_line_reg_tmp = '0;
            for (i=32'h0;i < unsigned'(32'(RESPONSE_SLOTS));i=i+1) begin
                L2CacheState___response_reg[64'(i)].valid <= 0;
                L2CacheState___response_reg[64'(i)].read <= 0;
                L2CacheState___response_reg[64'(i)].write <= 0;
                L2CacheState___response_reg[64'(i)].data_port <= 0;
                L2CacheState___response_reg[64'(i)].addr <= 32'h0;
                L2CacheState___response_reg[64'(i)].b.valid<=0;
                L2CacheState___response_reg[64'(i)].b.id <= 64'h0;
                L2CacheState___response_reg[64'(i)].r.valid<=0;
                L2CacheState___response_reg[64'(i)].r.id <= 64'h0;
                L2CacheState___response_reg[64'(i)].r.data <= 'h0;
                L2CacheState___response_reg[64'(i)].r.last<=0;
            end
            for (i=32'h0;64'(i) < MEM_PORTS;i=i+1) begin
                L2CacheState___slave_aw_reg[64'(i)].valid<=0;
                L2CacheState___slave_aw_reg[64'(i)].addr <= 64'h0;
                L2CacheState___slave_aw_reg[64'(i)].id <= 64'h0;
                L2CacheState___slave_aw_seen_reg[64'(i)].valid<=0;
                L2CacheState___slave_aw_seen_reg[64'(i)].addr <= 64'h0;
                L2CacheState___slave_aw_seen_reg[64'(i)].id <= 64'h0;
                L2CacheState___slave_ar_seen_reg[64'(i)].valid<=0;
                L2CacheState___slave_ar_seen_reg[64'(i)].addr <= 64'h0;
                L2CacheState___slave_ar_seen_reg[64'(i)].id <= 64'h0;
            end
            L2CacheState___slave_aw_novelty_reg_tmp = '0;
            L2CacheState___slave_ar_novelty_reg_tmp = '0;
            L2CacheState___lookup_data_reg_tmp = '0;
            L2CacheState___lookup_tag_reg_tmp = '0;
            L2CacheState___lookup_hit_reg_tmp = '0;
            L2CacheState___lookup_evict_reg_tmp = '0;
            L2CacheState___lookup_write_pair_reg_tmp = '0;
            L2CacheState___state_reg_tmp = L2CacheFsmState_pkg::ST_INIT;
        end
    end
    endtask

    task _work_clk (input logic unused);
    begin: _work_clk
    end
    endtask

    always_ff @(posedge clk) begin

            _work_clk(reset);

    end

    always_ff @(posedge l2_clock) begin
        L2CacheState___lookup_data_reg_tmp = L2CacheState___lookup_data_reg;
        L2CacheState___lookup_tag_reg_tmp = L2CacheState___lookup_tag_reg;
        L2CacheState___lookup_hit_reg_tmp = L2CacheState___lookup_hit_reg;
        L2CacheState___lookup_evict_reg_tmp = L2CacheState___lookup_evict_reg;
        L2CacheState___lookup_write_pair_reg_tmp = L2CacheState___lookup_write_pair_reg;
        L2CacheState___state_reg_tmp = L2CacheState___state_reg;
        L2CacheState___req_reg_tmp = L2CacheState___req_reg;
        L2CacheState___request_pipe_reg_tmp = L2CacheState___request_pipe_reg;
        L2CacheState___request_pipe_valid_reg_tmp = L2CacheState___request_pipe_valid_reg;
        L2CacheState___cpu_rr_reg_tmp = L2CacheState___cpu_rr_reg;
        L2CacheState___victim_reg_tmp = L2CacheState___victim_reg;
        L2CacheState___fill_way_reg_tmp = L2CacheState___fill_way_reg;
        L2CacheState___init_set_reg_tmp = L2CacheState___init_set_reg;
        L2CacheState___cross_low_reg_tmp = L2CacheState___cross_low_reg;
        L2CacheState___cross_high_reg_tmp = L2CacheState___cross_high_reg;
        L2CacheState___refill_data_reg_tmp = L2CacheState___refill_data_reg;
        L2CacheState___fill_beat_reg_tmp = L2CacheState___fill_beat_reg;
        L2CacheState___evict_beat_reg_tmp = L2CacheState___evict_beat_reg;
        L2CacheState___evict_tag_reg_tmp = L2CacheState___evict_tag_reg;
        L2CacheState___evict_line_reg_tmp = L2CacheState___evict_line_reg;
        L2CacheState___slave_aw_novelty_reg_tmp = L2CacheState___slave_aw_novelty_reg;
        L2CacheState___slave_ar_novelty_reg_tmp = L2CacheState___slave_ar_novelty_reg;

            _work_l2_clock(reset);

        L2CacheState___lookup_data_reg <= L2CacheState___lookup_data_reg_tmp;
        L2CacheState___lookup_tag_reg <= L2CacheState___lookup_tag_reg_tmp;
        L2CacheState___lookup_hit_reg <= L2CacheState___lookup_hit_reg_tmp;
        L2CacheState___lookup_evict_reg <= L2CacheState___lookup_evict_reg_tmp;
        L2CacheState___lookup_write_pair_reg <= L2CacheState___lookup_write_pair_reg_tmp;
        L2CacheState___state_reg <= L2CacheState___state_reg_tmp;
        L2CacheState___req_reg <= L2CacheState___req_reg_tmp;
        L2CacheState___request_pipe_reg <= L2CacheState___request_pipe_reg_tmp;
        L2CacheState___request_pipe_valid_reg <= L2CacheState___request_pipe_valid_reg_tmp;
        L2CacheState___cpu_rr_reg <= L2CacheState___cpu_rr_reg_tmp;
        L2CacheState___victim_reg <= L2CacheState___victim_reg_tmp;
        L2CacheState___fill_way_reg <= L2CacheState___fill_way_reg_tmp;
        L2CacheState___init_set_reg <= L2CacheState___init_set_reg_tmp;
        L2CacheState___cross_low_reg <= L2CacheState___cross_low_reg_tmp;
        L2CacheState___cross_high_reg <= L2CacheState___cross_high_reg_tmp;
        L2CacheState___refill_data_reg <= L2CacheState___refill_data_reg_tmp;
        L2CacheState___fill_beat_reg <= L2CacheState___fill_beat_reg_tmp;
        L2CacheState___evict_beat_reg <= L2CacheState___evict_beat_reg_tmp;
        L2CacheState___evict_tag_reg <= L2CacheState___evict_tag_reg_tmp;
        L2CacheState___evict_line_reg <= L2CacheState___evict_line_reg_tmp;
        L2CacheState___slave_aw_novelty_reg <= L2CacheState___slave_aw_novelty_reg_tmp;
        L2CacheState___slave_ar_novelty_reg <= L2CacheState___slave_ar_novelty_reg_tmp;
    end


endmodule

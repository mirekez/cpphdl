`default_nettype none

import Predef_pkg::*;
import L2AxiRequestNoveltyComb_pkg::*;
import CacheRequest_pkg::*;
import L2ActiveRequestComb_pkg::*;
import L2RequestGeometryComb_pkg::*;
import L2HitLookupComb_pkg::*;
import L2EvictCandidateComb_pkg::*;
import L2WordPairComb_pkg::*;
import Axi4WriteResponse4_pkg::*;
import Axi4ReadData4_256_pkg::*;
import CacheResponse_pkg::*;
import Axi4WriteAddress32_4_pkg::*;
import Axi4ReadAddress32_4_pkg::*;


module L2CacheRequest #(
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
    L2AxiRequestNoveltyComb slave_request_novelty_comb;
    L2ActiveRequestComb active_request_comb;
    L2RequestGeometryComb request_geometry_comb;
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
    CacheResponse[16-1:0] L2CacheState___response_reg_tmp;
    logic[PORT_BITWIDTH-1:0] L2CacheState___cross_low_reg_tmp;
    logic[PORT_BITWIDTH-1:0] L2CacheState___cross_high_reg_tmp;
    logic[PORT_BITWIDTH-1:0] L2CacheState___refill_data_reg_tmp;
    logic[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] L2CacheState___fill_beat_reg_tmp;
    logic[64'(unsigned'(32'(((unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8))<=64'h1) ? (32'h1) : ($clog2(32'(unsigned'(64'(CACHE_LINE_SIZE))/((unsigned'(64'(PORT_BITWIDTH))/64'h8)))))))))-1:0] L2CacheState___evict_beat_reg_tmp;
    logic[(ADDR_BITS - unsigned'(64'($clog2(unsigned'(32'(((unsigned'(64'(CACHE_SIZE))/unsigned'(64'(CACHE_LINE_SIZE)))/unsigned'(64'(WAYS))))))))) - unsigned'(64'($clog2(unsigned'(32'(CACHE_LINE_SIZE)))))-1:0] L2CacheState___evict_tag_reg_tmp;
    logic[CACHE_LINE_SIZE*64'h8-1:0] L2CacheState___evict_line_reg_tmp;
    Axi4WriteAddress32_4[8-1:0] L2CacheState___slave_aw_reg_tmp;
    Axi4WriteAddress32_4[8-1:0] L2CacheState___slave_aw_seen_reg_tmp;
    Axi4ReadAddress32_4[8-1:0] L2CacheState___slave_ar_seen_reg_tmp;
    logic[8-1:0] L2CacheState___slave_aw_novelty_reg_tmp;
    logic[8-1:0] L2CacheState___slave_ar_novelty_reg_tmp;


    always_comb begin : slave_request_novelty_comb_func  // slave_request_novelty_comb_func
        logic[31:0] index;
        slave_request_novelty_comb = 0;
        for (index=32'h0;64'(index) < MEM_PORTS;index=index+1) begin
            slave_request_novelty_comb.aw[64'(index)] = L2CacheState___slave_aw_novelty_reg[64'(index)];
            slave_request_novelty_comb.ar[64'(index)] = L2CacheState___slave_ar_novelty_reg[64'(index)];
        end
    end

    always_comb begin : active_request_comb_func  // active_request_comb_func
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
        active_request_comb = 0;
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
            if (((((L2CacheState___slave_aw_reg[64'(port_index)].valid && axi_in__wvalid_in[port_index])) || (((axi_in__awvalid_in[port_index] && slave_request_novelty_comb.aw[64'(port_index)]) && axi_in__wvalid_in[port_index])))) && ((!L2CacheState___response_reg[64'(port_index)].b.valid || axi_in__bready_in[port_index]))) begin
                slave_write_pending=1;
            end
            if ((axi_in__arvalid_in[port_index] && slave_request_novelty_comb.ar[64'(port_index)]) && ((!L2CacheState___response_reg[64'(port_index)].r.valid || axi_in__rready_in[port_index]))) begin
                slave_read_pending=1;
            end
        end
        for (port_index=32'h0;64'(port_index) < MEM_PORTS;port_index=port_index+1) begin
            if (((!slave_write_pending && axi_in__arvalid_in[port_index]) && slave_request_novelty_comb.ar[64'(port_index)]) && ((!L2CacheState___response_reg[64'(port_index)].r.valid || axi_in__rready_in[port_index]))) begin
                selected_slave=port_index;
            end
            if (((((L2CacheState___slave_aw_reg[64'(port_index)].valid && axi_in__wvalid_in[port_index])) || (((axi_in__awvalid_in[port_index] && slave_request_novelty_comb.aw[64'(port_index)]) && axi_in__wvalid_in[port_index])))) && ((!L2CacheState___response_reg[64'(port_index)].b.valid || axi_in__bready_in[port_index]))) begin
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
        active_request_comb.request.from_slave = slave_write_pending || slave_read_pending;
        active_request_comb.request.cpu_index = 64'(selected_cpu);
        active_request_comb.request.port = (!((active_request_comb.request.from_slave) != '0) && cpu_request_pending) && ((d_mem_in__write_in[selected_cpu] || d_mem_in__read_in[selected_cpu]));
        active_request_comb.request.read = (((((active_request_comb.request.from_slave) != '0) && !slave_write_pending)) || (((!((active_request_comb.request.from_slave) != '0) && cpu_request_pending) && d_mem_in__read_in[selected_cpu]))) || (((((!((active_request_comb.request.from_slave) != '0) && cpu_request_pending) && !d_mem_in__write_in[selected_cpu]) && !d_mem_in__read_in[selected_cpu]) && i_mem_in__read_in[selected_cpu]));
        active_request_comb.request.write = (((((active_request_comb.request.from_slave) != '0) && slave_write_pending)) || (((!((active_request_comb.request.from_slave) != '0) && cpu_request_pending) && d_mem_in__write_in[selected_cpu]))) || (((((!((active_request_comb.request.from_slave) != '0) && cpu_request_pending) && !d_mem_in__read_in[selected_cpu]) && !d_mem_in__write_in[selected_cpu]) && i_mem_in__write_in[selected_cpu]));
        active_request_comb.request.addr = unsigned'(32'((((active_request_comb.request.port) != '0)) ? (d_mem_in__addr_in[selected_cpu]) : (i_mem_in__addr_in[selected_cpu])));
        active_request_comb.request.write_data = unsigned'(32'((((active_request_comb.request.port) != '0)) ? (d_mem_in__write_data_in[selected_cpu]) : (i_mem_in__write_data_in[selected_cpu])));
        active_request_comb.request.write_mask = unsigned'(8'((((active_request_comb.request.from_slave) != '0)) ? (8'hF) : (((((active_request_comb.request.port) != '0)) ? (d_mem_in__write_mask_in[selected_cpu]) : (i_mem_in__write_mask_in[selected_cpu])))));
        active_request_comb.request.cache_disable = !((active_request_comb.request.from_slave) != '0) && ((((active_request_comb.request.port) != '0)) ? (d_mem_in__cache_disable_in[selected_cpu]) : (i_mem_in__cache_disable_in[selected_cpu]));
        active_request_comb.request.slave_index = selected_slave;
        for (port_index=32'h0;64'(port_index) < MEM_PORTS;port_index=port_index+1) begin
            if (((active_request_comb.request.from_slave) != '0) && (selected_slave == port_index)) begin
                slave_addr=(slave_write_pending) ? (((L2CacheState___slave_aw_reg[64'(port_index)].valid) ? (unsigned'(32'(L2CacheState___slave_aw_reg[64'(port_index)].addr))) : (unsigned'(32'(axi_in__awaddr_in[port_index]))))) : (unsigned'(32'(axi_in__araddr_in[port_index])));
                active_request_comb.request.addr = unsigned'(32'((slave_addr < memory_base_in) ? (slave_addr + memory_base_in) : (slave_addr)));
                active_request_comb.request.slave_id = (slave_write_pending) ? (((L2CacheState___slave_aw_reg[64'(port_index)].valid) ? (L2CacheState___slave_aw_reg[64'(port_index)].id) : (axi_in__awid_in[port_index]))) : (axi_in__arid_in[port_index]);
                if (slave_write_pending) begin
                    lane=((64'(unsigned'(32'(((L2CacheState___slave_aw_reg[64'(port_index)].valid) ? (unsigned'(32'(L2CacheState___slave_aw_reg[64'(port_index)].addr))) : (unsigned'(32'(axi_in__awaddr_in[port_index]))))))) % unsigned'(64'(PORT_BYTES))))/64'h4;
                    active_request_comb.request.write_data = (axi_in__wdata_in[port_index] >> (lane*'h20));
                    active_request_comb.request.write_beat = axi_in__wdata_in[port_index];
                    active_request_comb.request.write_strobe = axi_in__wstrb_in[port_index];
                end
            end
        end
        if (!((active_request_comb.request.from_slave) != '0)) begin
            _byte=unsigned'(32'(active_request_comb.request.addr)) % 'h4;
            word=((64'(unsigned'(32'(active_request_comb.request.addr))) % unsigned'(64'(PORT_BYTES))))/64'h4;
            for (byte_index=64'h0;byte_index < 64'h4;byte_index=byte_index+1) begin
                if ((((active_request_comb.request.write_mask & 64'(32'((('h1 <<< byte_index)))))) != 64'h0) && ((64'(32'(((word*'h4) + _byte))) + byte_index) < unsigned'(64'(PORT_BYTES)))) begin
                    active_request_comb.request.write_strobe[64'(32'(((word*'h4) + _byte))) + byte_index] = 64'h1;
                end
            end
        end
        for (word_index=64'h0;word_index < unsigned'(64'(PORT_WORDS));word_index=word_index+1) begin
            for (byte_index=64'h0;byte_index < 64'h4;byte_index=byte_index+1) begin
                if (active_request_comb.request.write_strobe[(word_index*64'h4) + byte_index]) begin
                    active_request_comb.request.write_word_mask[word_index] = 64'h1;
                end
            end
        end
        active_request_comb.cache_set = 64'(32'(((unsigned'(32'(active_request_comb.request.addr)) >>> unsigned'(64'(LINE_BITS)))))) & ((SETS - 64'h1));
        _byte=unsigned'(32'(active_request_comb.request.addr)) & 'h3;
        word=64'(32'(((unsigned'(32'(active_request_comb.request.addr)) >>> 32'sh2)))) & ((LINE_WORDS - 64'h1));
        active_request_comb.valid = ((active_request_comb.request.read) != '0) || ((active_request_comb.request.write) != '0);
        active_request_comb.cross_line_read = (((((active_request_comb.request.read) != '0) && !((active_request_comb.request.from_slave) != '0)) && !((active_request_comb.request.port) != '0)) && (_byte != 32'h0)) && (64'(word) == (LINE_WORDS - 64'h1));
    end

    always_comb begin : request_geometry_comb_func  // request_geometry_comb_func
        logic[31:0] _byte;
        logic[31:0] word;
        logic[31:0] _local;
        logic[31:0] size;
        logic[31:0] i;
        request_geometry_comb = 0;
        _byte=unsigned'(32'(L2CacheState___req_reg.addr)) & 'h3;
        word=64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) >>> 32'sh2)))) & ((LINE_WORDS - 64'h1));
        _local=unsigned'(32'(L2CacheState___req_reg.addr)) - memory_base_in;
        size=memory_size_in;
        request_geometry_comb.set = 64'(32'(((unsigned'(32'(L2CacheState___req_reg.addr)) >>> unsigned'(64'(LINE_BITS)))))) & ((SETS - 64'h1));
        request_geometry_comb.word = word;
        request_geometry_comb.beat = ((64'(unsigned'(32'(L2CacheState___req_reg.addr))) & ((CACHE_LINE_SIZE - 64'h1))))/unsigned'(64'(PORT_BYTES));
        request_geometry_comb.tag = 32'(unsigned'(32'(L2CacheState___req_reg.addr)) >>> ((LINE_BITS + SET_BITS)));
        request_geometry_comb.cross_beat_read = ((((L2CacheState___req_reg.read) != '0) && !((L2CacheState___req_reg.from_slave) != '0)) && (_byte != 32'h0)) && (((((64'(unsigned'(32'(L2CacheState___req_reg.addr))) % unsigned'(64'(PORT_BYTES))))/64'h4)) + 64'h1)>=unsigned'(64'(PORT_WORDS));
        request_geometry_comb.cross_write_data = unsigned'(32'((_byte == 32'h0) ? (32'h0) : (unsigned'(32'(L2CacheState___req_reg.write_data)) >>> ((32'h20 - (_byte*'h8))))));
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (((((L2CacheState___req_reg.write_mask & 64'(32'((('h1 <<< i))))))) != '0) && (i + _byte)>=32'h4) begin
                request_geometry_comb.cross_write_mask |= 8'(64'(32'('h1 <<< (((i + _byte) - 32'h4)))));
                if ((((L2CacheState___req_reg.write) != '0) && (_byte != 32'h0)) && (64'(word) == (LINE_WORDS - 64'h1))) begin
                    request_geometry_comb.cross_line_write = 1;
                end
            end
        end
        request_geometry_comb.addr_in_memory = (L2CacheState___req_reg.addr>=memory_base_in && (size != 32'h0)) && (_local < size);
    end

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

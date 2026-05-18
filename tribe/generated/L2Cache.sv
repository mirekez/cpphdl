`default_nettype none

import Predef_pkg::*;


module L2Cache #(
    parameter CACHE_SIZE
,   parameter PORT_BITWIDTH
,   parameter CACHE_LINE_SIZE
,   parameter WAYS
,   parameter ADDR_BITS
,   parameter MEM_ADDR_BITS
,   parameter MEM_PORTS
 )
 (
    input wire clk
,   input wire reset
,   input wire i_read_in
,   input wire i_write_in
,   input wire[31:0] i_addr_in
,   input wire[31:0] i_write_data_in
,   input wire[7:0] i_write_mask_in
,   output wire[PORT_BITWIDTH-1:0] i_read_data_out
,   output wire i_wait_out
,   input wire d_read_in
,   input wire d_write_in
,   input wire[31:0] d_addr_in
,   input wire[31:0] d_write_data_in
,   input wire[7:0] d_write_mask_in
,   output wire[PORT_BITWIDTH-1:0] d_read_data_out
,   output wire d_wait_out
,   input wire[31:0] memory_base_in
,   input wire[31:0] memory_size_in
,   input wire[31:0] mem_region_size_in[MEM_PORTS]
,   input wire mem_region_uncached_in[MEM_PORTS]
,   input wire axi_in__awvalid_in[MEM_PORTS]
,   output wire axi_in__awready_out[MEM_PORTS]
,   input wire[MEM_ADDR_BITS-1:0] axi_in__awaddr_in[MEM_PORTS]
,   input wire['h4-1:0] axi_in__awid_in[MEM_PORTS]
,   input wire axi_in__wvalid_in[MEM_PORTS]
,   output wire axi_in__wready_out[MEM_PORTS]
,   input wire[PORT_BITWIDTH-1:0] axi_in__wdata_in[MEM_PORTS]
,   input wire axi_in__wlast_in[MEM_PORTS]
,   output wire axi_in__bvalid_out[MEM_PORTS]
,   input wire axi_in__bready_in[MEM_PORTS]
,   output wire['h4-1:0] axi_in__bid_out[MEM_PORTS]
,   input wire axi_in__arvalid_in[MEM_PORTS]
,   output wire axi_in__arready_out[MEM_PORTS]
,   input wire[MEM_ADDR_BITS-1:0] axi_in__araddr_in[MEM_PORTS]
,   input wire['h4-1:0] axi_in__arid_in[MEM_PORTS]
,   output wire axi_in__rvalid_out[MEM_PORTS]
,   input wire axi_in__rready_in[MEM_PORTS]
,   output wire[PORT_BITWIDTH-1:0] axi_in__rdata_out[MEM_PORTS]
,   output wire axi_in__rlast_out[MEM_PORTS]
,   output wire['h4-1:0] axi_in__rid_out[MEM_PORTS]
,   output wire axi_out__awvalid_out[MEM_PORTS]
,   input wire axi_out__awready_in[MEM_PORTS]
,   output wire[MEM_ADDR_BITS-1:0] axi_out__awaddr_out[MEM_PORTS]
,   output wire['h4-1:0] axi_out__awid_out[MEM_PORTS]
,   output wire axi_out__wvalid_out[MEM_PORTS]
,   input wire axi_out__wready_in[MEM_PORTS]
,   output wire[PORT_BITWIDTH-1:0] axi_out__wdata_out[MEM_PORTS]
,   output wire axi_out__wlast_out[MEM_PORTS]
,   input wire axi_out__bvalid_in[MEM_PORTS]
,   output wire axi_out__bready_out[MEM_PORTS]
,   input wire['h4-1:0] axi_out__bid_in[MEM_PORTS]
,   output wire axi_out__arvalid_out[MEM_PORTS]
,   input wire axi_out__arready_in[MEM_PORTS]
,   output wire[MEM_ADDR_BITS-1:0] axi_out__araddr_out[MEM_PORTS]
,   output wire['h4-1:0] axi_out__arid_out[MEM_PORTS]
,   input wire axi_out__rvalid_in[MEM_PORTS]
,   output wire axi_out__rready_out[MEM_PORTS]
,   input wire[PORT_BITWIDTH-1:0] axi_out__rdata_in[MEM_PORTS]
,   input wire axi_out__rlast_in[MEM_PORTS]
,   input wire['h4-1:0] axi_out__rid_in[MEM_PORTS]
,   input wire debugen_in
);
    parameter  LINE_WORDS = CACHE_LINE_SIZE/'h4;
    parameter  PORT_BYTES = PORT_BITWIDTH/'h8;
    parameter  PORT_WORDS = PORT_BITWIDTH/'h20;
    parameter  LINE_BEATS = CACHE_LINE_SIZE/PORT_BYTES;
    parameter  LINE_BEAT_BITS = (LINE_BEATS<='h1) ? ('h1) : ($clog2(LINE_BEATS));
    parameter  SETS = (CACHE_SIZE/CACHE_LINE_SIZE)/WAYS;
    parameter  SET_BITS = $clog2(SETS);
    parameter  LINE_BITS = $clog2(CACHE_LINE_SIZE);
    parameter  WORD_BITS = $clog2(LINE_WORDS);
    parameter  WAY_BITS = (WAYS<='h1) ? ('h1) : ($clog2(WAYS));
    parameter  TAG_BITS = (ADDR_BITS - SET_BITS) - LINE_BITS;
    parameter  DATA_BANKS = WAYS*LINE_WORDS;
    parameter  MEM_PORT_BITS = $clog2(MEM_PORTS);
    parameter  MEM_ADDR_MASK64 = ((MEM_ADDR_BITS>='h40)) ? (~'h0) : (((('h1 <<< MEM_ADDR_BITS)) - 'h1));
    parameter  ST_IDLE = 'h0;
    parameter  ST_INIT = 'h1;
    parameter  ST_LOOKUP = 'h2;
    parameter  ST_AXI_AR = 'h3;
    parameter  ST_AXI_R = 'h4;
    parameter  ST_DONE = 'h5;
    parameter  ST_CROSS_AR0 = 'h6;
    parameter  ST_CROSS_R0 = 'h7;
    parameter  ST_CROSS_AR1 = 'h8;
    parameter  ST_CROSS_R1 = 'h9;
    parameter  ST_EVICT_AW = 'hA;
    parameter  ST_EVICT_W = 'hB;
    parameter  ST_EVICT_B = 'hC;
    parameter  ST_CROSS_WRITE_LOOKUP = 'hD;
    parameter  ST_CROSS_DONE = 'hE;
    parameter  ST_IO_AW = 'hF;
    parameter  ST_IO_W = 'h10;
    parameter  ST_IO_B = 'h11;
    parameter  ST_IO_AR = 'h12;
    parameter  ST_IO_R = 'h13;


    // regs and combs
    reg[5-1:0] state_reg;
    reg[32-1:0] req_addr_reg;
    reg[32-1:0] req_write_data_reg;
    reg[8-1:0] req_write_mask_reg;
    reg req_read_reg;
    reg req_write_reg;
    reg req_port_reg;
    reg req_from_slave_reg;
    reg[MEM_PORT_BITS-1:0] req_slave_index_reg;
    reg[4-1:0] req_slave_id_reg;
    reg[WAY_BITS-1:0] victim_reg;
    reg[WAY_BITS-1:0] fill_way_reg;
    reg[SET_BITS-1:0] init_set_reg;
    reg[PORT_BITWIDTH-1:0] last_data_reg;
    reg[PORT_BITWIDTH-1:0] cross_low_reg;
    reg[PORT_BITWIDTH-1:0] cross_high_reg;
    reg[LINE_BEAT_BITS-1:0] fill_beat_reg;
    reg[LINE_BEAT_BITS-1:0] evict_beat_reg;
    reg[MEM_PORTS-1:0] slave_bvalid_reg;
    reg[MEM_PORTS-1:0][4-1:0] slave_bid_reg;
    reg[MEM_PORTS-1:0] slave_rvalid_reg;
    reg[MEM_PORTS-1:0][4-1:0] slave_rid_reg;
    reg[MEM_PORTS-1:0][PORT_BITWIDTH-1:0] slave_rdata_reg;
    reg[MEM_PORTS-1:0] slave_aw_pending_reg;
    reg[MEM_PORTS-1:0][MEM_ADDR_BITS-1:0] slave_awaddr_reg;
    reg[MEM_PORTS-1:0][4-1:0] slave_awid_reg;
    logic slave_write_pending_comb;
;
    logic slave_read_pending_comb;
;
    logic[MEM_PORT_BITS-1:0] active_slave_index_comb;
;
    logic active_is_slave_comb;
;
    logic active_is_d_comb;
;
    logic active_read_comb;
;
    logic active_write_comb;
;
    logic[31:0] active_addr_comb;
;
    logic[31:0] active_write_data_comb;
;
    logic[7:0] active_write_mask_comb;
;
    logic[SET_BITS-1:0] req_set_comb;
;
    logic[SET_BITS-1:0] active_set_comb;
;
    logic[WORD_BITS-1:0] req_word_comb;
;
    logic[LINE_BEAT_BITS-1:0] req_beat_comb;
;
    logic[TAG_BITS-1:0] req_tag_comb;
;
    logic active_cross_line_read_comb;
;
    logic req_cross_line_write_comb;
;
    logic[31:0] cross_write_data_comb;
;
    logic[7:0] cross_write_mask_comb;
;
    logic addr_in_memory_comb;
;
    logic req_addr_in_memory_comb;
;
    logic[31:0] axi_araddr_full_comb;
;
    logic[31:0] axi_araddr_total_local_comb;
;
    logic[31:0] axi_ar_sel_comb;
;
    logic[31:0] axi_ar_region_base_comb;
;
    logic[MEM_ADDR_BITS-1:0] axi_araddr_local_comb;
;
    logic axi_arvalid_comb;
;
    logic axi_rready_comb;
;
    logic axi_arready_selected_comb;
;
    logic axi_rvalid_selected_comb;
;
    logic[PORT_BITWIDTH-1:0] axi_rdata_selected_comb;
;
    logic[WAY_BITS-1:0] evict_way_comb;
;
    logic evict_valid_comb;
;
    logic evict_dirty_comb;
;
    logic[TAG_BITS-1:0] evict_tag_comb;
;
    logic[31:0] axi_awaddr_full_comb;
;
    logic[31:0] axi_awaddr_total_local_comb;
;
    logic[31:0] axi_aw_sel_comb;
;
    logic[31:0] axi_aw_region_base_comb;
;
    logic[MEM_ADDR_BITS-1:0] axi_awaddr_local_comb;
;
    logic axi_awvalid_comb;
;
    logic axi_wvalid_comb;
;
    logic axi_awready_selected_comb;
;
    logic axi_wready_selected_comb;
;
    logic axi_bvalid_selected_comb;
;
    logic[PORT_BITWIDTH-1:0] evict_line_comb;
;
    logic req_uncached_region_comb;
;
    logic[PORT_BITWIDTH-1:0] io_write_beat_comb;
;
    logic[PORT_BITWIDTH-1:0] axi_wdata_comb;
;
    logic hit_comb;
;
    logic[WAY_BITS-1:0] hit_way_comb;
;
    logic[31:0] hit_aligned_word_comb;
;
    logic[31:0] hit_aligned_next_word_comb;
;
    logic[31:0] hit_word_comb;
;
    logic[31:0] write_word_comb;
;
    logic[31:0] write_next_word_comb;
;
    logic[31:0] axi_aligned_word_comb;
;
    logic[31:0] fill_write_word_comb;
;
    logic[31:0] fill_write_next_word_comb;
;
    logic[PORT_BITWIDTH-1:0] hit_beat_comb;
;
    logic[PORT_BITWIDTH-1:0] cross_read_data_comb;
;
    logic[TAG_BITS + 'h2-1:0] tag_write_data_comb;
;
    logic[PORT_BITWIDTH-1:0] read_data_comb;
;
    logic i_wait_comb;
;
    logic d_wait_comb;
;

    // members
    genvar __i;
      wire[$clog2((SETS))-1:0] data_ram__addr_in[DATA_BANKS];
      wire[('h20)-1:0] data_ram__data_in[DATA_BANKS];
      wire data_ram__wr_in[DATA_BANKS];
      wire data_ram__rd_in[DATA_BANKS];
      wire[('h20)-1:0] data_ram__q_out[DATA_BANKS];
      wire signed[31:0] data_ram__id_in[DATA_BANKS];
    generate
    for (__i=0; __i < DATA_BANKS; __i = __i + 1) begin
        RAM1PORT #(
        'h20
,       SETS
        ) data_ram (
            .clk(clk)
        ,           .reset(reset)
        ,           .addr_in(data_ram__addr_in[__i])
        ,           .data_in(data_ram__data_in[__i])
        ,           .wr_in(data_ram__wr_in[__i])
        ,           .rd_in(data_ram__rd_in[__i])
        ,           .q_out(data_ram__q_out[__i])
        ,           .id_in(data_ram__id_in[__i])
        );
    end
    endgenerate
      wire[$clog2((SETS))-1:0] tag_ram__addr_in[WAYS];
      wire[(TAG_BITS + 'h2)-1:0] tag_ram__data_in[WAYS];
      wire tag_ram__wr_in[WAYS];
      wire tag_ram__rd_in[WAYS];
      wire[(TAG_BITS + 'h2)-1:0] tag_ram__q_out[WAYS];
      wire signed[31:0] tag_ram__id_in[WAYS];
    generate
    for (__i=0; __i < WAYS; __i = __i + 1) begin
        RAM1PORT #(
        TAG_BITS + 'h2
,       SETS
        ) tag_ram (
            .clk(clk)
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
    logic[5-1:0] state_reg_tmp;
    logic[32-1:0] req_addr_reg_tmp;
    logic[32-1:0] req_write_data_reg_tmp;
    logic[8-1:0] req_write_mask_reg_tmp;
    logic req_read_reg_tmp;
    logic req_write_reg_tmp;
    logic req_port_reg_tmp;
    logic req_from_slave_reg_tmp;
    logic[MEM_PORT_BITS-1:0] req_slave_index_reg_tmp;
    logic[4-1:0] req_slave_id_reg_tmp;
    logic[WAY_BITS-1:0] victim_reg_tmp;
    logic[WAY_BITS-1:0] fill_way_reg_tmp;
    logic[SET_BITS-1:0] init_set_reg_tmp;
    logic[PORT_BITWIDTH-1:0] last_data_reg_tmp;
    logic[PORT_BITWIDTH-1:0] cross_low_reg_tmp;
    logic[PORT_BITWIDTH-1:0] cross_high_reg_tmp;
    logic[LINE_BEAT_BITS-1:0] fill_beat_reg_tmp;
    logic[LINE_BEAT_BITS-1:0] evict_beat_reg_tmp;
    logic[MEM_PORTS-1:0] slave_bvalid_reg_tmp;
    logic[MEM_PORTS-1:0][4-1:0] slave_bid_reg_tmp;
    logic[MEM_PORTS-1:0] slave_rvalid_reg_tmp;
    logic[MEM_PORTS-1:0][4-1:0] slave_rid_reg_tmp;
    logic[MEM_PORTS-1:0][PORT_BITWIDTH-1:0] slave_rdata_reg_tmp;
    logic[MEM_PORTS-1:0] slave_aw_pending_reg_tmp;
    logic[MEM_PORTS-1:0][MEM_ADDR_BITS-1:0] slave_awaddr_reg_tmp;
    logic[MEM_PORTS-1:0][4-1:0] slave_awid_reg_tmp;


    always_comb begin : slave_write_pending_comb_func  // slave_write_pending_comb_func
        logic[63:0] i;
        slave_write_pending_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (((((slave_aw_pending_reg[i] && axi_in__wvalid_in[i])) || ((axi_in__awvalid_in[i] && axi_in__wvalid_in[i])))) && !slave_bvalid_reg[i]) begin
                slave_write_pending_comb=1;
            end
        end
    end

    always_comb begin : slave_read_pending_comb_func  // slave_read_pending_comb_func
        logic[63:0] i;
        slave_read_pending_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_in__arvalid_in[i] && !slave_rvalid_reg[i]) begin
                slave_read_pending_comb=1;
            end
        end
    end

    always_comb begin : active_slave_index_comb_func  // active_slave_index_comb_func
        logic[63:0] i;
        active_slave_index_comb = 'h0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            logic[32-1:0] port_index; port_index = unsigned'(32'(unsigned'(32'(unsigned'(32'(i))))));
            if ((!slave_write_pending_comb && axi_in__arvalid_in[i]) && !slave_rvalid_reg[i]) begin
                active_slave_index_comb = port_index;
            end
            if (((((slave_aw_pending_reg[i] && axi_in__wvalid_in[i])) || ((axi_in__awvalid_in[i] && axi_in__wvalid_in[i])))) && !slave_bvalid_reg[i]) begin
                active_slave_index_comb = port_index;
            end
        end
    end

    always_comb begin : active_is_slave_comb_func  // active_is_slave_comb_func
        active_is_slave_comb=slave_write_pending_comb || slave_read_pending_comb;
    end

    always_comb begin : active_is_d_comb_func  // active_is_d_comb_func
        active_is_d_comb=!active_is_slave_comb && ((d_write_in || d_read_in));
    end

    always_comb begin : active_read_comb_func  // active_read_comb_func
        active_read_comb=(((active_is_slave_comb && !slave_write_pending_comb)) || ((!active_is_slave_comb && d_read_in))) || (((((!d_write_in && !d_read_in) && !slave_write_pending_comb) && !slave_read_pending_comb) && i_read_in));
    end

    always_comb begin : active_write_comb_func  // active_write_comb_func
        active_write_comb=(((active_is_slave_comb && slave_write_pending_comb)) || ((!active_is_slave_comb && d_write_in))) || (((((!d_read_in && !d_write_in) && !slave_write_pending_comb) && !slave_read_pending_comb) && i_write_in));
    end

    always_comb begin : active_addr_comb_func  // active_addr_comb_func
        logic[63:0] i;
        active_addr_comb=(active_is_d_comb) ? (d_addr_in) : (i_addr_in);
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (active_is_slave_comb && (active_slave_index_comb == i)) begin
                active_addr_comb=(slave_write_pending_comb) ? (((slave_aw_pending_reg[i]) ? (unsigned'(32'(slave_awaddr_reg[i]))) : (unsigned'(32'(axi_in__awaddr_in[i]))))) : (unsigned'(32'(axi_in__araddr_in[i])));
            end
        end
    end

    always_comb begin : active_write_data_comb_func  // active_write_data_comb_func
        logic[63:0] i;
        logic[31:0] lane;
        lane='h0;
        active_write_data_comb=(active_is_d_comb) ? (d_write_data_in) : (i_write_data_in);
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if ((active_is_slave_comb && slave_write_pending_comb) && (active_slave_index_comb == i)) begin
                lane=((((slave_aw_pending_reg[i]) ? (unsigned'(32'(slave_awaddr_reg[i]))) : (unsigned'(32'(axi_in__awaddr_in[i])))) % PORT_BYTES))/'h4;
                active_write_data_comb=unsigned'(32'(axi_in__wdata_in[i][lane*'h20 +:32]));
            end
        end
    end

    always_comb begin : active_write_mask_comb_func  // active_write_mask_comb_func
        active_write_mask_comb=(active_is_slave_comb) ? (unsigned'(8'('hF))) : (((active_is_d_comb) ? (d_write_mask_in) : (i_write_mask_in)));
    end

    always_comb begin : req_set_comb_func  // req_set_comb_func
        req_set_comb = unsigned'(SET_BITS'(unsigned'(SET_BITS'((unsigned'(32'(req_addr_reg)) >>> LINE_BITS)))));
    end

    always_comb begin : active_set_comb_func  // active_set_comb_func
        active_set_comb = unsigned'(SET_BITS'(unsigned'(SET_BITS'((active_addr_comb >>> LINE_BITS)))));
    end

    always_comb begin : req_word_comb_func  // req_word_comb_func
        req_word_comb = unsigned'(WORD_BITS'(unsigned'(WORD_BITS'((((unsigned'(32'(req_addr_reg)) >>> 'h2)) & ((LINE_WORDS - 'h1)))))));
    end

    always_comb begin : req_beat_comb_func  // req_beat_comb_func
        req_beat_comb = unsigned'(LINE_BEAT_BITS'(unsigned'(LINE_BEAT_BITS'((((unsigned'(32'(req_addr_reg)) & ((CACHE_LINE_SIZE - 'h1))))/PORT_BYTES)))));
    end

    always_comb begin : req_tag_comb_func  // req_tag_comb_func
        req_tag_comb = unsigned'(TAG_BITS'(unsigned'(TAG_BITS'((unsigned'(32'(req_addr_reg)) >>> ((LINE_BITS + SET_BITS)))))));
    end

    always_comb begin : active_cross_line_read_comb_func  // active_cross_line_read_comb_func
        logic[31:0] _byte;
        logic[31:0] word;
        _byte=active_addr_comb & 'h3;
        word=((active_addr_comb >>> 'h2)) & ((LINE_WORDS - 'h1));
        active_cross_line_read_comb=(((active_read_comb && !active_is_slave_comb) && !active_is_d_comb) && (_byte != 'h0)) && (word == (LINE_WORDS - 'h1));
    end

    always_comb begin : req_cross_line_write_comb_func  // req_cross_line_write_comb_func
        logic[31:0] _byte;
        logic[31:0] word;
        logic[31:0] i;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        word=((unsigned'(32'(req_addr_reg)) >>> 'h2)) & ((LINE_WORDS - 'h1));
        req_cross_line_write_comb=0;
        if ((req_write_reg && (_byte != 'h0)) && (word == (LINE_WORDS - 'h1))) begin
            for (i='h0;i < 'h4;i=i+1) begin
                if (((req_write_mask_reg & (('h1 <<< i)))) && (i + _byte)>='h4) begin
                    req_cross_line_write_comb=1;
                end
            end
        end
    end

    always_comb begin : cross_write_data_comb_func  // cross_write_data_comb_func
        logic[31:0] _byte;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        cross_write_data_comb=(_byte == 'h0) ? (unsigned'(32'('h0))) : (unsigned'(32'(req_write_data_reg)) >>> (('h20 - (_byte*'h8))));
    end

    always_comb begin : cross_write_mask_comb_func  // cross_write_mask_comb_func
        logic[31:0] _byte;
        logic[31:0] i;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        cross_write_mask_comb='h0;
        for (i='h0;i < 'h4;i=i+1) begin
            if (((req_write_mask_reg & (('h1 <<< i)))) && (i + _byte)>='h4) begin
                cross_write_mask_comb|='h1 <<< (((i + _byte) - 'h4));
            end
        end
    end

    always_comb begin : req_addr_in_memory_comb_func  // req_addr_in_memory_comb_func
        logic[31:0] addr;
        logic[31:0] _local;
        logic[31:0] size;
        addr=req_addr_reg;
        _local=addr - memory_base_in;
        size=memory_size_in;
        req_addr_in_memory_comb=(addr>=memory_base_in && (size != 'h0)) && (_local < size);
    end

    always_comb begin : axi_araddr_full_comb_func  // axi_araddr_full_comb_func
        logic[31:0] line_addr;
        line_addr=((unsigned'(32'(req_addr_reg)) & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) + ((unsigned'(32'(fill_beat_reg))*PORT_BYTES));
        if ((state_reg == ST_IO_AR) || (state_reg == ST_IO_R)) begin
            line_addr=req_addr_reg;
        end
        if ((state_reg == ST_CROSS_AR0) || (state_reg == ST_CROSS_R0)) begin
            line_addr=((unsigned'(32'(req_addr_reg)) & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) + ((unsigned'(32'(req_beat_comb))*PORT_BYTES));
        end
        if ((state_reg == ST_CROSS_AR1) || (state_reg == ST_CROSS_R1)) begin
            line_addr=((unsigned'(32'(req_addr_reg)) & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) + CACHE_LINE_SIZE;
        end
        axi_araddr_full_comb=line_addr;
    end

    always_comb begin : axi_araddr_total_local_comb_func  // axi_araddr_total_local_comb_func
        axi_araddr_total_local_comb=axi_araddr_full_comb - memory_base_in;
    end

    always_comb begin : axi_ar_sel_comb_func  // axi_ar_sel_comb_func
        logic[63:0] i;
        logic[63:0] base;
        logic[63:0] _local;
        _local=axi_araddr_total_local_comb;
        base='h0;
        axi_ar_sel_comb=MEM_PORTS - 'h1;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (_local>=base && (_local < (base + mem_region_size_in[i]))) begin
                axi_ar_sel_comb=i;
            end
            base+=mem_region_size_in[i];
        end
    end

    always_comb begin : axi_ar_region_base_comb_func  // axi_ar_region_base_comb_func
        logic[63:0] i;
        logic[63:0] base;
        base='h0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (i < axi_ar_sel_comb) begin
                base+=mem_region_size_in[i];
            end
        end
        axi_ar_region_base_comb=base;
    end

    always_comb begin : axi_araddr_local_comb_func  // axi_araddr_local_comb_func
        axi_araddr_local_comb = unsigned'(MEM_ADDR_BITS'(unsigned'(MEM_ADDR_BITS'((unsigned'(64'(((axi_araddr_total_local_comb - axi_ar_region_base_comb)))) & MEM_ADDR_MASK64)))));
    end

    always_comb begin : axi_arvalid_comb_func  // axi_arvalid_comb_func
        axi_arvalid_comb=req_addr_in_memory_comb && (((((state_reg == ST_AXI_AR) || (state_reg == ST_CROSS_AR0)) || (state_reg == ST_CROSS_AR1)) || (state_reg == ST_IO_AR)));
    end

    always_comb begin : axi_rready_comb_func  // axi_rready_comb_func
        axi_rready_comb=(((state_reg == ST_AXI_R) || (state_reg == ST_CROSS_R0)) || (state_reg == ST_CROSS_R1)) || (state_reg == ST_IO_R);
    end

    always_comb begin : axi_arready_selected_comb_func  // axi_arready_selected_comb_func
        logic[63:0] i;
        axi_arready_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_ar_sel_comb == i) begin
                axi_arready_selected_comb=axi_out__arready_in[i];
            end
        end
    end

    always_comb begin : axi_rvalid_selected_comb_func  // axi_rvalid_selected_comb_func
        logic[63:0] i;
        axi_rvalid_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_ar_sel_comb == i) begin
                axi_rvalid_selected_comb=axi_out__rvalid_in[i];
            end
        end
    end

    always_comb begin : axi_rdata_selected_comb_func  // axi_rdata_selected_comb_func
        logic[63:0] i;
        axi_rdata_selected_comb = 'h0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_ar_sel_comb == i) begin
                axi_rdata_selected_comb = axi_out__rdata_in[i];
            end
        end
    end

    always_comb begin : evict_way_comb_func  // evict_way_comb_func
        evict_way_comb = ((state_reg == ST_LOOKUP)) ? (victim_reg) : (fill_way_reg);
    end

    always_comb begin : evict_valid_comb_func  // evict_valid_comb_func
        logic valid;
        logic[63:0] i;
        valid=0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (evict_way_comb == i) begin
                valid=tag_ram__q_out[i][TAG_BITS + 'h1];
            end
        end
        evict_valid_comb=valid;
    end

    always_comb begin : evict_dirty_comb_func  // evict_dirty_comb_func
        logic dirty;
        logic[63:0] i;
        dirty=0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (evict_way_comb == i) begin
                dirty=tag_ram__q_out[i][TAG_BITS];
            end
        end
        evict_dirty_comb=dirty;
    end

    always_comb begin : evict_tag_comb_func  // evict_tag_comb_func
        logic[63:0] i;
        evict_tag_comb = 'h0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (evict_way_comb == i) begin
                evict_tag_comb = unsigned'(64'(tag_ram__q_out[i]['h0 +:TAG_BITS - 'h1 - 'h0 + 1]));
            end
        end
    end

    always_comb begin : axi_awaddr_full_comb_func  // axi_awaddr_full_comb_func
        logic[31:0] addr;
        addr=((((unsigned'(32'(evict_tag_comb)) <<< ((SET_BITS + LINE_BITS)))) | ((unsigned'(32'(req_set_comb)) <<< LINE_BITS)))) + ((unsigned'(32'(evict_beat_reg))*PORT_BYTES));
        if (((state_reg == ST_IO_AW) || (state_reg == ST_IO_W)) || (state_reg == ST_IO_B)) begin
            addr=req_addr_reg;
        end
        axi_awaddr_full_comb=addr;
    end

    always_comb begin : axi_awaddr_total_local_comb_func  // axi_awaddr_total_local_comb_func
        axi_awaddr_total_local_comb=axi_awaddr_full_comb - memory_base_in;
    end

    always_comb begin : axi_aw_sel_comb_func  // axi_aw_sel_comb_func
        logic[63:0] i;
        logic[63:0] base;
        logic[63:0] _local;
        _local=axi_awaddr_total_local_comb;
        base='h0;
        axi_aw_sel_comb=MEM_PORTS - 'h1;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (_local>=base && (_local < (base + mem_region_size_in[i]))) begin
                axi_aw_sel_comb=i;
            end
            base+=mem_region_size_in[i];
        end
    end

    always_comb begin : axi_aw_region_base_comb_func  // axi_aw_region_base_comb_func
        logic[63:0] i;
        logic[63:0] base;
        base='h0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (i < axi_aw_sel_comb) begin
                base+=mem_region_size_in[i];
            end
        end
        axi_aw_region_base_comb=base;
    end

    always_comb begin : axi_awaddr_local_comb_func  // axi_awaddr_local_comb_func
        axi_awaddr_local_comb = unsigned'(MEM_ADDR_BITS'(unsigned'(MEM_ADDR_BITS'((unsigned'(64'(((axi_awaddr_total_local_comb - axi_aw_region_base_comb)))) & MEM_ADDR_MASK64)))));
    end

    always_comb begin : axi_awvalid_comb_func  // axi_awvalid_comb_func
        axi_awvalid_comb=req_addr_in_memory_comb && (((state_reg == ST_EVICT_AW) || (state_reg == ST_IO_AW)));
    end

    always_comb begin : axi_wvalid_comb_func  // axi_wvalid_comb_func
        axi_wvalid_comb=req_addr_in_memory_comb && (((state_reg == ST_EVICT_W) || (state_reg == ST_IO_W)));
    end

    always_comb begin : axi_awready_selected_comb_func  // axi_awready_selected_comb_func
        logic[63:0] i;
        axi_awready_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_aw_sel_comb == i) begin
                axi_awready_selected_comb=axi_out__awready_in[i];
            end
        end
    end

    always_comb begin : axi_wready_selected_comb_func  // axi_wready_selected_comb_func
        logic[63:0] i;
        axi_wready_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_aw_sel_comb == i) begin
                axi_wready_selected_comb=axi_out__wready_in[i];
            end
        end
    end

    always_comb begin : axi_bvalid_selected_comb_func  // axi_bvalid_selected_comb_func
        logic[63:0] i;
        axi_bvalid_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_aw_sel_comb == i) begin
                axi_bvalid_selected_comb=axi_out__bvalid_in[i];
            end
        end
    end

    always_comb begin : evict_line_comb_func  // evict_line_comb_func
        logic[63:0] i;
        logic[63:0] way;
        logic[63:0] word;
        logic[63:0] beat_word;
        way='h0;
        word='h0;
        beat_word='h0;
        evict_line_comb = 'h0;
        for (i='h0;i < DATA_BANKS;i=i+1) begin
            way=i/LINE_WORDS;
            word=i % LINE_WORDS;
            if (((evict_way_comb == way) && word>=(unsigned'(32'(evict_beat_reg))*PORT_WORDS)) && (word < (((unsigned'(32'(evict_beat_reg)) + 'h1))*PORT_WORDS))) begin
                beat_word=word - (unsigned'(32'(evict_beat_reg))*PORT_WORDS);
                evict_line_comb[beat_word*'h20 +:32] = data_ram__q_out[i];
            end
        end
    end

    always_comb begin : req_uncached_region_comb_func  // req_uncached_region_comb_func
        logic[31:0] _local;
        logic[63:0] base;
        logic[63:0] i;
        _local=unsigned'(32'(req_addr_reg)) - memory_base_in;
        base='h0;
        req_uncached_region_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (_local>=base && (unsigned'(64'(_local)) < (base + mem_region_size_in[i]))) begin
                req_uncached_region_comb=mem_region_uncached_in[i];
            end
            base+=mem_region_size_in[i];
        end
        req_uncached_region_comb=req_addr_in_memory_comb && req_uncached_region_comb;
    end

    always_comb begin : io_write_beat_comb_func  // io_write_beat_comb_func
        logic[31:0] _byte;
        logic[31:0] word;
        io_write_beat_comb = 'h0;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        word=((unsigned'(32'(req_addr_reg)) % PORT_BYTES))/'h4;
        io_write_beat_comb[word*'h20 +:32] = unsigned'(32'(req_write_data_reg)) <<< ((_byte*'h8));
    end

    always_comb begin : axi_wdata_comb_func  // axi_wdata_comb_func
        axi_wdata_comb = (state_reg == ST_IO_W) ? (io_write_beat_comb) : (evict_line_comb);
    end

    always_comb begin : hit_comb_func  // hit_comb_func
        logic[63:0] i;
        hit_comb=0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (tag_ram__q_out[i][(TAG_BITS + 'h1)] && (tag_ram__q_out[i]['h0 +:(TAG_BITS - 'h1) - 'h0 + 1] == req_tag_comb)) begin
                hit_comb=1;
            end
        end
    end

    always_comb begin : hit_way_comb_func  // hit_way_comb_func
        logic[63:0] i;
        hit_way_comb = 'h0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (tag_ram__q_out[i][(TAG_BITS + 'h1)] && (tag_ram__q_out[i]['h0 +:(TAG_BITS - 'h1) - 'h0 + 1] == req_tag_comb)) begin
                hit_way_comb = unsigned'(WAY_BITS'(unsigned'(WAY_BITS'(i))));
            end
        end
    end

    always_comb begin : hit_aligned_word_comb_func  // hit_aligned_word_comb_func
        logic[63:0] i;
        logic[63:0] way;
        logic[63:0] word;
        logic[31:0] ret;
        way='h0;
        word='h0;
        ret='h0;
        for (i='h0;i < DATA_BANKS;i=i+1) begin
            way=i/LINE_WORDS;
            word=i % LINE_WORDS;
            if ((hit_way_comb == way) && (req_word_comb == word)) begin
                ret=unsigned'(32'(data_ram__q_out[i]));
            end
        end
        hit_aligned_word_comb=ret;
    end

    always_comb begin : hit_aligned_next_word_comb_func  // hit_aligned_next_word_comb_func
        logic[63:0] i;
        logic[63:0] way;
        logic[63:0] word;
        logic[31:0] ret;
        way='h0;
        word='h0;
        ret='h0;
        for (i='h0;i < DATA_BANKS;i=i+1) begin
            way=i/LINE_WORDS;
            word=i % LINE_WORDS;
            if ((hit_way_comb == way) && ((req_word_comb + 'h1) == word)) begin
                ret=unsigned'(32'(data_ram__q_out[i]));
            end
        end
        hit_aligned_next_word_comb=ret;
    end

    always_comb begin : hit_word_comb_func  // hit_word_comb_func
        logic[63:0] i;
        logic[63:0] way;
        logic[63:0] word_index;
        logic[31:0] _byte;
        logic[31:0] word;
        way='h0;
        word_index='h0;
        word='h0;
        hit_word_comb='h0;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        for (i='h0;i < DATA_BANKS;i=i+1) begin
            way=i/LINE_WORDS;
            word_index=i % LINE_WORDS;
            if ((hit_way_comb == way) && (req_word_comb == word_index)) begin
                word=unsigned'(32'(data_ram__q_out[i]));
                hit_word_comb|=word >>> ((_byte*'h8));
            end
            if (((_byte != 'h0) && (hit_way_comb == way)) && ((req_word_comb + 'h1) == word_index)) begin
                word=unsigned'(32'(data_ram__q_out[i]));
                hit_word_comb|=word <<< (('h20 - (_byte*'h8)));
            end
        end
    end

    always_comb begin : write_word_comb_func  // write_word_comb_func
        logic[63:0] i;
        logic[31:0] old_data;
        logic[31:0] new_data;
        logic[31:0] mask;
        logic[31:0] _byte;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        old_data=hit_aligned_word_comb;
        new_data=unsigned'(32'(req_write_data_reg)) <<< ((_byte*'h8));
        mask='h0;
        for (i='h0;i < 'h4;i=i+1) begin
            if (((req_write_mask_reg & (('h1 <<< i)))) && ((i + _byte) < 'h4)) begin
                mask|='hFF <<< ((((i + _byte))*'h8));
            end
        end
        write_word_comb=((old_data & ~mask)) | ((new_data & mask));
    end

    always_comb begin : write_next_word_comb_func  // write_next_word_comb_func
        logic[63:0] i;
        logic[31:0] old_data;
        logic[31:0] new_data;
        logic[31:0] mask;
        logic[31:0] _byte;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        old_data=hit_aligned_next_word_comb;
        new_data=(_byte == 'h0) ? (unsigned'(32'('h0))) : (unsigned'(32'(req_write_data_reg)) >>> (('h20 - (_byte*'h8))));
        mask='h0;
        for (i='h0;i < 'h4;i=i+1) begin
            if (((req_write_mask_reg & (('h1 <<< i)))) && (i + _byte)>='h4) begin
                mask|='hFF <<< (((((i + _byte) - 'h4))*'h8));
            end
        end
        write_next_word_comb=((old_data & ~mask)) | ((new_data & mask));
    end

    always_comb begin : axi_aligned_word_comb_func  // axi_aligned_word_comb_func
        logic[31:0] word;
        word=unsigned'(32'(req_word_comb)) % PORT_WORDS;
        axi_aligned_word_comb=unsigned'(32'(axi_rdata_selected_comb[word*'h20 +:32]));
    end

    always_comb begin : fill_write_word_comb_func  // fill_write_word_comb_func
        logic[63:0] i;
        logic[31:0] old_data;
        logic[31:0] new_data;
        logic[31:0] mask;
        logic[31:0] _byte;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        old_data=axi_aligned_word_comb;
        new_data=unsigned'(32'(req_write_data_reg)) <<< ((_byte*'h8));
        mask='h0;
        if (req_write_reg) begin
            for (i='h0;i < 'h4;i=i+1) begin
                if (((req_write_mask_reg & (('h1 <<< i)))) && ((i + _byte) < 'h4)) begin
                    mask|='hFF <<< ((((i + _byte))*'h8));
                end
            end
            fill_write_word_comb=((old_data & ~mask)) | ((new_data & mask));
        end
        else begin
            fill_write_word_comb=old_data;
        end
    end

    always_comb begin : fill_write_next_word_comb_func  // fill_write_next_word_comb_func
        logic[63:0] i;
        logic[31:0] old_data;
        logic[31:0] new_data;
        logic[31:0] mask;
        logic[31:0] _byte;
        logic[31:0] word;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        word=((unsigned'(32'(req_word_comb)) + 'h1)) % PORT_WORDS;
        old_data='h0;
        if ((unsigned'(32'(req_word_comb)) + 'h1) < LINE_WORDS) begin
            old_data=unsigned'(32'(axi_rdata_selected_comb[word*'h20 +:32]));
        end
        new_data=(_byte == 'h0) ? (unsigned'(32'('h0))) : (unsigned'(32'(req_write_data_reg)) >>> (('h20 - (_byte*'h8))));
        mask='h0;
        if (req_write_reg) begin
            for (i='h0;i < 'h4;i=i+1) begin
                if (((req_write_mask_reg & (('h1 <<< i)))) && (i + _byte)>='h4) begin
                    mask|='hFF <<< (((((i + _byte) - 'h4))*'h8));
                end
            end
            fill_write_next_word_comb=((old_data & ~mask)) | ((new_data & mask));
        end
        else begin
            fill_write_next_word_comb=old_data;
        end
    end

    always_comb begin : hit_beat_comb_func  // hit_beat_comb_func
        logic[63:0] i;
        logic[63:0] way;
        logic[63:0] word_index;
        logic[63:0] beat_word;
        way='h0;
        word_index='h0;
        beat_word='h0;
        hit_beat_comb = 'h0;
        for (i='h0;i < DATA_BANKS;i=i+1) begin
            way=i/LINE_WORDS;
            word_index=i % LINE_WORDS;
            if (((hit_way_comb == way) && word_index>=(unsigned'(32'(req_beat_comb))*PORT_WORDS)) && (word_index < (((unsigned'(32'(req_beat_comb)) + 'h1))*PORT_WORDS))) begin
                beat_word=word_index - (unsigned'(32'(req_beat_comb))*PORT_WORDS);
                hit_beat_comb[beat_word*'h20 +:32] = data_ram__q_out[i];
            end
        end
    end

    always_comb begin : cross_read_data_comb_func  // cross_read_data_comb_func
        logic[31:0] low_word;
        logic[31:0] _byte;
        logic[31:0] low;
        logic[31:0] high;
        logic[31:0] data;
        cross_read_data_comb = cross_low_reg;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        low_word=(unsigned'(32'(req_addr_reg)) % PORT_BYTES)/'h4;
        low=unsigned'(32'(cross_low_reg[low_word*'h20 +:32]));
        high=unsigned'(32'(cross_high_reg['h0 +:32]));
        data=((low >>> ((_byte*'h8)))) | ((high <<< (('h20 - (_byte*'h8)))));
        cross_read_data_comb = 'h0;
        cross_read_data_comb['h0 +:32] = data;
    end

    always_comb begin : tag_write_data_comb_func  // tag_write_data_comb_func
        if (state_reg == ST_INIT) begin
            tag_write_data_comb = 'h0;
        end
        else begin
            tag_write_data_comb = ((((unsigned'(64'('h1)) <<< ((TAG_BITS + 'h1)))) | ((unsigned'(64'(req_write_reg)) <<< TAG_BITS))) | unsigned'(64'(req_tag_comb)));
        end
    end

    generate  // _assign
        genvar gi;
        for (gi='h0;gi < DATA_BANKS;gi=gi+1) begin
            assign data_ram__addr_in[gi] = unsigned'(SET_BITS'(((state_reg == ST_IDLE)) ? (active_set_comb) : (req_set_comb)));
            assign data_ram__rd_in[gi] = (((state_reg == ST_IDLE) && ((active_read_comb || active_write_comb)))) || (state_reg == ST_CROSS_WRITE_LOOKUP);
            assign data_ram__wr_in[gi] = (((((((state_reg == ST_AXI_R) && axi_rvalid_selected_comb) && axi_rready_comb) && (fill_way_reg == ((gi/LINE_WORDS)))) && (gi % LINE_WORDS)>=(unsigned'(32'(fill_beat_reg))*PORT_WORDS)) && (((gi % LINE_WORDS)) < (((unsigned'(32'(fill_beat_reg)) + 'h1))*PORT_WORDS)))) || ((((((((state_reg == ST_LOOKUP) || (state_reg == ST_CROSS_WRITE_LOOKUP))) && req_write_reg) && hit_comb) && (hit_way_comb == ((gi/LINE_WORDS)))) && (((req_word_comb == ((gi % LINE_WORDS))) || (((((unsigned'(32'(req_addr_reg)) & 'h3)) != 'h0) && ((req_word_comb + 'h1) == ((gi % LINE_WORDS)))))))));
            assign data_ram__data_in[gi] = (((state_reg == ST_LOOKUP) || (state_reg == ST_CROSS_WRITE_LOOKUP))) ? (((((((unsigned'(32'(req_addr_reg)) & 'h3)) != 'h0) && ((req_word_comb + 'h1) == ((gi % LINE_WORDS))))) ? (write_next_word_comb) : (write_word_comb))) : ((((req_write_reg && (req_word_comb == ((gi % LINE_WORDS))))) ? (fill_write_word_comb) : ((((req_write_reg && (((unsigned'(32'(req_addr_reg)) & 'h3)) != 'h0)) && ((req_word_comb + 'h1) == ((gi % LINE_WORDS))))) ? (fill_write_next_word_comb) : (unsigned'(32'(axi_rdata_selected_comb[((((gi % LINE_WORDS)) % PORT_WORDS))*'h20 +:(((gi % LINE_WORDS) % PORT_WORDS)*'h20) + 'h1F - ((gi % LINE_WORDS) % PORT_WORDS)*'h20 + 1]))))));
            assign data_ram__id_in[gi]='h7D0 + gi;
        end
        for (gi='h0;gi < WAYS;gi=gi+1) begin
            assign tag_ram__addr_in[gi] = unsigned'(SET_BITS'(((state_reg == ST_INIT)) ? (init_set_reg) : ((((state_reg == ST_IDLE)) ? (active_set_comb) : (req_set_comb)))));
            assign tag_ram__rd_in[gi] = (((state_reg == ST_IDLE) && ((active_read_comb || active_write_comb)))) || (state_reg == ST_CROSS_WRITE_LOOKUP);
            assign tag_ram__wr_in[gi] = (((state_reg == ST_INIT)) || ((((((state_reg == ST_AXI_R) && axi_rvalid_selected_comb) && axi_rready_comb) && (fill_beat_reg == (LINE_BEATS - 'h1))) && (fill_way_reg == gi)))) || (((((((state_reg == ST_LOOKUP) || (state_reg == ST_CROSS_WRITE_LOOKUP))) && req_write_reg) && hit_comb) && (hit_way_comb == gi)));
            assign tag_ram__data_in[gi] = tag_write_data_comb;
            assign tag_ram__id_in[gi]='h834 + gi;
        end
        for (gi='h0;gi < MEM_PORTS;gi=gi+1) begin
            assign axi_in__awready_out[gi] = (((state_reg == ST_IDLE) && !slave_aw_pending_reg[gi]) && !slave_bvalid_reg[gi]) && axi_in__awvalid_in[gi];
            assign axi_in__wready_out[gi] = ((state_reg == ST_IDLE) && slave_write_pending_comb) && (active_slave_index_comb == gi);
            assign axi_in__bvalid_out[gi] = slave_bvalid_reg[gi];
            assign axi_in__bid_out[gi] = slave_bid_reg[gi];
            assign axi_in__arready_out[gi] = ((((state_reg == ST_IDLE) && active_is_slave_comb) && !slave_write_pending_comb) && slave_read_pending_comb) && (active_slave_index_comb == gi);
            assign axi_in__rvalid_out[gi] = slave_rvalid_reg[gi];
            assign axi_in__rdata_out[gi] = slave_rdata_reg[gi];
            assign axi_in__rlast_out[gi] = slave_rvalid_reg[gi];
            assign axi_in__rid_out[gi] = slave_rid_reg[gi];
            assign axi_out__awvalid_out[gi] = axi_awvalid_comb && (axi_aw_sel_comb == gi);
            assign axi_out__awaddr_out[gi] = axi_awaddr_local_comb;
            assign axi_out__awid_out[gi] = unsigned'(4'(unsigned'(4'('h0))));
            assign axi_out__wvalid_out[gi] = axi_wvalid_comb && (axi_aw_sel_comb == gi);
            assign axi_out__wdata_out[gi] = axi_wdata_comb;
            assign axi_out__wlast_out[gi] = axi_wvalid_comb && (axi_aw_sel_comb == gi);
            assign axi_out__bready_out[gi] = axi_aw_sel_comb == gi;
            assign axi_out__arvalid_out[gi] = axi_arvalid_comb && (axi_ar_sel_comb == gi);
            assign axi_out__araddr_out[gi] = axi_araddr_local_comb;
            assign axi_out__arid_out[gi] = unsigned'(4'(unsigned'(4'('h0))));
            assign axi_out__rready_out[gi] = axi_rready_comb && (axi_ar_sel_comb == gi);
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        logic[63:0] way;
        logic[31:0] trace_line;
        logic trace_line_enabled;
        logic trace_req_line;
        logic trace_active_line;
        logic[128-1:0] trace_data;
        logic[31:0] trace_word0;
        logic[31:0] trace_word1;
        trace_line='h0;
        trace_line_enabled=0;
        trace_req_line=0;
        trace_active_line=0;
        trace_data = 'h0;
        trace_word0='h0;
        trace_word1='h0;
        for (i='h0;i < DATA_BANKS;i=i+1) begin
        end
        for (way='h0;way < WAYS;way=way+1) begin
        end
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (slave_bvalid_reg[i] && axi_in__bready_in[i]) begin
                slave_bvalid_reg_tmp[i] = unsigned'(1'(0));
            end
            if (slave_rvalid_reg[i] && axi_in__rready_in[i]) begin
                slave_rvalid_reg_tmp[i] = unsigned'(1'(0));
            end
            if (((state_reg == ST_IDLE) && axi_in__awvalid_in[i]) && axi_in__awready_out[i]) begin
                slave_aw_pending_reg_tmp[i] = unsigned'(1'(1));
                slave_awaddr_reg_tmp[i] = axi_in__awaddr_in[i];
                slave_awid_reg_tmp[i] = axi_in__awid_in[i];
            end
        end
        if (state_reg == ST_INIT) begin
            if (init_set_reg == (SETS - 'h1)) begin
                state_reg_tmp = ST_IDLE;
            end
            else begin
                init_set_reg_tmp = init_set_reg + 'h1;
            end
        end
        else begin
            if (state_reg == ST_IDLE) begin
                if (active_read_comb || active_write_comb) begin
                    if (trace_active_line) begin
                        $write("trace-l2 cycle=%x accept addr=%08x rd=%x wr=%x wdata=%08x mask=%02x slave=%x dport=%x victim=%x\n", $time, active_addr_comb, active_read_comb, active_write_comb, active_write_data_comb, active_write_mask_comb, active_is_slave_comb, active_is_d_comb, unsigned'(32'(victim_reg)));
                    end
                    req_addr_reg_tmp = unsigned'(32'(active_addr_comb));
                    req_write_data_reg_tmp = unsigned'(32'(active_write_data_comb));
                    req_write_mask_reg_tmp = unsigned'(8'(active_write_mask_comb));
                    req_read_reg_tmp = unsigned'(1'(active_read_comb));
                    req_write_reg_tmp = unsigned'(1'(active_write_comb));
                    req_port_reg_tmp = unsigned'(1'(active_is_d_comb));
                    req_from_slave_reg_tmp = unsigned'(1'(active_is_slave_comb));
                    req_slave_index_reg_tmp = active_slave_index_comb;
                    for (i='h0;i < MEM_PORTS;i=i+1) begin
                        if (active_is_slave_comb && (active_slave_index_comb == i)) begin
                            req_slave_id_reg_tmp = (slave_write_pending_comb) ? (((slave_aw_pending_reg[i]) ? (slave_awid_reg[i]) : (axi_in__awid_in[i]))) : (axi_in__arid_in[i]);
                            if (slave_write_pending_comb) begin
                                slave_aw_pending_reg_tmp[i] = unsigned'(1'(0));
                            end
                        end
                    end
                    state_reg_tmp = (active_cross_line_read_comb) ? (ST_CROSS_AR0) : (ST_LOOKUP);
                end
            end
            else begin
                if (state_reg == ST_LOOKUP) begin
                    if (!req_addr_in_memory_comb) begin
                        if (trace_req_line) begin
                            $write("trace-l2 cycle=%x lookup-outside addr=%08x rd=%x wr=%x\n", $time, unsigned'(32'(req_addr_reg)), req_read_reg, req_write_reg);
                        end
                        if (req_from_slave_reg) begin
                            for (i='h0;i < MEM_PORTS;i=i+1) begin
                                if (req_slave_index_reg == i) begin
                                    if (req_read_reg) begin
                                        slave_rvalid_reg_tmp[i] = unsigned'(1'(1));
                                        slave_rid_reg_tmp[i] = req_slave_id_reg;
                                        slave_rdata_reg_tmp[i] = 'h0;
                                    end
                                    if (req_write_reg) begin
                                        slave_bvalid_reg_tmp[i] = unsigned'(1'(1));
                                        slave_bid_reg_tmp[i] = req_slave_id_reg;
                                    end
                                end
                            end
                            state_reg_tmp = ST_IDLE;
                        end
                        else begin
                            if (req_read_reg) begin
                                last_data_reg_tmp = 'h0;
                            end
                            state_reg_tmp = ST_DONE;
                        end
                    end
                    else begin
                        if (req_uncached_region_comb) begin
                            if (trace_req_line) begin
                                $write("trace-l2 cycle=%x lookup-uncached addr=%08x rd=%x wr=%x\n", $time, unsigned'(32'(req_addr_reg)), req_read_reg, req_write_reg);
                            end
                            state_reg_tmp = (req_read_reg) ? (ST_IO_AR) : (ST_IO_AW);
                        end
                        else begin
                            if (hit_comb) begin
                                if (trace_req_line) begin
                                    trace_data = hit_beat_comb;
                                    trace_word0=unsigned'(32'(trace_data['h0 +:32]));
                                    trace_word1=(PORT_WORDS > 'h1) ? (unsigned'(32'(trace_data['h20 +:32]))) : ('h0);
                                    $write("trace-l2 cycle=%x lookup-hit addr=%08x rd=%x wr=%x way=%x word=%x hit_word=%08x beat0=%08x beat1=%08x wdata=%08x mask=%02x\n", $time, unsigned'(32'(req_addr_reg)), req_read_reg, req_write_reg, unsigned'(32'(hit_way_comb)), unsigned'(32'(req_word_comb)), hit_word_comb, trace_word0, trace_word1, unsigned'(32'(req_write_data_reg)), unsigned'(32'(req_write_mask_reg)));
                                end
                                if (req_from_slave_reg) begin
                                    for (i='h0;i < MEM_PORTS;i=i+1) begin
                                        if (req_slave_index_reg == i) begin
                                            if (req_read_reg) begin
                                                slave_rvalid_reg_tmp[i] = unsigned'(1'(1));
                                                slave_rid_reg_tmp[i] = req_slave_id_reg;
                                                slave_rdata_reg_tmp[i] = hit_beat_comb;
                                            end
                                            if (req_write_reg && !req_cross_line_write_comb) begin
                                                slave_bvalid_reg_tmp[i] = unsigned'(1'(1));
                                                slave_bid_reg_tmp[i] = req_slave_id_reg;
                                            end
                                        end
                                    end
                                end
                                else begin
                                    if (req_read_reg) begin
                                        last_data_reg_tmp = 'h0;
                                    end
                                end
                                if (!req_from_slave_reg && req_read_reg) begin
                                    last_data_reg_tmp = hit_beat_comb;
                                end
                                if (req_cross_line_write_comb) begin
                                    req_addr_reg_tmp = unsigned'(32'(((unsigned'(32'(req_addr_reg)) & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) + CACHE_LINE_SIZE));
                                    req_write_data_reg_tmp = unsigned'(32'(cross_write_data_comb));
                                    req_write_mask_reg_tmp = unsigned'(8'(cross_write_mask_comb));
                                    state_reg_tmp = ST_CROSS_WRITE_LOOKUP;
                                end
                                else begin
                                    state_reg_tmp = (req_from_slave_reg) ? (ST_IDLE) : ((((req_read_reg || req_write_reg)) ? (ST_DONE) : (ST_IDLE)));
                                end
                            end
                            else begin
                                if (trace_req_line) begin
                                    $write("trace-l2 cycle=%x lookup-miss addr=%08x rd=%x wr=%x victim=%x evict_valid=%x evict_dirty=%x evict_tag=%08x\n", $time, unsigned'(32'(req_addr_reg)), req_read_reg, req_write_reg, unsigned'(32'(victim_reg)), evict_valid_comb, evict_dirty_comb, unsigned'(32'(evict_tag_comb)));
                                end
                                fill_way_reg_tmp = victim_reg;
                                fill_beat_reg_tmp = 'h0;
                                evict_beat_reg_tmp = 'h0;
                                state_reg_tmp = ((evict_valid_comb && evict_dirty_comb)) ? (ST_EVICT_AW) : (ST_AXI_AR);
                            end
                        end
                    end
                end
                else begin
                    if (state_reg == ST_CROSS_WRITE_LOOKUP) begin
                        if (!req_addr_in_memory_comb) begin
                            if (req_from_slave_reg) begin
                                for (i='h0;i < MEM_PORTS;i=i+1) begin
                                    if (req_slave_index_reg == i) begin
                                        slave_bvalid_reg_tmp[i] = unsigned'(1'(1));
                                        slave_bid_reg_tmp[i] = req_slave_id_reg;
                                    end
                                end
                                state_reg_tmp = ST_IDLE;
                            end
                            else begin
                                state_reg_tmp = ST_DONE;
                            end
                        end
                        else begin
                            if (hit_comb) begin
                                if (req_from_slave_reg) begin
                                    for (i='h0;i < MEM_PORTS;i=i+1) begin
                                        if (req_slave_index_reg == i) begin
                                            slave_bvalid_reg_tmp[i] = unsigned'(1'(1));
                                            slave_bid_reg_tmp[i] = req_slave_id_reg;
                                        end
                                    end
                                    state_reg_tmp = ST_IDLE;
                                end
                                else begin
                                    state_reg_tmp = ST_DONE;
                                end
                            end
                            else begin
                                fill_way_reg_tmp = victim_reg;
                                fill_beat_reg_tmp = 'h0;
                                evict_beat_reg_tmp = 'h0;
                                state_reg_tmp = ((evict_valid_comb && evict_dirty_comb)) ? (ST_EVICT_AW) : (ST_AXI_AR);
                            end
                        end
                    end
                    else begin
                        if (state_reg == ST_EVICT_AW) begin
                            if (axi_awvalid_comb && axi_awready_selected_comb) begin
                                state_reg_tmp = ST_EVICT_W;
                            end
                        end
                        else begin
                            if (state_reg == ST_EVICT_W) begin
                                if (axi_wvalid_comb && axi_wready_selected_comb) begin
                                    if (trace_line_enabled && ((((axi_awaddr_full_comb & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) == trace_line))) begin
                                        trace_data = evict_line_comb;
                                        trace_word0=unsigned'(32'(trace_data['h0 +:32]));
                                        trace_word1=(PORT_WORDS > 'h1) ? (unsigned'(32'(trace_data['h20 +:32]))) : ('h0);
                                        $write("trace-l2 cycle=%x evict addr=%08x beat=%x data0=%08x data1=%08x way=%x\n", $time, axi_awaddr_full_comb, unsigned'(32'(evict_beat_reg)), trace_word0, trace_word1, unsigned'(32'(evict_way_comb)));
                                    end
                                    state_reg_tmp = ST_EVICT_B;
                                end
                            end
                            else begin
                                if (state_reg == ST_EVICT_B) begin
                                    if (axi_bvalid_selected_comb) begin
                                        if (evict_beat_reg == (LINE_BEATS - 'h1)) begin
                                            fill_beat_reg_tmp = 'h0;
                                            state_reg_tmp = ST_AXI_AR;
                                        end
                                        else begin
                                            evict_beat_reg_tmp = evict_beat_reg + 'h1;
                                            state_reg_tmp = ST_EVICT_AW;
                                        end
                                    end
                                end
                                else begin
                                    if (state_reg == ST_AXI_AR) begin
                                        if (axi_arvalid_comb && axi_arready_selected_comb) begin
                                            state_reg_tmp = ST_AXI_R;
                                        end
                                    end
                                    else begin
                                        if (state_reg == ST_AXI_R) begin
                                            if (axi_rvalid_selected_comb && axi_rready_comb) begin
                                                if (trace_req_line) begin
                                                    trace_data = axi_rdata_selected_comb;
                                                    trace_word0=unsigned'(32'(trace_data['h0 +:32]));
                                                    trace_word1=(PORT_WORDS > 'h1) ? (unsigned'(32'(trace_data['h20 +:32]))) : ('h0);
                                                    $write("trace-l2 cycle=%x fill addr=%08x beat=%x data0=%08x data1=%08x req_word=%x req_beat=%x\n", $time, axi_araddr_full_comb, unsigned'(32'(fill_beat_reg)), trace_word0, trace_word1, unsigned'(32'(req_word_comb)), unsigned'(32'(req_beat_comb)));
                                                end
                                                if ((!req_from_slave_reg && req_read_reg) && (fill_beat_reg == req_beat_comb)) begin
                                                    last_data_reg_tmp = axi_rdata_selected_comb;
                                                end
                                                if (fill_beat_reg == (LINE_BEATS - 'h1)) begin
                                                    victim_reg_tmp = ((victim_reg == (WAYS - 'h1))) ? (unsigned'(WAY_BITS'(unsigned'(WAY_BITS'('h0))))) : (unsigned'(WAY_BITS'(unsigned'(WAY_BITS'(victim_reg + 'h1)))));
                                                    if (req_cross_line_write_comb) begin
                                                        req_addr_reg_tmp = unsigned'(32'(((unsigned'(32'(req_addr_reg)) & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) + CACHE_LINE_SIZE));
                                                        req_write_data_reg_tmp = unsigned'(32'(cross_write_data_comb));
                                                        req_write_mask_reg_tmp = unsigned'(8'(cross_write_mask_comb));
                                                        state_reg_tmp = ST_CROSS_WRITE_LOOKUP;
                                                    end
                                                    else begin
                                                        if (req_from_slave_reg) begin
                                                            for (i='h0;i < MEM_PORTS;i=i+1) begin
                                                                if (req_slave_index_reg == i) begin
                                                                    if (req_read_reg) begin
                                                                        slave_rvalid_reg_tmp[i] = unsigned'(1'(1));
                                                                        slave_rid_reg_tmp[i] = req_slave_id_reg;
                                                                        slave_rdata_reg_tmp[i] = ((fill_beat_reg == req_beat_comb)) ? (axi_rdata_selected_comb) : (hit_beat_comb);
                                                                    end
                                                                    if (req_write_reg) begin
                                                                        slave_bvalid_reg_tmp[i] = unsigned'(1'(1));
                                                                        slave_bid_reg_tmp[i] = req_slave_id_reg;
                                                                    end
                                                                end
                                                            end
                                                            state_reg_tmp = ST_IDLE;
                                                        end
                                                        else begin
                                                            state_reg_tmp = ST_DONE;
                                                        end
                                                    end
                                                end
                                                else begin
                                                    fill_beat_reg_tmp = fill_beat_reg + 'h1;
                                                    state_reg_tmp = ST_AXI_AR;
                                                end
                                            end
                                        end
                                        else begin
                                            if (state_reg == ST_CROSS_AR0) begin
                                                if (axi_arvalid_comb && axi_arready_selected_comb) begin
                                                    state_reg_tmp = ST_CROSS_R0;
                                                end
                                            end
                                            else begin
                                                if (state_reg == ST_CROSS_R0) begin
                                                    if (axi_rvalid_selected_comb && axi_rready_comb) begin
                                                        cross_low_reg_tmp = axi_rdata_selected_comb;
                                                        state_reg_tmp = ST_CROSS_AR1;
                                                    end
                                                end
                                                else begin
                                                    if (state_reg == ST_CROSS_AR1) begin
                                                        if (axi_arvalid_comb && axi_arready_selected_comb) begin
                                                            state_reg_tmp = ST_CROSS_R1;
                                                        end
                                                    end
                                                    else begin
                                                        if (state_reg == ST_CROSS_R1) begin
                                                            if (axi_rvalid_selected_comb && axi_rready_comb) begin
                                                                cross_high_reg_tmp = axi_rdata_selected_comb;
                                                                state_reg_tmp = ST_CROSS_DONE;
                                                            end
                                                        end
                                                        else begin
                                                            if (state_reg == ST_CROSS_DONE) begin
                                                                if (req_from_slave_reg) begin
                                                                    for (i='h0;i < MEM_PORTS;i=i+1) begin
                                                                        if (req_slave_index_reg == i) begin
                                                                            slave_rvalid_reg_tmp[i] = unsigned'(1'(1));
                                                                            slave_rid_reg_tmp[i] = req_slave_id_reg;
                                                                            slave_rdata_reg_tmp[i] = cross_read_data_comb;
                                                                        end
                                                                    end
                                                                    state_reg_tmp = ST_IDLE;
                                                                end
                                                                else begin
                                                                    last_data_reg_tmp = cross_read_data_comb;
                                                                    state_reg_tmp = ST_DONE;
                                                                end
                                                            end
                                                            else begin
                                                                if (state_reg == ST_IO_AW) begin
                                                                    if (axi_awvalid_comb && axi_awready_selected_comb) begin
                                                                        state_reg_tmp = ST_IO_W;
                                                                    end
                                                                end
                                                                else begin
                                                                    if (state_reg == ST_IO_W) begin
                                                                        if (axi_wvalid_comb && axi_wready_selected_comb) begin
                                                                            state_reg_tmp = ST_IO_B;
                                                                        end
                                                                    end
                                                                    else begin
                                                                        if (state_reg == ST_IO_B) begin
                                                                            if (axi_bvalid_selected_comb) begin
                                                                                if (req_from_slave_reg) begin
                                                                                    for (i='h0;i < MEM_PORTS;i=i+1) begin
                                                                                        if (req_slave_index_reg == i) begin
                                                                                            slave_bvalid_reg_tmp[i] = unsigned'(1'(1));
                                                                                            slave_bid_reg_tmp[i] = req_slave_id_reg;
                                                                                        end
                                                                                    end
                                                                                    state_reg_tmp = ST_IDLE;
                                                                                end
                                                                                else begin
                                                                                    state_reg_tmp = ST_DONE;
                                                                                end
                                                                            end
                                                                        end
                                                                        else begin
                                                                            if (state_reg == ST_IO_AR) begin
                                                                                if (axi_arvalid_comb && axi_arready_selected_comb) begin
                                                                                    state_reg_tmp = ST_IO_R;
                                                                                end
                                                                            end
                                                                            else begin
                                                                                if (state_reg == ST_IO_R) begin
                                                                                    if (axi_rvalid_selected_comb && axi_rready_comb) begin
                                                                                        if (req_from_slave_reg) begin
                                                                                            for (i='h0;i < MEM_PORTS;i=i+1) begin
                                                                                                if (req_slave_index_reg == i) begin
                                                                                                    slave_rvalid_reg_tmp[i] = unsigned'(1'(1));
                                                                                                    slave_rid_reg_tmp[i] = req_slave_id_reg;
                                                                                                    slave_rdata_reg_tmp[i] = axi_rdata_selected_comb;
                                                                                                end
                                                                                            end
                                                                                            state_reg_tmp = ST_IDLE;
                                                                                        end
                                                                                        else begin
                                                                                            last_data_reg_tmp = axi_rdata_selected_comb;
                                                                                            state_reg_tmp = ST_DONE;
                                                                                        end
                                                                                    end
                                                                                end
                                                                                else begin
                                                                                    if (state_reg == ST_DONE) begin
                                                                                        if (req_from_slave_reg) begin
                                                                                            for (i='h0;i < MEM_PORTS;i=i+1) begin
                                                                                                if (req_slave_index_reg == i) begin
                                                                                                    if (req_read_reg) begin
                                                                                                        slave_rvalid_reg_tmp[i] = unsigned'(1'(1));
                                                                                                        slave_rid_reg_tmp[i] = req_slave_id_reg;
                                                                                                        slave_rdata_reg_tmp[i] = last_data_reg;
                                                                                                    end
                                                                                                    if (req_write_reg) begin
                                                                                                        slave_bvalid_reg_tmp[i] = unsigned'(1'(1));
                                                                                                        slave_bid_reg_tmp[i] = req_slave_id_reg;
                                                                                                    end
                                                                                                end
                                                                                            end
                                                                                        end
                                                                                        state_reg_tmp = ST_IDLE;
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
            state_reg_tmp = '0;
            req_addr_reg_tmp = '0;
            req_write_data_reg_tmp = '0;
            req_write_mask_reg_tmp = '0;
            req_read_reg_tmp = '0;
            req_write_reg_tmp = '0;
            req_port_reg_tmp = '0;
            req_from_slave_reg_tmp = '0;
            req_slave_index_reg_tmp = '0;
            req_slave_id_reg_tmp = '0;
            victim_reg_tmp = '0;
            fill_way_reg_tmp = '0;
            init_set_reg_tmp = '0;
            last_data_reg_tmp = '0;
            cross_low_reg_tmp = '0;
            cross_high_reg_tmp = '0;
            fill_beat_reg_tmp = '0;
            evict_beat_reg_tmp = '0;
            slave_bvalid_reg_tmp = '0;
            slave_bid_reg_tmp = '0;
            slave_rvalid_reg_tmp = '0;
            slave_rid_reg_tmp = '0;
            slave_rdata_reg_tmp = '0;
            slave_aw_pending_reg_tmp = '0;
            slave_awaddr_reg_tmp = '0;
            slave_awid_reg_tmp = '0;
            state_reg_tmp = ST_INIT;
        end
    end
    endtask

    always_comb begin : read_data_comb_func  // read_data_comb_func
        if ((state_reg != ST_IDLE) && req_from_slave_reg) begin
            read_data_comb='h0;
        end
        else begin
            if (state_reg == ST_DONE) begin
                read_data_comb=last_data_reg;
            end
            else begin
                if (state_reg == ST_CROSS_DONE) begin
                    read_data_comb=cross_read_data_comb;
                end
                else begin
                    if ((state_reg == ST_LOOKUP) && hit_comb) begin
                        read_data_comb=hit_beat_comb;
                    end
                    else begin
                        if ((((state_reg == ST_AXI_R) || (state_reg == ST_IO_R)) || (state_reg == ST_CROSS_R0)) || (state_reg == ST_CROSS_R1)) begin
                            read_data_comb=axi_rdata_selected_comb;
                        end
                        else begin
                            read_data_comb='h0;
                        end
                    end
                end
            end
        end
    end

    always_comb begin : i_wait_comb_func  // i_wait_comb_func
        i_wait_comb=0;
        if (i_read_in) begin
            i_wait_comb=1;
            if ((((state_reg == ST_DONE) && !req_from_slave_reg) && !req_port_reg) && req_read_reg) begin
                i_wait_comb=0;
            end
        end
        if ((state_reg != ST_IDLE) && !(((((state_reg == ST_DONE) && !req_from_slave_reg) && !req_port_reg) && req_read_reg))) begin
            i_wait_comb=1;
        end
        if (d_read_in || d_write_in) begin
            i_wait_comb=1;
        end
        if (active_is_slave_comb && !(((((state_reg == ST_DONE) && !req_from_slave_reg) && !req_port_reg) && req_read_reg))) begin
            i_wait_comb=1;
        end
    end

    always_comb begin : d_wait_comb_func  // d_wait_comb_func
        d_wait_comb=0;
        if (d_write_in) begin
            d_wait_comb=1;
            if ((((state_reg == ST_DONE) && !req_from_slave_reg) && req_port_reg) && req_write_reg) begin
                d_wait_comb=0;
            end
        end
        if (d_read_in) begin
            d_wait_comb=1;
            if ((((state_reg == ST_DONE) && !req_from_slave_reg) && req_port_reg) && req_read_reg) begin
                d_wait_comb=0;
            end
        end
        if ((state_reg != ST_IDLE) && !(((((state_reg == ST_DONE) && !req_from_slave_reg) && req_port_reg) && ((req_read_reg || req_write_reg))))) begin
            d_wait_comb=1;
        end
        if (active_is_slave_comb && !(((((state_reg == ST_DONE) && !req_from_slave_reg) && req_port_reg) && ((req_read_reg || req_write_reg))))) begin
            d_wait_comb=1;
        end
    end

    always @(posedge clk) begin
        state_reg_tmp = state_reg;
        req_addr_reg_tmp = req_addr_reg;
        req_write_data_reg_tmp = req_write_data_reg;
        req_write_mask_reg_tmp = req_write_mask_reg;
        req_read_reg_tmp = req_read_reg;
        req_write_reg_tmp = req_write_reg;
        req_port_reg_tmp = req_port_reg;
        req_from_slave_reg_tmp = req_from_slave_reg;
        req_slave_index_reg_tmp = req_slave_index_reg;
        req_slave_id_reg_tmp = req_slave_id_reg;
        victim_reg_tmp = victim_reg;
        fill_way_reg_tmp = fill_way_reg;
        init_set_reg_tmp = init_set_reg;
        last_data_reg_tmp = last_data_reg;
        cross_low_reg_tmp = cross_low_reg;
        cross_high_reg_tmp = cross_high_reg;
        fill_beat_reg_tmp = fill_beat_reg;
        evict_beat_reg_tmp = evict_beat_reg;
        slave_bvalid_reg_tmp = slave_bvalid_reg;
        slave_bid_reg_tmp = slave_bid_reg;
        slave_rvalid_reg_tmp = slave_rvalid_reg;
        slave_rid_reg_tmp = slave_rid_reg;
        slave_rdata_reg_tmp = slave_rdata_reg;
        slave_aw_pending_reg_tmp = slave_aw_pending_reg;
        slave_awaddr_reg_tmp = slave_awaddr_reg;
        slave_awid_reg_tmp = slave_awid_reg;

        _work(reset);

        state_reg <= state_reg_tmp;
        req_addr_reg <= req_addr_reg_tmp;
        req_write_data_reg <= req_write_data_reg_tmp;
        req_write_mask_reg <= req_write_mask_reg_tmp;
        req_read_reg <= req_read_reg_tmp;
        req_write_reg <= req_write_reg_tmp;
        req_port_reg <= req_port_reg_tmp;
        req_from_slave_reg <= req_from_slave_reg_tmp;
        req_slave_index_reg <= req_slave_index_reg_tmp;
        req_slave_id_reg <= req_slave_id_reg_tmp;
        victim_reg <= victim_reg_tmp;
        fill_way_reg <= fill_way_reg_tmp;
        init_set_reg <= init_set_reg_tmp;
        last_data_reg <= last_data_reg_tmp;
        cross_low_reg <= cross_low_reg_tmp;
        cross_high_reg <= cross_high_reg_tmp;
        fill_beat_reg <= fill_beat_reg_tmp;
        evict_beat_reg <= evict_beat_reg_tmp;
        slave_bvalid_reg <= slave_bvalid_reg_tmp;
        slave_bid_reg <= slave_bid_reg_tmp;
        slave_rvalid_reg <= slave_rvalid_reg_tmp;
        slave_rid_reg <= slave_rid_reg_tmp;
        slave_rdata_reg <= slave_rdata_reg_tmp;
        slave_aw_pending_reg <= slave_aw_pending_reg_tmp;
        slave_awaddr_reg <= slave_awaddr_reg_tmp;
        slave_awid_reg <= slave_awid_reg_tmp;
    end

    assign i_read_data_out = read_data_comb;

    assign i_wait_out = i_wait_comb;

    assign d_read_data_out = read_data_comb;

    assign d_wait_out = d_wait_comb;


endmodule

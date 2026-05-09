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
    parameter  WAY_BITS = $clog2(WAYS);
    parameter  TAG_BITS = (ADDR_BITS - SET_BITS) - LINE_BITS;
    parameter  DATA_BANKS = WAYS*LINE_WORDS;
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
    reg[WAY_BITS-1:0] victim_reg;
    reg[WAY_BITS-1:0] fill_way_reg;
    reg[SET_BITS-1:0] init_set_reg;
    reg[PORT_BITWIDTH-1:0] last_data_reg;
    reg[PORT_BITWIDTH-1:0] cross_low_reg;
    reg[PORT_BITWIDTH-1:0] cross_high_reg;
    reg[LINE_BEAT_BITS-1:0] fill_beat_reg;
    reg[LINE_BEAT_BITS-1:0] evict_beat_reg;
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
    genvar gi, gj, gk;
      wire[$clog2((SETS))-1:0] data_ram__addr_in[DATA_BANKS];
      wire[('h20)-1:0] data_ram__data_in[DATA_BANKS];
      wire data_ram__wr_in[DATA_BANKS];
      wire data_ram__rd_in[DATA_BANKS];
      wire[('h20)-1:0] data_ram__q_out[DATA_BANKS];
      wire signed[31:0] data_ram__id_in[DATA_BANKS];
    generate
    for (gi=0; gi < DATA_BANKS; gi = gi + 1) begin
        RAM1PORT #(
        'h20
,       SETS
    ) data_ram (
            .clk(clk)
,           .reset(reset)
,           .addr_in(data_ram__addr_in[gi])
,           .data_in(data_ram__data_in[gi])
,           .wr_in(data_ram__wr_in[gi])
,           .rd_in(data_ram__rd_in[gi])
,           .q_out(data_ram__q_out[gi])
,           .id_in(data_ram__id_in[gi])
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
    for (gi=0; gi < WAYS; gi = gi + 1) begin
        RAM1PORT #(
        TAG_BITS + 'h2
,       SETS
    ) tag_ram (
            .clk(clk)
,           .reset(reset)
,           .addr_in(tag_ram__addr_in[gi])
,           .data_in(tag_ram__data_in[gi])
,           .wr_in(tag_ram__wr_in[gi])
,           .rd_in(tag_ram__rd_in[gi])
,           .q_out(tag_ram__q_out[gi])
,           .id_in(tag_ram__id_in[gi])
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
    logic[WAY_BITS-1:0] victim_reg_tmp;
    logic[WAY_BITS-1:0] fill_way_reg_tmp;
    logic[SET_BITS-1:0] init_set_reg_tmp;
    logic[PORT_BITWIDTH-1:0] last_data_reg_tmp;
    logic[PORT_BITWIDTH-1:0] cross_low_reg_tmp;
    logic[PORT_BITWIDTH-1:0] cross_high_reg_tmp;
    logic[LINE_BEAT_BITS-1:0] fill_beat_reg_tmp;
    logic[LINE_BEAT_BITS-1:0] evict_beat_reg_tmp;


    always_comb begin : active_is_d_comb_func  // active_is_d_comb_func
        active_is_d_comb=d_write_in || d_read_in;
        disable active_is_d_comb_func;
    end

    always_comb begin : active_read_comb_func  // active_read_comb_func
        active_read_comb=d_read_in || ((!d_write_in && i_read_in));
        disable active_read_comb_func;
    end

    always_comb begin : active_write_comb_func  // active_write_comb_func
        active_write_comb=d_write_in || (((!d_read_in && !d_write_in) && i_write_in));
        disable active_write_comb_func;
    end

    always_comb begin : active_addr_comb_func  // active_addr_comb_func
        active_addr_comb=(active_is_d_comb) ? (d_addr_in) : (i_addr_in);
        disable active_addr_comb_func;
    end

    always_comb begin : active_write_data_comb_func  // active_write_data_comb_func
        active_write_data_comb=(active_is_d_comb) ? (d_write_data_in) : (i_write_data_in);
        disable active_write_data_comb_func;
    end

    always_comb begin : active_write_mask_comb_func  // active_write_mask_comb_func
        active_write_mask_comb=(active_is_d_comb) ? (d_write_mask_in) : (i_write_mask_in);
        disable active_write_mask_comb_func;
    end

    always_comb begin : req_set_comb_func  // req_set_comb_func
        req_set_comb = unsigned'(6'((unsigned'(32'(req_addr_reg)) >>> LINE_BITS)));
        disable req_set_comb_func;
    end

    always_comb begin : active_set_comb_func  // active_set_comb_func
        active_set_comb = unsigned'(6'((active_addr_comb >>> LINE_BITS)));
        disable active_set_comb_func;
    end

    always_comb begin : req_word_comb_func  // req_word_comb_func
        req_word_comb = unsigned'(3'((((unsigned'(32'(req_addr_reg)) >>> 'h2)) & ((LINE_WORDS - 'h1)))));
        disable req_word_comb_func;
    end

    always_comb begin : req_beat_comb_func  // req_beat_comb_func
        req_beat_comb = unsigned'(1'((((unsigned'(32'(req_addr_reg)) & ((CACHE_LINE_SIZE - 'h1))))/PORT_BYTES)));
        disable req_beat_comb_func;
    end

    always_comb begin : req_tag_comb_func  // req_tag_comb_func
        req_tag_comb = unsigned'(21'((unsigned'(32'(req_addr_reg)) >>> ((LINE_BITS + SET_BITS)))));
        disable req_tag_comb_func;
    end

    always_comb begin : active_cross_line_read_comb_func  // active_cross_line_read_comb_func
        active_cross_line_read_comb=(active_read_comb && ((((active_addr_comb & 'h3)) != 'h0))) && ((((((active_addr_comb >>> 'h2)) & ((LINE_WORDS - 'h1)))) == (LINE_WORDS - 'h1)));
        disable active_cross_line_read_comb_func;
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
        disable req_cross_line_write_comb_func;
    end

    always_comb begin : cross_write_data_comb_func  // cross_write_data_comb_func
        logic[31:0] _byte;
        _byte=unsigned'(32'(req_addr_reg)) & 'h3;
        cross_write_data_comb=(_byte == 'h0) ? (unsigned'(32'('h0))) : (unsigned'(32'(req_write_data_reg)) >>> (('h20 - (_byte*'h8))));
        disable cross_write_data_comb_func;
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
        disable cross_write_mask_comb_func;
    end

    always_comb begin : req_addr_in_memory_comb_func  // req_addr_in_memory_comb_func
        logic[31:0] addr;
        logic[31:0] _local;
        logic[31:0] size;
        addr=req_addr_reg;
        _local=addr - memory_base_in;
        size=memory_size_in;
        req_addr_in_memory_comb=(addr>=memory_base_in && (size != 'h0)) && (_local < size);
        disable req_addr_in_memory_comb_func;
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
        disable axi_araddr_full_comb_func;
    end

    always_comb begin : axi_araddr_total_local_comb_func  // axi_araddr_total_local_comb_func
        axi_araddr_total_local_comb=axi_araddr_full_comb - memory_base_in;
        disable axi_araddr_total_local_comb_func;
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
        disable axi_ar_sel_comb_func;
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
        disable axi_ar_region_base_comb_func;
    end

    always_comb begin : axi_araddr_local_comb_func  // axi_araddr_local_comb_func
        axi_araddr_local_comb = unsigned'(19'((unsigned'(64'(((axi_araddr_total_local_comb - axi_ar_region_base_comb)))) & MEM_ADDR_MASK64)));
        disable axi_araddr_local_comb_func;
    end

    always_comb begin : axi_arvalid_comb_func  // axi_arvalid_comb_func
        axi_arvalid_comb=req_addr_in_memory_comb && (((((state_reg == ST_AXI_AR) || (state_reg == ST_CROSS_AR0)) || (state_reg == ST_CROSS_AR1)) || (state_reg == ST_IO_AR)));
        disable axi_arvalid_comb_func;
    end

    always_comb begin : axi_rready_comb_func  // axi_rready_comb_func
        axi_rready_comb=((state_reg == ST_AXI_R) || (state_reg == ST_CROSS_R0)) || (state_reg == ST_CROSS_R1);
        disable axi_rready_comb_func;
    end

    always_comb begin : axi_arready_selected_comb_func  // axi_arready_selected_comb_func
        logic[63:0] i;
        axi_arready_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_ar_sel_comb == i) begin
                axi_arready_selected_comb=axi_out__arready_in[i];
            end
        end
        disable axi_arready_selected_comb_func;
    end

    always_comb begin : axi_rvalid_selected_comb_func  // axi_rvalid_selected_comb_func
        logic[63:0] i;
        axi_rvalid_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_ar_sel_comb == i) begin
                axi_rvalid_selected_comb=axi_out__rvalid_in[i];
            end
        end
        disable axi_rvalid_selected_comb_func;
    end

    always_comb begin : axi_rdata_selected_comb_func  // axi_rdata_selected_comb_func
        logic[63:0] i;
        axi_rdata_selected_comb = 'h0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_ar_sel_comb == i) begin
                axi_rdata_selected_comb = axi_out__rdata_in[i];
            end
        end
        disable axi_rdata_selected_comb_func;
    end

    always_comb begin : evict_way_comb_func  // evict_way_comb_func
        evict_way_comb = ((state_reg == ST_LOOKUP)) ? (victim_reg) : (fill_way_reg);
        disable evict_way_comb_func;
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
        disable evict_valid_comb_func;
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
        disable evict_dirty_comb_func;
    end

    always_comb begin : evict_tag_comb_func  // evict_tag_comb_func
        logic[63:0] i;
        evict_tag_comb = 'h0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (evict_way_comb == i) begin
                evict_tag_comb = unsigned'(64'(tag_ram__q_out[i]['h0 +:TAG_BITS - 'h1 - 'h0 + 1]));
            end
        end
        disable evict_tag_comb_func;
    end

    always_comb begin : axi_awaddr_full_comb_func  // axi_awaddr_full_comb_func
        logic[31:0] addr;
        addr=((((unsigned'(32'(evict_tag_comb)) <<< ((SET_BITS + LINE_BITS)))) | ((unsigned'(32'(req_set_comb)) <<< LINE_BITS)))) + ((unsigned'(32'(evict_beat_reg))*PORT_BYTES));
        if (((state_reg == ST_IO_AW) || (state_reg == ST_IO_W)) || (state_reg == ST_IO_B)) begin
            addr=req_addr_reg;
        end
        axi_awaddr_full_comb=addr;
        disable axi_awaddr_full_comb_func;
    end

    always_comb begin : axi_awaddr_total_local_comb_func  // axi_awaddr_total_local_comb_func
        axi_awaddr_total_local_comb=axi_awaddr_full_comb - memory_base_in;
        disable axi_awaddr_total_local_comb_func;
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
        disable axi_aw_sel_comb_func;
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
        disable axi_aw_region_base_comb_func;
    end

    always_comb begin : axi_awaddr_local_comb_func  // axi_awaddr_local_comb_func
        axi_awaddr_local_comb = unsigned'(19'((unsigned'(64'(((axi_awaddr_total_local_comb - axi_aw_region_base_comb)))) & MEM_ADDR_MASK64)));
        disable axi_awaddr_local_comb_func;
    end

    always_comb begin : axi_awvalid_comb_func  // axi_awvalid_comb_func
        axi_awvalid_comb=req_addr_in_memory_comb && (((state_reg == ST_EVICT_AW) || (state_reg == ST_IO_AW)));
        disable axi_awvalid_comb_func;
    end

    always_comb begin : axi_wvalid_comb_func  // axi_wvalid_comb_func
        axi_wvalid_comb=req_addr_in_memory_comb && (((state_reg == ST_EVICT_W) || (state_reg == ST_IO_W)));
        disable axi_wvalid_comb_func;
    end

    always_comb begin : axi_awready_selected_comb_func  // axi_awready_selected_comb_func
        logic[63:0] i;
        axi_awready_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_aw_sel_comb == i) begin
                axi_awready_selected_comb=axi_out__awready_in[i];
            end
        end
        disable axi_awready_selected_comb_func;
    end

    always_comb begin : axi_wready_selected_comb_func  // axi_wready_selected_comb_func
        logic[63:0] i;
        axi_wready_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_aw_sel_comb == i) begin
                axi_wready_selected_comb=axi_out__wready_in[i];
            end
        end
        disable axi_wready_selected_comb_func;
    end

    always_comb begin : axi_bvalid_selected_comb_func  // axi_bvalid_selected_comb_func
        logic[63:0] i;
        axi_bvalid_selected_comb=0;
        for (i='h0;i < MEM_PORTS;i=i+1) begin
            if (axi_aw_sel_comb == i) begin
                axi_bvalid_selected_comb=axi_out__bvalid_in[i];
            end
        end
        disable axi_bvalid_selected_comb_func;
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
        disable evict_line_comb_func;
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
        disable req_uncached_region_comb_func;
    end

    always_comb begin : io_write_beat_comb_func  // io_write_beat_comb_func
        logic[31:0] word;
        io_write_beat_comb = 'h0;
        word=((unsigned'(32'(req_addr_reg)) % PORT_BYTES))/'h4;
        io_write_beat_comb[word*'h20 +:32] = req_write_data_reg;
        disable io_write_beat_comb_func;
    end

    always_comb begin : axi_wdata_comb_func  // axi_wdata_comb_func
        axi_wdata_comb = (state_reg == ST_IO_W) ? (io_write_beat_comb) : (evict_line_comb);
        disable axi_wdata_comb_func;
    end

    always_comb begin : hit_comb_func  // hit_comb_func
        logic[63:0] i;
        hit_comb=0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (tag_ram__q_out[i][(TAG_BITS + 'h1)] && (tag_ram__q_out[i]['h0 +:(TAG_BITS - 'h1) - 'h0 + 1] == req_tag_comb)) begin
                hit_comb=1;
            end
        end
        disable hit_comb_func;
    end

    always_comb begin : hit_way_comb_func  // hit_way_comb_func
        logic[63:0] i;
        hit_way_comb = 'h0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (tag_ram__q_out[i][(TAG_BITS + 'h1)] && (tag_ram__q_out[i]['h0 +:(TAG_BITS - 'h1) - 'h0 + 1] == req_tag_comb)) begin
                hit_way_comb = unsigned'(2'(i));
            end
        end
        disable hit_way_comb_func;
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
        disable hit_aligned_word_comb_func;
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
        disable hit_aligned_next_word_comb_func;
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
        disable write_word_comb_func;
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
        disable write_next_word_comb_func;
    end

    always_comb begin : axi_aligned_word_comb_func  // axi_aligned_word_comb_func
        logic[31:0] word;
        word=unsigned'(32'(req_word_comb)) % PORT_WORDS;
        axi_aligned_word_comb=unsigned'(32'(axi_rdata_selected_comb[word*'h20 +:32]));
        disable axi_aligned_word_comb_func;
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
        disable fill_write_word_comb_func;
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
        disable fill_write_next_word_comb_func;
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
        disable hit_beat_comb_func;
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
        disable cross_read_data_comb_func;
    end

    always_comb begin : tag_write_data_comb_func  // tag_write_data_comb_func
        if (state_reg == ST_INIT) begin
            tag_write_data_comb = 'h0;
        end
        else begin
            tag_write_data_comb = ((((unsigned'(64'('h1)) <<< ((TAG_BITS + 'h1)))) | ((unsigned'(64'(req_write_reg)) <<< TAG_BITS))) | unsigned'(64'(req_tag_comb)));
        end
        disable tag_write_data_comb_func;
    end

    generate  // _assign
        for (gi='h0;gi < DATA_BANKS;gi=gi+1) begin
            assign data_ram__addr_in[gi] = ((state_reg == ST_IDLE)) ? (active_set_comb) : (req_set_comb);
            assign data_ram__rd_in[gi] = (((state_reg == ST_IDLE) && ((active_read_comb || active_write_comb)))) || (state_reg == ST_CROSS_WRITE_LOOKUP);
            assign data_ram__wr_in[gi] = (((((((state_reg == ST_AXI_R) && axi_rvalid_selected_comb) && axi_rready_comb) && (fill_way_reg == ((gi/LINE_WORDS)))) && (gi % LINE_WORDS)>=(unsigned'(32'(fill_beat_reg))*PORT_WORDS)) && (((gi % LINE_WORDS)) < (((unsigned'(32'(fill_beat_reg)) + 'h1))*PORT_WORDS)))) || ((((((((state_reg == ST_LOOKUP) || (state_reg == ST_CROSS_WRITE_LOOKUP))) && req_write_reg) && hit_comb) && (hit_way_comb == ((gi/LINE_WORDS)))) && (((req_word_comb == ((gi % LINE_WORDS))) || (((((unsigned'(32'(req_addr_reg)) & 'h3)) != 'h0) && ((req_word_comb + 'h1) == ((gi % LINE_WORDS)))))))));
            assign data_ram__data_in[gi] = (((state_reg == ST_LOOKUP) || (state_reg == ST_CROSS_WRITE_LOOKUP))) ? (((((((unsigned'(32'(req_addr_reg)) & 'h3)) != 'h0) && ((req_word_comb + 'h1) == ((gi % LINE_WORDS))))) ? (write_next_word_comb) : (write_word_comb))) : ((((req_write_reg && (req_word_comb == ((gi % LINE_WORDS))))) ? (fill_write_word_comb) : ((((req_write_reg && (((unsigned'(32'(req_addr_reg)) & 'h3)) != 'h0)) && ((req_word_comb + 'h1) == ((gi % LINE_WORDS))))) ? (fill_write_next_word_comb) : (unsigned'(32'(axi_rdata_selected_comb[((((gi % LINE_WORDS)) % PORT_WORDS))*'h20 +:(((gi % LINE_WORDS) % PORT_WORDS)*'h20) + 'h1F - ((gi % LINE_WORDS) % PORT_WORDS)*'h20 + 1]))))));
            assign data_ram__id_in[gi]='h7D0 + gi;
        end
        for (gi='h0;gi < WAYS;gi=gi+1) begin
            assign tag_ram__addr_in[gi] = ((state_reg == ST_INIT)) ? (init_set_reg) : ((((state_reg == ST_IDLE)) ? (active_set_comb) : (req_set_comb)));
            assign tag_ram__rd_in[gi] = (((state_reg == ST_IDLE) && ((active_read_comb || active_write_comb)))) || (state_reg == ST_CROSS_WRITE_LOOKUP);
            assign tag_ram__wr_in[gi] = (((state_reg == ST_INIT)) || ((((((state_reg == ST_AXI_R) && axi_rvalid_selected_comb) && axi_rready_comb) && (fill_beat_reg == (LINE_BEATS - 'h1))) && (fill_way_reg == gi)))) || (((((((state_reg == ST_LOOKUP) || (state_reg == ST_CROSS_WRITE_LOOKUP))) && req_write_reg) && hit_comb) && (hit_way_comb == gi)));
            assign tag_ram__data_in[gi] = tag_write_data_comb;
            assign tag_ram__id_in[gi]='h834 + gi;
        end
        for (gi='h0;gi < MEM_PORTS;gi=gi+1) begin
            assign axi_out__awvalid_out[gi] = axi_awvalid_comb && (axi_aw_sel_comb == gi);
            assign axi_out__awaddr_out[gi] = axi_awaddr_local_comb;
            assign axi_out__awid_out[gi] = unsigned'(4'('h0));
            assign axi_out__wvalid_out[gi] = axi_wvalid_comb && (axi_aw_sel_comb == gi);
            assign axi_out__wdata_out[gi] = axi_wdata_comb;
            assign axi_out__wlast_out[gi] = axi_wvalid_comb && (axi_aw_sel_comb == gi);
            assign axi_out__bready_out[gi] = axi_aw_sel_comb == gi;
            assign axi_out__arvalid_out[gi] = axi_arvalid_comb && (axi_ar_sel_comb == gi);
            assign axi_out__araddr_out[gi] = axi_araddr_local_comb;
            assign axi_out__arid_out[gi] = unsigned'(4'('h0));
            assign axi_out__rready_out[gi] = axi_rready_comb && (axi_ar_sel_comb == gi);
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        logic[63:0] way;
        for (i='h0;i < DATA_BANKS;i=i+1) begin
        end
        for (way='h0;way < WAYS;way=way+1) begin
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
                    req_addr_reg_tmp = active_addr_comb;
                    req_write_data_reg_tmp = active_write_data_comb;
                    req_write_mask_reg_tmp = active_write_mask_comb;
                    req_read_reg_tmp = active_read_comb;
                    req_write_reg_tmp = active_write_comb;
                    req_port_reg_tmp = active_is_d_comb;
                    state_reg_tmp = (active_cross_line_read_comb) ? (ST_CROSS_AR0) : (ST_LOOKUP);
                end
            end
            else begin
                if (state_reg == ST_LOOKUP) begin
                    if (!req_addr_in_memory_comb) begin
                        if (req_read_reg) begin
                            last_data_reg_tmp = 'h0;
                        end
                        state_reg_tmp = ST_DONE;
                    end
                    else begin
                        if (req_uncached_region_comb) begin
                            state_reg_tmp = (req_read_reg) ? (ST_IO_AR) : (ST_IO_AW);
                        end
                        else begin
                            if (hit_comb) begin
                                if (req_read_reg) begin
                                    last_data_reg_tmp = hit_beat_comb;
                                end
                                if (req_cross_line_write_comb) begin
                                    req_addr_reg_tmp = ((unsigned'(32'(req_addr_reg)) & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) + CACHE_LINE_SIZE;
                                    req_write_data_reg_tmp = cross_write_data_comb;
                                    req_write_mask_reg_tmp = cross_write_mask_comb;
                                    state_reg_tmp = ST_CROSS_WRITE_LOOKUP;
                                end
                                else begin
                                    state_reg_tmp = (req_write_reg) ? (ST_DONE) : (ST_IDLE);
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
                end
                else begin
                    if (state_reg == ST_CROSS_WRITE_LOOKUP) begin
                        if (!req_addr_in_memory_comb) begin
                            state_reg_tmp = ST_DONE;
                        end
                        else begin
                            if (hit_comb) begin
                                state_reg_tmp = ST_DONE;
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
                                                if (req_read_reg && (fill_beat_reg == req_beat_comb)) begin
                                                    last_data_reg_tmp = axi_rdata_selected_comb;
                                                end
                                                if (fill_beat_reg == (LINE_BEATS - 'h1)) begin
                                                    victim_reg_tmp = victim_reg + 'h1;
                                                    if (req_cross_line_write_comb) begin
                                                        req_addr_reg_tmp = ((unsigned'(32'(req_addr_reg)) & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) + CACHE_LINE_SIZE;
                                                        req_write_data_reg_tmp = cross_write_data_comb;
                                                        req_write_mask_reg_tmp = cross_write_mask_comb;
                                                        state_reg_tmp = ST_CROSS_WRITE_LOOKUP;
                                                    end
                                                    else begin
                                                        state_reg_tmp = ST_DONE;
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
                                                                last_data_reg_tmp = cross_read_data_comb;
                                                                state_reg_tmp = ST_DONE;
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
                                                                                state_reg_tmp = ST_DONE;
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
                                                                                        last_data_reg_tmp = axi_rdata_selected_comb;
                                                                                        state_reg_tmp = ST_DONE;
                                                                                    end
                                                                                end
                                                                                else begin
                                                                                    if (state_reg == ST_DONE) begin
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
            victim_reg_tmp = '0;
            fill_way_reg_tmp = '0;
            init_set_reg_tmp = '0;
            last_data_reg_tmp = '0;
            cross_low_reg_tmp = '0;
            cross_high_reg_tmp = '0;
            fill_beat_reg_tmp = '0;
            evict_beat_reg_tmp = '0;
            state_reg_tmp = ST_INIT;
        end
    end
    endtask

    always_comb begin : read_data_comb_func  // read_data_comb_func
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
                    read_data_comb=axi_rdata_selected_comb;
                end
            end
        end
        disable read_data_comb_func;
    end

    always_comb begin : i_wait_comb_func  // i_wait_comb_func
        i_wait_comb=0;
        if (i_read_in) begin
            i_wait_comb=1;
            if ((((state_reg == ST_LOOKUP) && !req_port_reg) && req_read_reg) && hit_comb) begin
                i_wait_comb=0;
            end
            if (((state_reg == ST_DONE) && !req_port_reg) && req_read_reg) begin
                i_wait_comb=0;
            end
        end
        if (((state_reg != ST_IDLE) && !(((((state_reg == ST_LOOKUP) && !req_port_reg) && req_read_reg) && hit_comb))) && !((((state_reg == ST_DONE) && !req_port_reg) && req_read_reg))) begin
            i_wait_comb=1;
        end
        if (d_read_in || d_write_in) begin
            i_wait_comb=1;
        end
        disable i_wait_comb_func;
    end

    always_comb begin : d_wait_comb_func  // d_wait_comb_func
        d_wait_comb=0;
        if (d_write_in) begin
            d_wait_comb=1;
            if (((state_reg == ST_DONE) && req_port_reg) && req_write_reg) begin
                d_wait_comb=0;
            end
        end
        if (d_read_in) begin
            d_wait_comb=1;
            if ((((state_reg == ST_LOOKUP) && req_port_reg) && req_read_reg) && hit_comb) begin
                d_wait_comb=0;
            end
            if (((state_reg == ST_DONE) && req_port_reg) && req_read_reg) begin
                d_wait_comb=0;
            end
        end
        if (((state_reg != ST_IDLE) && !(((((state_reg == ST_LOOKUP) && req_port_reg) && req_read_reg) && hit_comb))) && !((((state_reg == ST_DONE) && req_port_reg) && ((req_read_reg || req_write_reg))))) begin
            d_wait_comb=1;
        end
        disable d_wait_comb_func;
    end

    always @(posedge clk) begin
        state_reg_tmp = state_reg;
        req_addr_reg_tmp = req_addr_reg;
        req_write_data_reg_tmp = req_write_data_reg;
        req_write_mask_reg_tmp = req_write_mask_reg;
        req_read_reg_tmp = req_read_reg;
        req_write_reg_tmp = req_write_reg;
        req_port_reg_tmp = req_port_reg;
        victim_reg_tmp = victim_reg;
        fill_way_reg_tmp = fill_way_reg;
        init_set_reg_tmp = init_set_reg;
        last_data_reg_tmp = last_data_reg;
        cross_low_reg_tmp = cross_low_reg;
        cross_high_reg_tmp = cross_high_reg;
        fill_beat_reg_tmp = fill_beat_reg;
        evict_beat_reg_tmp = evict_beat_reg;

        _work(reset);

        state_reg <= state_reg_tmp;
        req_addr_reg <= req_addr_reg_tmp;
        req_write_data_reg <= req_write_data_reg_tmp;
        req_write_mask_reg <= req_write_mask_reg_tmp;
        req_read_reg <= req_read_reg_tmp;
        req_write_reg <= req_write_reg_tmp;
        req_port_reg <= req_port_reg_tmp;
        victim_reg <= victim_reg_tmp;
        fill_way_reg <= fill_way_reg_tmp;
        init_set_reg <= init_set_reg_tmp;
        last_data_reg <= last_data_reg_tmp;
        cross_low_reg <= cross_low_reg_tmp;
        cross_high_reg <= cross_high_reg_tmp;
        fill_beat_reg <= fill_beat_reg_tmp;
        evict_beat_reg <= evict_beat_reg_tmp;
    end

    assign i_read_data_out = read_data_comb;

    assign i_wait_out = i_wait_comb;

    assign d_read_data_out = read_data_comb;

    assign d_wait_out = d_wait_comb;


endmodule

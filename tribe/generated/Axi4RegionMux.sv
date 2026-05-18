`default_nettype none

import Predef_pkg::*;


module Axi4RegionMux #(
    parameter N
,   parameter ADDR_WIDTH
,   parameter ID_WIDTH
,   parameter DATA_WIDTH
 )
 (
    input wire clk
,   input wire reset
,   input wire slave_in__awvalid_in
,   output wire slave_in__awready_out
,   input wire[ADDR_WIDTH-1:0] slave_in__awaddr_in
,   input wire[ID_WIDTH-1:0] slave_in__awid_in
,   input wire slave_in__wvalid_in
,   output wire slave_in__wready_out
,   input wire[DATA_WIDTH-1:0] slave_in__wdata_in
,   input wire slave_in__wlast_in
,   output wire slave_in__bvalid_out
,   input wire slave_in__bready_in
,   output wire[ID_WIDTH-1:0] slave_in__bid_out
,   input wire slave_in__arvalid_in
,   output wire slave_in__arready_out
,   input wire[ADDR_WIDTH-1:0] slave_in__araddr_in
,   input wire[ID_WIDTH-1:0] slave_in__arid_in
,   output wire slave_in__rvalid_out
,   input wire slave_in__rready_in
,   output wire[DATA_WIDTH-1:0] slave_in__rdata_out
,   output wire slave_in__rlast_out
,   output wire[ID_WIDTH-1:0] slave_in__rid_out
,   output wire masters_out__awvalid_out[N]
,   input wire masters_out__awready_in[N]
,   output wire[ADDR_WIDTH-1:0] masters_out__awaddr_out[N]
,   output wire[ID_WIDTH-1:0] masters_out__awid_out[N]
,   output wire masters_out__wvalid_out[N]
,   input wire masters_out__wready_in[N]
,   output wire[DATA_WIDTH-1:0] masters_out__wdata_out[N]
,   output wire masters_out__wlast_out[N]
,   input wire masters_out__bvalid_in[N]
,   output wire masters_out__bready_out[N]
,   input wire[ID_WIDTH-1:0] masters_out__bid_in[N]
,   output wire masters_out__arvalid_out[N]
,   input wire masters_out__arready_in[N]
,   output wire[ADDR_WIDTH-1:0] masters_out__araddr_out[N]
,   output wire[ID_WIDTH-1:0] masters_out__arid_out[N]
,   input wire masters_out__rvalid_in[N]
,   output wire masters_out__rready_out[N]
,   input wire[DATA_WIDTH-1:0] masters_out__rdata_in[N]
,   input wire masters_out__rlast_in[N]
,   input wire[ID_WIDTH-1:0] masters_out__rid_in[N]
,   input wire[31:0] region_base_in[N]
,   input wire[31:0] region_size_in[N]
);
    parameter  SEL_BITS = (N<='h1) ? ('h1) : ($clog2(N));


    // regs and combs
    reg[SEL_BITS-1:0] aw_sel_reg;
    reg[SEL_BITS-1:0] ar_sel_reg;
    reg aw_active_reg;
    reg ar_active_reg;
    logic[31:0] aw_sel_safe_comb;
;
    logic[31:0] ar_sel_safe_comb;
;
    logic[31:0] aw_next_comb;
;
    logic[31:0] ar_next_comb;
;
    logic[ADDR_WIDTH-1:0] aw_local_addr_comb;
;
    logic[ADDR_WIDTH-1:0] ar_local_addr_comb;
;
    logic awready_comb;
;
    logic arready_comb;
;

    // members

    // tmp variables
    logic[SEL_BITS-1:0] aw_sel_reg_tmp;
    logic[SEL_BITS-1:0] ar_sel_reg_tmp;
    logic aw_active_reg_tmp;
    logic ar_active_reg_tmp;


    always_comb begin : aw_sel_safe_comb_func  // aw_sel_safe_comb_func
        logic[31:0] sel;
        sel=unsigned'(32'(aw_sel_reg));
        if (sel>=N) begin
            sel='h0;
        end
        aw_sel_safe_comb=sel;
    end

    always_comb begin : ar_sel_safe_comb_func  // ar_sel_safe_comb_func
        logic[31:0] sel;
        sel=unsigned'(32'(ar_sel_reg));
        if (sel>=N) begin
            sel='h0;
        end
        ar_sel_safe_comb=sel;
    end

    always_comb begin : aw_next_comb_func  // aw_next_comb_func
        logic[63:0] i;
        logic[31:0] addr;
        aw_next_comb='h0;
        addr=slave_in__awaddr_in;
        for (i='h0;i < N;i=i+1) begin
            if (addr>=region_base_in[i] && ((addr - region_base_in[i]) < region_size_in[i])) begin
                aw_next_comb=i;
            end
        end
    end

    always_comb begin : ar_next_comb_func  // ar_next_comb_func
        logic[63:0] i;
        logic[31:0] addr;
        ar_next_comb='h0;
        addr=slave_in__araddr_in;
        for (i='h0;i < N;i=i+1) begin
            if (addr>=region_base_in[i] && ((addr - region_base_in[i]) < region_size_in[i])) begin
                ar_next_comb=i;
            end
        end
    end

    always_comb begin : aw_local_addr_comb_func  // aw_local_addr_comb_func
        logic[31:0] sel;
        sel=(aw_active_reg) ? (aw_sel_safe_comb) : (aw_next_comb);
        aw_local_addr_comb = unsigned'(ADDR_WIDTH'(unsigned'(ADDR_WIDTH'((slave_in__awaddr_in - region_base_in[sel])))));
    end

    always_comb begin : ar_local_addr_comb_func  // ar_local_addr_comb_func
        logic[31:0] sel;
        sel=(ar_active_reg) ? (ar_sel_safe_comb) : (ar_next_comb);
        ar_local_addr_comb = unsigned'(ADDR_WIDTH'(unsigned'(ADDR_WIDTH'((slave_in__araddr_in - region_base_in[sel])))));
    end

    always_comb begin : awready_comb_func  // awready_comb_func
        logic[63:0] i;
        awready_comb=0;
        for (i='h0;i < N;i=i+1) begin
            if (aw_next_comb == i) begin
                awready_comb=masters_out__awready_in[i];
            end
        end
    end

    always_comb begin : arready_comb_func  // arready_comb_func
        logic[63:0] i;
        arready_comb=0;
        for (i='h0;i < N;i=i+1) begin
            if (ar_next_comb == i) begin
                arready_comb=masters_out__arready_in[i];
            end
        end
    end

    generate  // _assign
        genvar gi;
        for (gi='h0;gi < N;gi=gi+1) begin
            assign masters_out__awvalid_out[gi] = (!aw_active_reg && (aw_next_comb == gi)) && slave_in__awvalid_in;
            assign masters_out__awaddr_out[gi] = aw_local_addr_comb;
            assign masters_out__awid_out[gi] = slave_in__awid_in;
            assign masters_out__wvalid_out[gi] = (aw_active_reg && (aw_sel_safe_comb == gi)) && slave_in__wvalid_in;
            assign masters_out__wdata_out[gi] = slave_in__wdata_in;
            assign masters_out__wlast_out[gi] = slave_in__wlast_in;
            assign masters_out__bready_out[gi] = (aw_active_reg && (aw_sel_safe_comb == gi)) && slave_in__bready_in;
            assign masters_out__arvalid_out[gi] = (!ar_active_reg && (ar_next_comb == gi)) && slave_in__arvalid_in;
            assign masters_out__araddr_out[gi] = ar_local_addr_comb;
            assign masters_out__arid_out[gi] = slave_in__arid_in;
            assign masters_out__rready_out[gi] = (ar_active_reg && (ar_sel_safe_comb == gi)) && slave_in__rready_in;
        end
        assign slave_in__awready_out = !aw_active_reg && awready_comb;
        assign slave_in__wready_out = (aw_active_reg) ? (masters_out__wready_in[aw_sel_safe_comb]) : (0);
        assign slave_in__bvalid_out = (aw_active_reg) ? (masters_out__bvalid_in[aw_sel_safe_comb]) : (0);
        assign slave_in__bid_out = unsigned'(4'(masters_out__bid_in[aw_sel_safe_comb]));
        assign slave_in__arready_out = !ar_active_reg && arready_comb;
        assign slave_in__rvalid_out = (ar_active_reg) ? (masters_out__rvalid_in[ar_sel_safe_comb]) : (0);
        assign slave_in__rdata_out = masters_out__rdata_in[ar_sel_safe_comb];
        assign slave_in__rlast_out = (ar_active_reg) ? (masters_out__rlast_in[ar_sel_safe_comb]) : (0);
        assign slave_in__rid_out = unsigned'(4'(masters_out__rid_in[ar_sel_safe_comb]));
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            aw_sel_reg_tmp = '0;
            ar_sel_reg_tmp = '0;
            aw_active_reg_tmp = '0;
            ar_active_reg_tmp = '0;
            disable _work;
        end
        if ((!aw_active_reg && slave_in__awvalid_in) && slave_in__awready_out) begin
            aw_active_reg_tmp = unsigned'(1'(1));
            aw_sel_reg_tmp = aw_next_comb;
        end
        if ((aw_active_reg && slave_in__bvalid_out) && slave_in__bready_in) begin
            aw_active_reg_tmp = unsigned'(1'(0));
        end
        if ((!ar_active_reg && slave_in__arvalid_in) && slave_in__arready_out) begin
            ar_active_reg_tmp = unsigned'(1'(1));
            ar_sel_reg_tmp = ar_next_comb;
        end
        if (((ar_active_reg && slave_in__rvalid_out) && slave_in__rready_in) && slave_in__rlast_out) begin
            ar_active_reg_tmp = unsigned'(1'(0));
        end
    end
    endtask

    always @(posedge clk) begin
        aw_sel_reg_tmp = aw_sel_reg;
        ar_sel_reg_tmp = ar_sel_reg;
        aw_active_reg_tmp = aw_active_reg;
        ar_active_reg_tmp = ar_active_reg;

        _work(reset);

        aw_sel_reg <= aw_sel_reg_tmp;
        ar_sel_reg <= ar_sel_reg_tmp;
        aw_active_reg <= aw_active_reg_tmp;
        ar_active_reg <= ar_active_reg_tmp;
    end


endmodule

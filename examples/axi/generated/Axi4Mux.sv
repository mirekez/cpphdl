`default_nettype none

import Predef_pkg::*;


module Axi4Mux #(
    parameter N
,   parameter ADDR_WIDTH
,   parameter ID_WIDTH
,   parameter DATA_WIDTH
 )
 (
    input wire clk
,   input wire reset
,   input wire slaves_in__awvalid_in[N]
,   output wire slaves_in__awready_out[N]
,   input wire[ADDR_WIDTH-1:0] slaves_in__awaddr_in[N]
,   input wire[ID_WIDTH-1:0] slaves_in__awid_in[N]
,   input wire slaves_in__wvalid_in[N]
,   output wire slaves_in__wready_out[N]
,   input wire[DATA_WIDTH-1:0] slaves_in__wdata_in[N]
,   input wire slaves_in__wlast_in[N]
,   output wire slaves_in__bvalid_out[N]
,   input wire slaves_in__bready_in[N]
,   output wire[ID_WIDTH-1:0] slaves_in__bid_out[N]
,   input wire slaves_in__arvalid_in[N]
,   output wire slaves_in__arready_out[N]
,   input wire[ADDR_WIDTH-1:0] slaves_in__araddr_in[N]
,   input wire[ID_WIDTH-1:0] slaves_in__arid_in[N]
,   output wire slaves_in__rvalid_out[N]
,   input wire slaves_in__rready_in[N]
,   output wire[DATA_WIDTH-1:0] slaves_in__rdata_out[N]
,   output wire slaves_in__rlast_out[N]
,   output wire[ID_WIDTH-1:0] slaves_in__rid_out[N]
,   output wire master_out__awvalid_out
,   input wire master_out__awready_in
,   output wire[ADDR_WIDTH-1:0] master_out__awaddr_out
,   output wire[ID_WIDTH-1:0] master_out__awid_out
,   output wire master_out__wvalid_out
,   input wire master_out__wready_in
,   output wire[DATA_WIDTH-1:0] master_out__wdata_out
,   output wire master_out__wlast_out
,   input wire master_out__bvalid_in
,   output wire master_out__bready_out
,   input wire[ID_WIDTH-1:0] master_out__bid_in
,   output wire master_out__arvalid_out
,   input wire master_out__arready_in
,   output wire[ADDR_WIDTH-1:0] master_out__araddr_out
,   output wire[ID_WIDTH-1:0] master_out__arid_out
,   input wire master_out__rvalid_in
,   output wire master_out__rready_out
,   input wire[DATA_WIDTH-1:0] master_out__rdata_in
,   input wire master_out__rlast_in
,   input wire[ID_WIDTH-1:0] master_out__rid_in
,   input wire debugen_in
);


    // regs and combs
    reg[$clog2(N)-1:0] rr_aw;
    reg[$clog2(N)-1:0] rr_ar;
    reg[$clog2(N)-1:0] aw_sel;
    reg[$clog2(N)-1:0] ar_sel;
    reg aw_active;
    reg ar_active;
    logic[$clog2(N)-1:0] aw_next_comb;
    logic[$clog2(N)-1:0] ar_next_comb;

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[$clog2(N)-1:0] rr_aw_tmp;
    logic[$clog2(N)-1:0] rr_ar_tmp;
    logic[$clog2(N)-1:0] aw_sel_tmp;
    logic[$clog2(N)-1:0] ar_sel_tmp;
    logic aw_active_tmp;
    logic ar_active_tmp;


    always @(*) begin  // aw_next_comb_func
        logic[8-1:0] i;
        logic[8-1:0] idx; idx = rr_aw;
        for (i = 0;i < N;i++) begin
            if (slaves_in__awvalid_in[idx]) begin
                idx = ((rr_aw + i)) % N;
            end
        end
        aw_next_comb = idx;
    end

    always @(*) begin  // ar_next_comb_func
        logic[8-1:0] i;
        logic[8-1:0] idx; idx = rr_ar;
        for (i = 0;i < N;i++) begin
            if (slaves_in__arvalid_in[idx]) begin
                idx = ((rr_ar + i)) % N;
            end
        end
        ar_next_comb = idx;
    end

    generate  // _assign
        assign master_out__awvalid_out = !aw_active && slaves_in__awvalid_in[aw_next_comb];
        assign master_out__awaddr_out = slaves_in__awaddr_in[aw_next_comb];
        assign master_out__awid_out = slaves_in__awid_in[aw_next_comb];
        for (gi = 0;gi < N;gi++) begin
            assign slaves_in__awready_out[gi] = ((!aw_active && (aw_next_comb == gi))) ? (master_out__awready_in) : (0);
        end
        assign master_out__wvalid_out = (aw_active) ? (slaves_in__wvalid_in[aw_sel]) : (0);
        assign master_out__wdata_out = slaves_in__wdata_in[aw_sel];
        assign master_out__wlast_out = slaves_in__wlast_in[aw_sel];
        for (gi = 0;gi < N;gi++) begin
            assign slaves_in__wready_out[gi] = ((aw_active && (aw_sel == gi))) ? (master_out__wready_in) : (0);
        end
        assign master_out__bready_out = slaves_in__bready_in[aw_sel];
        for (gi = 0;gi < N;gi++) begin
            assign slaves_in__bvalid_out[gi] = ((aw_sel == gi)) ? (master_out__bvalid_in) : (0);
            assign slaves_in__bid_out[gi] = master_out__bid_in;
        end
        assign master_out__arvalid_out = !ar_active && slaves_in__arvalid_in[ar_next_comb];
        assign master_out__araddr_out = slaves_in__araddr_in[ar_next_comb];
        assign master_out__arid_out = slaves_in__arid_in[ar_next_comb];
        for (gi = 0;gi < N;gi++) begin
            assign slaves_in__arready_out[gi] = ((!ar_active && (ar_next_comb == gi))) ? (master_out__arready_in) : (0);
        end
        assign master_out__rready_out = slaves_in__rready_in[ar_sel];
        for (gi = 0;gi < N;gi++) begin
            assign slaves_in__rvalid_out[gi] = ((ar_sel == gi)) ? (master_out__rvalid_in) : (0);
            assign slaves_in__rdata_out[gi] = master_out__rdata_in;
            assign slaves_in__rlast_out[gi] = master_out__rlast_in;
            assign slaves_in__rid_out[gi] = master_out__rid_in;
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        if ((!ar_active && master_out__arvalid_out) && master_out__arready_in) begin
            ar_active_tmp = 1;
            ar_sel_tmp = ar_next_comb;
            rr_ar_tmp = ar_next_comb + 1;
        end
        if ((master_out__rvalid_in && master_out__rready_out) && master_out__rlast_in) begin
            ar_active_tmp = 0;
        end
        if ((!aw_active && master_out__awvalid_out) && master_out__awready_in) begin
            aw_active_tmp = 1;
            aw_sel_tmp = aw_next_comb;
            rr_aw_tmp = aw_next_comb + 1;
        end
        if (master_out__bvalid_in && master_out__bready_out) begin
            aw_active_tmp = 0;
        end
        if (reset) begin
            ar_active_tmp = 0;
            rr_ar_tmp = 0;
            aw_active_tmp = 0;
            rr_aw_tmp = 0;
        end
    end
    endtask

    always @(posedge clk) begin
        _work(reset);

        rr_aw <= rr_aw_tmp;
        rr_ar <= rr_ar_tmp;
        aw_sel <= aw_sel_tmp;
        ar_sel <= ar_sel_tmp;
        aw_active <= aw_active_tmp;
        ar_active <= ar_active_tmp;
    end


endmodule

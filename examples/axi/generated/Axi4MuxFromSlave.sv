`default_nettype none

import Predef_pkg::*;


module Axi4MuxFromSlave #(
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
,   input wire debugen_in
);


    // regs and combs
    reg[$clog2(N)-1:0] aw_sel;
    reg[$clog2(N)-1:0] ar_sel;
    reg aw_active;
    reg ar_active;
    logic awready_comb;
    logic arready_comb;

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[$clog2(N)-1:0] aw_sel_tmp;
    logic[$clog2(N)-1:0] ar_sel_tmp;
    logic aw_active_tmp;
    logic ar_active_tmp;


    always @(*) begin  // awready_comb_func
        logic[8-1:0] i;
        logic ret;
        ret=0;
        for (i = 'h0;i < N;i++) begin
            if (((slave_in__awaddr_in % N)) == i) begin
                ret=masters_out__awready_in[i];
            end
        end
        awready_comb=ret;
    end

    always @(*) begin  // arready_comb_func
        logic[8-1:0] i;
        logic ret;
        ret=0;
        for (i = 'h0;i < N;i++) begin
            if (((slave_in__araddr_in % N)) == i) begin
                ret=masters_out__arready_in[i];
            end
        end
        arready_comb=ret;
    end

    generate  // _assign
        for (gi = 'h0;gi < N;gi++) begin
            assign masters_out__awvalid_out[gi] = ((!aw_active && (((slave_in__awaddr_in % N)) == gi))) ? (slave_in__awvalid_in) : ('h0);
            assign masters_out__awaddr_out[gi] = slave_in__awaddr_in;
            assign masters_out__awid_out[gi] = slave_in__awid_in;
        end
        assign slave_in__awready_out = (!aw_active) ? (awready_comb) : ('h0);
        for (gi = 'h0;gi < N;gi++) begin
            assign masters_out__wvalid_out[gi] = ((aw_active && (aw_sel == gi))) ? (slave_in__wvalid_in) : ('h0);
            assign masters_out__wdata_out[gi] = slave_in__wdata_in;
            assign masters_out__wlast_out[gi] = slave_in__wlast_in;
        end
        assign slave_in__wready_out = (aw_active) ? (masters_out__wready_in[aw_sel]) : ('h0);
        for (gi = 'h0;gi < N;gi++) begin
            assign masters_out__bready_out[gi] = ((aw_sel == gi)) ? (slave_in__bready_in) : ('h0);
        end
        assign slave_in__bvalid_out = (aw_active) ? (masters_out__bvalid_in[aw_sel]) : ('h0);
        assign slave_in__bid_out = masters_out__bid_in[aw_sel];
        for (gi = 'h0;gi < N;gi++) begin
            assign masters_out__arvalid_out[gi] = ((!ar_active && (((slave_in__araddr_in % N)) == gi))) ? (slave_in__arvalid_in) : ('h0);
            assign masters_out__araddr_out[gi] = slave_in__araddr_in;
            assign masters_out__arid_out[gi] = slave_in__arid_in;
        end
        assign slave_in__arready_out = (!ar_active) ? (arready_comb) : ('h0);
        for (gi = 'h0;gi < N;gi++) begin
            assign masters_out__rready_out[gi] = ((ar_sel == gi)) ? (slave_in__rready_in) : ('h0);
        end
        assign slave_in__rvalid_out = (ar_active) ? (masters_out__rvalid_in[ar_sel]) : ('h0);
        assign slave_in__rdata_out = masters_out__rdata_in[ar_sel];
        assign slave_in__rlast_out = masters_out__rlast_in[ar_sel];
        assign slave_in__rid_out = masters_out__rid_in[ar_sel];
    endgenerate

    task _work (input logic reset);
    begin: _work
        if ((!ar_active && slave_in__arvalid_in) && slave_in__arready_out) begin
            ar_active_tmp = 'h1;
            ar_sel_tmp = slave_in__araddr_in % N;
        end
        if ((slave_in__rvalid_out && slave_in__rready_in) && slave_in__rlast_out) begin
            ar_active_tmp = 'h0;
        end
        if ((!aw_active && slave_in__awvalid_in) && slave_in__awready_out) begin
            aw_active_tmp = 'h1;
            aw_sel_tmp = slave_in__awaddr_in % N;
        end
        if (slave_in__bvalid_out && slave_in__bready_in) begin
            aw_active_tmp = 'h0;
        end
        if (reset) begin
            ar_active_tmp = 'h0;
            aw_active_tmp = 'h0;
        end
    end
    endtask

    always @(posedge clk) begin
        aw_sel_tmp = aw_sel;
        ar_sel_tmp = ar_sel;
        aw_active_tmp = aw_active;
        ar_active_tmp = ar_active;

        _work(reset);

        aw_sel <= aw_sel_tmp;
        ar_sel <= ar_sel_tmp;
        aw_active <= aw_active_tmp;
        ar_active <= ar_active_tmp;
    end


endmodule

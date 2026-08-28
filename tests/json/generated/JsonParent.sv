`default_nettype none

import Predef_pkg::*;
import JsonPayload_pkg::*;


module JsonParent (
    input wire clk
,   input wire reset
,   input wire start_in
,   input wire JsonPayload payload_in
,   input wire axi__awvalid_in
,   output wire axi__awready_out
,   input wire[16-1:0] axi__awaddr_in
,   input wire[4-1:0] axi__awid_in
,   input wire axi__wvalid_in
,   output wire axi__wready_out
,   input wire[32-1:0] axi__wdata_in
,   input wire[32/'h8-1:0] axi__wstrb_in
,   input wire axi__wlast_in
,   output wire axi__bvalid_out
,   input wire axi__bready_in
,   output wire[4-1:0] axi__bid_out
,   input wire axi__arvalid_in
,   output wire axi__arready_out
,   input wire[16-1:0] axi__araddr_in
,   input wire[4-1:0] axi__arid_in
,   output wire axi__rvalid_out
,   input wire axi__rready_in
,   output wire[32-1:0] axi__rdata_out
,   output wire axi__rlast_out
,   output wire[4-1:0] axi__rid_out
,   output wire[17-1:0] result_out
);


    // regs and combs
    logic[17-1:0] result_comb;

    // members
    wire leaf__start_in;
    wire JsonPayload leaf__payload_in;
    wire leaf__axi__awvalid_in;
    wire leaf__axi__awready_out;
    wire[16-1:0] leaf__axi__awaddr_in;
    wire[4-1:0] leaf__axi__awid_in;
    wire leaf__axi__wvalid_in;
    wire leaf__axi__wready_out;
    wire[32-1:0] leaf__axi__wdata_in;
    wire[32/'h8-1:0] leaf__axi__wstrb_in;
    wire leaf__axi__wlast_in;
    wire leaf__axi__bvalid_out;
    wire leaf__axi__bready_in;
    wire[4-1:0] leaf__axi__bid_out;
    wire leaf__axi__arvalid_in;
    wire leaf__axi__arready_out;
    wire[16-1:0] leaf__axi__araddr_in;
    wire[4-1:0] leaf__axi__arid_in;
    wire leaf__axi__rvalid_out;
    wire leaf__axi__rready_in;
    wire[32-1:0] leaf__axi__rdata_out;
    wire leaf__axi__rlast_out;
    wire[4-1:0] leaf__axi__rid_out;
    wire[17-1:0] leaf__result_out;
    JsonLeaf      leaf (
        .clk(clk)
,       .reset(reset)
,       .start_in(leaf__start_in)
,       .payload_in(leaf__payload_in)
,       .axi__awvalid_in(leaf__axi__awvalid_in)
,       .axi__awready_out(leaf__axi__awready_out)
,       .axi__awaddr_in(leaf__axi__awaddr_in)
,       .axi__awid_in(leaf__axi__awid_in)
,       .axi__wvalid_in(leaf__axi__wvalid_in)
,       .axi__wready_out(leaf__axi__wready_out)
,       .axi__wdata_in(leaf__axi__wdata_in)
,       .axi__wstrb_in(leaf__axi__wstrb_in)
,       .axi__wlast_in(leaf__axi__wlast_in)
,       .axi__bvalid_out(leaf__axi__bvalid_out)
,       .axi__bready_in(leaf__axi__bready_in)
,       .axi__bid_out(leaf__axi__bid_out)
,       .axi__arvalid_in(leaf__axi__arvalid_in)
,       .axi__arready_out(leaf__axi__arready_out)
,       .axi__araddr_in(leaf__axi__araddr_in)
,       .axi__arid_in(leaf__axi__arid_in)
,       .axi__rvalid_out(leaf__axi__rvalid_out)
,       .axi__rready_in(leaf__axi__rready_in)
,       .axi__rdata_out(leaf__axi__rdata_out)
,       .axi__rlast_out(leaf__axi__rlast_out)
,       .axi__rid_out(leaf__axi__rid_out)
,       .result_out(leaf__result_out)
    );

    // tmp variables


    always_comb begin : result_comb_func  // result_comb_func
        result_comb = leaf__result_out;
    end

    generate  // _assign
        assign leaf__start_in = start_in;
        assign leaf__payload_in = payload_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign result_out = result_comb;


endmodule

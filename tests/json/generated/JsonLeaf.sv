`default_nettype none

import Predef_pkg::*;
import JsonPayload_pkg::*;


module JsonLeaf (
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

    // tmp variables


    always_comb begin : result_comb_func  // result_comb_func
        result_comb = payload_in.data | (payload_in.tag << 'h7);
        if (start_in) begin
            result_comb['h10] = 'h1;
        end
    end

    task _work (input logic unused);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign result_out = result_comb;


endmodule

`default_nettype none

import Predef_pkg::*;


module Axi4Ram #(
    parameter ADDR_WIDTH = 'h20
,   parameter ID_WIDTH = 'h4
,   parameter DATA_WIDTH = 'h100
,   parameter DEPTH = 'h800
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire axi_in__awvalid_in
,   output wire axi_in__awready_out
,   input wire[ADDR_WIDTH-1:0] axi_in__awaddr_in
,   input wire[ID_WIDTH-1:0] axi_in__awid_in
,   input wire axi_in__wvalid_in
,   output wire axi_in__wready_out
,   input wire[DATA_WIDTH-1:0] axi_in__wdata_in
,   input wire[unsigned'(64'(DATA_WIDTH))/64'h8-1:0] axi_in__wstrb_in
,   input wire axi_in__wlast_in
,   output wire axi_in__bvalid_out
,   input wire axi_in__bready_in
,   output wire[ID_WIDTH-1:0] axi_in__bid_out
,   input wire axi_in__arvalid_in
,   output wire axi_in__arready_out
,   input wire[ADDR_WIDTH-1:0] axi_in__araddr_in
,   input wire[ID_WIDTH-1:0] axi_in__arid_in
,   output wire axi_in__rvalid_out
,   input wire axi_in__rready_in
,   output wire[DATA_WIDTH-1:0] axi_in__rdata_out
,   output wire axi_in__rlast_out
,   output wire[ID_WIDTH-1:0] axi_in__rid_out
,   input wire debugen_in
);


    // regs and combs
    reg[ADDR_WIDTH-1:0] read_addr_reg;
    reg[ID_WIDTH-1:0] read_id_reg;
    reg read_valid_reg;
    reg[ADDR_WIDTH-1:0] write_addr_reg;
    reg[ID_WIDTH-1:0] write_id_reg;
    reg write_addr_valid_reg;
    reg write_resp_valid_reg;

    // members
    wire[unsigned'(64'($clog2(unsigned'(32'(DEPTH)))))-1:0] ram__addr0_in;
    wire ram__write0_in;
    wire[(unsigned'(64'(DATA_WIDTH))/64'h8)*64'h8-1:0] ram__write0_data_in;
    wire[unsigned'(64'(DATA_WIDTH))/64'h8-1:0] ram__write0_mask_in;
    wire ram__read0_in;
    wire[(unsigned'(64'(DATA_WIDTH))/64'h8)*64'h8-1:0] ram__read0_data_out;
    wire[unsigned'(64'($clog2(unsigned'(32'(DEPTH)))))-1:0] ram__addr1_in;
    wire ram__write1_in;
    wire[(unsigned'(64'(DATA_WIDTH))/64'h8)*64'h8-1:0] ram__write1_data_in;
    wire[unsigned'(64'(DATA_WIDTH))/64'h8-1:0] ram__write1_mask_in;
    wire ram__read1_in;
    wire[(unsigned'(64'(DATA_WIDTH))/64'h8)*64'h8-1:0] ram__read1_data_out;
    wire ram__debugen_in;
    Memory #(
        unsigned'(64'(DATA_WIDTH))/64'h8
,       DEPTH
,       1
,       'h0
    ) ram (
        .clk(clk)
,       .l2_clock(l2_clock)
,       .reset(reset)
,       .addr0_in(ram__addr0_in)
,       .write0_in(ram__write0_in)
,       .write0_data_in(ram__write0_data_in)
,       .write0_mask_in(ram__write0_mask_in)
,       .read0_in(ram__read0_in)
,       .read0_data_out(ram__read0_data_out)
,       .addr1_in(ram__addr1_in)
,       .write1_in(ram__write1_in)
,       .write1_data_in(ram__write1_data_in)
,       .write1_mask_in(ram__write1_mask_in)
,       .read1_in(ram__read1_in)
,       .read1_data_out(ram__read1_data_out)
,       .debugen_in(ram__debugen_in)
    );

    // tmp variables
    logic[ADDR_WIDTH-1:0] read_addr_reg_tmp;
    logic[ID_WIDTH-1:0] read_id_reg_tmp;
    logic read_valid_reg_tmp;
    logic[ADDR_WIDTH-1:0] write_addr_reg_tmp;
    logic[ID_WIDTH-1:0] write_id_reg_tmp;
    logic write_addr_valid_reg_tmp;
    logic write_resp_valid_reg_tmp;


    generate  // _assign
        assign axi_in__awready_out = ((!((write_addr_valid_reg) != '0) && !((write_resp_valid_reg) != '0)) != '0);
        assign axi_in__wready_out = ((((write_addr_valid_reg) != '0) && !((write_resp_valid_reg) != '0)) != '0);
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = ((!((read_valid_reg) != '0)) != '0);
        assign axi_in__rvalid_out = read_valid_reg;
        assign axi_in__rdata_out = ram__read0_data_out;
        assign axi_in__rlast_out = read_valid_reg;
        assign axi_in__rid_out = read_id_reg;
        assign ram__addr0_in = (unsigned'(64'($clog2(unsigned'(32'(DEPTH))))))'(unsigned'(64'(((((read_valid_reg) != '0)) ? (64'(unsigned'(32'(read_addr_reg)))/((DATA_WIDTH/64'h8))) : (axi_in__araddr_in/((DATA_WIDTH/64'h8)))))));
        assign ram__read0_in = ((axi_in__arvalid_in && axi_in__arready_out) != '0);
        assign ram__write0_in = ((0) != '0);
        assign ram__write0_data_in = DATA_WIDTH'('h0);
        assign ram__write0_mask_in = (DATA_WIDTH/64'h8)'('h0);
        assign ram__addr1_in = (unsigned'(64'($clog2(unsigned'(32'(DEPTH))))))'(64'((64'(unsigned'(32'(write_addr_reg)))/((DATA_WIDTH/64'h8)))));
        assign ram__read1_in = ((0) != '0);
        assign ram__write1_in = ((axi_in__wvalid_in && axi_in__wready_out) != '0);
        assign ram__write1_data_in = axi_in__wdata_in;
        assign ram__write1_mask_in = axi_in__wstrb_in;
        assign ram__debugen_in=debugen_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            read_addr_reg_tmp = axi_in__araddr_in;
            read_id_reg_tmp = axi_in__arid_in;
            read_valid_reg_tmp = 1;
        end
        if (((read_valid_reg) != '0) && axi_in__rready_in) begin
            read_valid_reg_tmp = 0;
        end
        if (axi_in__awvalid_in && axi_in__awready_out) begin
            write_addr_reg_tmp = axi_in__awaddr_in;
            write_id_reg_tmp = axi_in__awid_in;
            write_addr_valid_reg_tmp = 1;
        end
        if (axi_in__wvalid_in && axi_in__wready_out) begin
            write_addr_valid_reg_tmp = 0;
            write_resp_valid_reg_tmp = 1;
        end
        if (((write_resp_valid_reg) != '0) && axi_in__bready_in) begin
            write_resp_valid_reg_tmp = 0;
        end
        if (reset) begin
            read_addr_reg_tmp = '0;
            read_id_reg_tmp = '0;
            read_valid_reg_tmp = '0;
            write_addr_reg_tmp = '0;
            write_id_reg_tmp = '0;
            write_addr_valid_reg_tmp = '0;
            write_resp_valid_reg_tmp = '0;
        end
    end
    endtask

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin
        read_addr_reg_tmp = read_addr_reg;
        read_id_reg_tmp = read_id_reg;
        read_valid_reg_tmp = read_valid_reg;
        write_addr_reg_tmp = write_addr_reg;
        write_id_reg_tmp = write_id_reg;
        write_addr_valid_reg_tmp = write_addr_valid_reg;
        write_resp_valid_reg_tmp = write_resp_valid_reg;

            _work(reset);

        read_addr_reg <= read_addr_reg_tmp;
        read_id_reg <= read_id_reg_tmp;
        read_valid_reg <= read_valid_reg_tmp;
        write_addr_reg <= write_addr_reg_tmp;
        write_id_reg <= write_id_reg_tmp;
        write_addr_valid_reg <= write_addr_valid_reg_tmp;
        write_resp_valid_reg <= write_resp_valid_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end


endmodule

`default_nettype none

import Predef_pkg::*;


module Axi4Ram #(
    parameter ADDR_WIDTH
,   parameter ID_WIDTH
,   parameter DATA_WIDTH
,   parameter DEPTH
 )
 (
    input wire clk
,   input wire reset
,   input wire axi_in__awvalid_in
,   output wire axi_in__awready_out
,   input wire[ADDR_WIDTH-1:0] axi_in__awaddr_in
,   input wire[ID_WIDTH-1:0] axi_in__awid_in
,   input wire axi_in__wvalid_in
,   output wire axi_in__wready_out
,   input wire[DATA_WIDTH-1:0] axi_in__wdata_in
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
    parameter  FULL_MASK = ((DATA_WIDTH/'h8>='h40)) ? (~'h0) : (((('h1 <<< ((DATA_WIDTH/'h8)))) - 'h1));


    // regs and combs
    reg[ADDR_WIDTH-1:0] read_addr_reg;
    reg[ID_WIDTH-1:0] read_id_reg;
    reg read_valid_reg;
    reg[ADDR_WIDTH-1:0] write_addr_reg;
    reg[ID_WIDTH-1:0] write_id_reg;
    reg write_addr_valid_reg;
    reg write_resp_valid_reg;

    // members
      wire[$clog2((DEPTH))-1:0] ram__addr0_in;
      wire ram__write0_in;
      wire[(DATA_WIDTH/'h8)*'h8-1:0] ram__write0_data_in;
      wire[(DATA_WIDTH/'h8)-1:0] ram__write0_mask_in;
      wire ram__read0_in;
      wire[(DATA_WIDTH/'h8)*'h8-1:0] ram__read0_data_out;
      wire[$clog2((DEPTH))-1:0] ram__addr1_in;
      wire ram__write1_in;
      wire[(DATA_WIDTH/'h8)*'h8-1:0] ram__write1_data_in;
      wire[(DATA_WIDTH/'h8)-1:0] ram__write1_mask_in;
      wire ram__read1_in;
      wire[(DATA_WIDTH/'h8)*'h8-1:0] ram__read1_data_out;
      wire ram__debugen_in;
    Memory #(
        DATA_WIDTH/'h8
,       DEPTH
,       1
,       'h0
    ) ram (
        .clk(clk)
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
        assign axi_in__awready_out = !write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__wready_out = write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = !read_valid_reg;
        assign axi_in__rvalid_out = read_valid_reg;
        assign axi_in__rdata_out = ram__read0_data_out;
        assign axi_in__rlast_out = read_valid_reg;
        assign axi_in__rid_out = read_id_reg;
        assign ram__addr0_in = unsigned'($clog2(DEPTH)'(unsigned'($clog2(DEPTH)'(((read_valid_reg) ? (unsigned'(32'(read_addr_reg))/((DATA_WIDTH/'h8))) : (axi_in__araddr_in/((DATA_WIDTH/'h8))))))));
        assign ram__read0_in = axi_in__arvalid_in && axi_in__arready_out;
        assign ram__write0_in = 0;
        assign ram__write0_data_in = 'h0;
        assign ram__write0_mask_in = 'h0;
        assign ram__addr1_in = unsigned'($clog2(DEPTH)'(unsigned'($clog2(DEPTH)'((unsigned'(32'(write_addr_reg))/((DATA_WIDTH/'h8)))))));
        assign ram__read1_in = 0;
        assign ram__write1_in = axi_in__wvalid_in && axi_in__wready_out;
        assign ram__write1_data_in = axi_in__wdata_in;
        assign ram__write1_mask_in = FULL_MASK;
        assign ram__debugen_in=debugen_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            read_addr_reg_tmp = axi_in__araddr_in;
            read_id_reg_tmp = axi_in__arid_in;
            read_valid_reg_tmp = unsigned'(1'(1));
        end
        if (read_valid_reg && axi_in__rready_in) begin
            read_valid_reg_tmp = unsigned'(1'(0));
        end
        if (axi_in__awvalid_in && axi_in__awready_out) begin
            write_addr_reg_tmp = axi_in__awaddr_in;
            write_id_reg_tmp = axi_in__awid_in;
            write_addr_valid_reg_tmp = unsigned'(1'(1));
        end
        if (axi_in__wvalid_in && axi_in__wready_out) begin
            write_addr_valid_reg_tmp = unsigned'(1'(0));
            write_resp_valid_reg_tmp = unsigned'(1'(1));
        end
        if (write_resp_valid_reg && axi_in__bready_in) begin
            write_resp_valid_reg_tmp = unsigned'(1'(0));
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

    always @(posedge clk) begin
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


endmodule

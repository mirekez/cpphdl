`default_nettype none

import Predef_pkg::*;


module IOUART #(
    parameter ADDR_WIDTH
,   parameter ID_WIDTH
,   parameter DATA_WIDTH
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
,   output wire uart_valid_out
,   output wire[7:0] uart_data_out
);
    parameter  REG_TXDATA = 'h0;
    parameter  REG_STATUS = 'h4;


    // regs and combs
    reg[ADDR_WIDTH-1:0] read_addr_reg;
    reg[ID_WIDTH-1:0] read_id_reg;
    reg read_valid_reg;
    reg[ADDR_WIDTH-1:0] write_addr_reg;
    reg[ID_WIDTH-1:0] write_id_reg;
    reg write_addr_valid_reg;
    reg write_resp_valid_reg;
    reg uart_valid_reg;
    reg[8-1:0] uart_data_reg;
    logic[DATA_WIDTH-1:0] read_data_comb;
;

    // members

    // tmp variables
    logic[ADDR_WIDTH-1:0] read_addr_reg_tmp;
    logic[ID_WIDTH-1:0] read_id_reg_tmp;
    logic read_valid_reg_tmp;
    logic[ADDR_WIDTH-1:0] write_addr_reg_tmp;
    logic[ID_WIDTH-1:0] write_id_reg_tmp;
    logic write_addr_valid_reg_tmp;
    logic write_resp_valid_reg_tmp;
    logic uart_valid_reg_tmp;
    logic[8-1:0] uart_data_reg_tmp;


    always_comb begin : read_data_comb_func  // read_data_comb_func
        read_data_comb = 'h0;
        if (unsigned'(32'(read_addr_reg)) == REG_STATUS) begin
            read_data_comb['h0 +:32] = 'h1;
        end
        disable read_data_comb_func;
    end

    generate  // _assign
        assign axi_in__awready_out = !write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__wready_out = write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = !read_valid_reg;
        assign axi_in__rvalid_out = read_valid_reg;
        assign axi_in__rdata_out = read_data_comb;
        assign axi_in__rlast_out = read_valid_reg;
        assign axi_in__rid_out = read_id_reg;
    endgenerate

    task _work (input logic reset);
    begin: _work
        uart_valid_reg_tmp = 0;
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            read_addr_reg_tmp = axi_in__araddr_in;
            read_id_reg_tmp = axi_in__arid_in;
            read_valid_reg_tmp = 1;
        end
        if (read_valid_reg && axi_in__rready_in) begin
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
            if (unsigned'(32'(write_addr_reg)) == REG_TXDATA) begin
                uart_data_reg_tmp = unsigned'(8'(unsigned'(32'(axi_in__wdata_in['h0 +:8]))));
                uart_valid_reg_tmp = 1;
            end
        end
        if (write_resp_valid_reg && axi_in__bready_in) begin
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
            uart_valid_reg_tmp = '0;
            uart_data_reg_tmp = '0;
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
        uart_valid_reg_tmp = uart_valid_reg;
        uart_data_reg_tmp = uart_data_reg;

        _work(reset);

        read_addr_reg <= read_addr_reg_tmp;
        read_id_reg <= read_id_reg_tmp;
        read_valid_reg <= read_valid_reg_tmp;
        write_addr_reg <= write_addr_reg_tmp;
        write_id_reg <= write_id_reg_tmp;
        write_addr_valid_reg <= write_addr_valid_reg_tmp;
        write_resp_valid_reg <= write_resp_valid_reg_tmp;
        uart_valid_reg <= uart_valid_reg_tmp;
        uart_data_reg <= uart_data_reg_tmp;
    end

    assign uart_valid_out = uart_valid_reg;

    assign uart_data_out = uart_data_reg;


endmodule

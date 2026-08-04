`default_nettype none

import Predef_pkg::*;


module CLINT #(
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
,   input wire set_mtimecmp_in
,   input wire[31:0] set_mtimecmp_lo_in
,   input wire[31:0] set_mtimecmp_hi_in
,   output wire msip_out
,   output wire mtip_out
);
    parameter  REG_MSIP = 'h0;
    parameter  REG_MTIMECMP_LO = 'h4000;
    parameter  REG_MTIMECMP_HI = 'h4004;
    parameter  REG_MTIME_LO = 'hBFF8;
    parameter  REG_MTIME_HI = 'hBFFC;
    parameter  DATA_BYTES = DATA_WIDTH/'h8;


    // regs and combs
    reg[ADDR_WIDTH-1:0] read_addr_reg;
    reg[ID_WIDTH-1:0] read_id_reg;
    reg read_valid_reg;
    reg[ADDR_WIDTH-1:0] write_addr_reg;
    reg[ID_WIDTH-1:0] write_id_reg;
    reg write_addr_valid_reg;
    reg write_resp_valid_reg;
    reg[32-1:0] msip_reg;
    reg[64-1:0] mtime_reg;
    reg[64-1:0] mtimecmp_reg;
    reg[32-1:0] mtime_div_reg;
    logic[31:0] read_word_lane_comb;
;
    logic[31:0] read_word_comb;
;
    logic[DATA_WIDTH-1:0] read_data_comb;
;
    logic[31:0] write_word_comb;
;
    logic msip_comb;
;
    logic mtip_comb;
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
    logic[32-1:0] msip_reg_tmp;
    logic[64-1:0] mtime_reg_tmp;
    logic[64-1:0] mtimecmp_reg_tmp;
    logic[32-1:0] mtime_div_reg_tmp;


    always_comb begin : read_word_comb_func  // read_word_comb_func
        logic[31:0] addr;
        addr=unsigned'(32'(read_addr_reg));
        read_word_comb='h0;
        if (addr == REG_MSIP) begin
            read_word_comb=msip_reg;
        end
        else begin
            if (addr == REG_MTIMECMP_LO) begin
                read_word_comb=unsigned'(32'(mtimecmp_reg));
            end
            else begin
                if (addr == REG_MTIMECMP_HI) begin
                    read_word_comb=unsigned'(32'((unsigned'(64'(mtimecmp_reg)) >>> 'h20)));
                end
                else begin
                    if (addr == REG_MTIME_LO) begin
                        read_word_comb=unsigned'(32'(mtime_reg));
                    end
                    else begin
                        if (addr == REG_MTIME_HI) begin
                            read_word_comb=unsigned'(32'((unsigned'(64'(mtime_reg)) >>> 'h20)));
                        end
                    end
                end
            end
        end
    end

    always_comb begin : read_data_comb_func  // read_data_comb_func
        logic[63:0] lane;
        read_data_comb = 'h0;
        for (lane='h0;lane < (DATA_BYTES/'h4);lane=lane+1) begin
            read_data_comb[lane*'h20 +:32] = read_word_comb;
        end
    end

    always_comb begin : write_word_comb_func  // write_word_comb_func
        logic[63:0] lane;
        write_word_comb='h0;
        for (lane='h0;lane < (DATA_BYTES/'h4);lane=lane+1) begin
            write_word_comb|=unsigned'(32'(axi_in__wdata_in[lane*'h20 +:32]));
        end
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
        logic[31:0] addr;
        logic[31:0] word;
        if ('h1<='h1) begin
            mtime_reg_tmp = unsigned'(64'(unsigned'(64'(mtime_reg)) + 'h1));
        end
        else begin
        end
        if (set_mtimecmp_in) begin
            mtimecmp_reg_tmp = unsigned'(64'(((unsigned'(64'(set_mtimecmp_hi_in)) <<< 'h20)) | unsigned'(64'(set_mtimecmp_lo_in))));
        end
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
            addr=unsigned'(32'(write_addr_reg));
            word=write_word_comb;
            write_addr_valid_reg_tmp = unsigned'(1'(0));
            write_resp_valid_reg_tmp = unsigned'(1'(1));
            if (addr == REG_MSIP) begin
                msip_reg_tmp = unsigned'(32'(word & 'h1));
            end
            else begin
                if (addr == REG_MTIMECMP_LO) begin
                    mtimecmp_reg_tmp = unsigned'(64'(((unsigned'(64'(unsigned'(32'((unsigned'(64'(mtimecmp_reg)) >>> 'h20))))) <<< 'h20)) | unsigned'(64'(word))));
                end
                else begin
                    if (addr == REG_MTIMECMP_HI) begin
                        mtimecmp_reg_tmp = unsigned'(64'(((unsigned'(64'(word)) <<< 'h20)) | unsigned'(64'(unsigned'(32'(mtimecmp_reg))))));
                    end
                    else begin
                        if (addr == REG_MTIME_LO) begin
                            mtime_reg_tmp = unsigned'(64'(((unsigned'(64'(unsigned'(32'((unsigned'(64'(mtime_reg)) >>> 'h20))))) <<< 'h20)) | unsigned'(64'(word))));
                        end
                        else begin
                            if (addr == REG_MTIME_HI) begin
                                mtime_reg_tmp = unsigned'(64'(((unsigned'(64'(word)) <<< 'h20)) | unsigned'(64'(unsigned'(32'(mtime_reg))))));
                            end
                        end
                    end
                end
            end
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
            msip_reg_tmp = '0;
            mtime_reg_tmp = '0;
            mtime_div_reg_tmp = '0;
            mtimecmp_reg_tmp = unsigned'(64'(~'h0));
        end
    end
    endtask

    always_comb begin : msip_comb_func  // msip_comb_func
        msip_comb=((msip_reg & 'h1)) != 'h0;
    end

    always_comb begin : mtip_comb_func  // mtip_comb_func
        mtip_comb=mtime_reg>=mtimecmp_reg;
    end

    always @(posedge clk) begin
        read_addr_reg_tmp = read_addr_reg;
        read_id_reg_tmp = read_id_reg;
        read_valid_reg_tmp = read_valid_reg;
        write_addr_reg_tmp = write_addr_reg;
        write_id_reg_tmp = write_id_reg;
        write_addr_valid_reg_tmp = write_addr_valid_reg;
        write_resp_valid_reg_tmp = write_resp_valid_reg;
        msip_reg_tmp = msip_reg;
        mtime_reg_tmp = mtime_reg;
        mtimecmp_reg_tmp = mtimecmp_reg;
        mtime_div_reg_tmp = mtime_div_reg;

        _work(reset);

        read_addr_reg <= read_addr_reg_tmp;
        read_id_reg <= read_id_reg_tmp;
        read_valid_reg <= read_valid_reg_tmp;
        write_addr_reg <= write_addr_reg_tmp;
        write_id_reg <= write_id_reg_tmp;
        write_addr_valid_reg <= write_addr_valid_reg_tmp;
        write_resp_valid_reg <= write_resp_valid_reg_tmp;
        msip_reg <= msip_reg_tmp;
        mtime_reg <= mtime_reg_tmp;
        mtimecmp_reg <= mtimecmp_reg_tmp;
        mtime_div_reg <= mtime_div_reg_tmp;
    end

    assign msip_out = msip_comb;

    assign mtip_out = mtip_comb;


endmodule

`default_nettype none

import Predef_pkg::*;


module CLINT #(
    parameter ADDR_WIDTH = 'h20
,   parameter ID_WIDTH = 'h4
,   parameter DATA_WIDTH = 'h100
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
,   input wire set_mtimecmp_in
,   input wire[31:0] set_mtimecmp_lo_in
,   input wire[31:0] set_mtimecmp_hi_in
,   output wire msip_out
,   output wire mtip_out
,   output wire[31:0] debug_mtime_lo_out
,   output wire[31:0] debug_mtime_hi_out
,   output wire[31:0] debug_mtimecmp_lo_out
,   output wire[31:0] debug_mtimecmp_hi_out
);
    localparam  REG_MSIP = 'h0;
    localparam  REG_MTIMECMP_LO = 'h4000;
    localparam  REG_MTIME_LO = 'hBFF8;
    localparam  REG_MTIME_HI = 'hBFFC;
    localparam  DATA_BYTES = 32'(unsigned'(64'(DATA_WIDTH))/64'h8);
    localparam  HART_COUNT = 64'h1;
    localparam  REG_MTIMECMP_HI = 'h4004;


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
    reg[64-1:0] mtimecmp_reg[1];
    reg[32-1:0] mtime_div_reg;
    logic[31:0] read_word_lane_comb;
    logic[31:0] read_word_comb;
    logic[DATA_WIDTH-1:0] read_data_comb;
    logic[31:0] write_word_comb;
    logic msip_comb;
    logic mtip_comb;

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
    logic[64-1:0] mtimecmp_reg_tmp[1];
    logic[32-1:0] mtime_div_reg_tmp;


    always_comb begin : msip_comb_func  // msip_comb_func
        msip_comb=((msip_reg & 64'h1)) != 64'h0;
    end

    always_comb begin : mtip_comb_func  // mtip_comb_func
        mtip_comb=mtime_reg>=mtimecmp_reg['h0];
    end

    always_comb begin : read_word_lane_comb_func  // read_word_lane_comb_func
        read_word_lane_comb=((unsigned'(32'(read_addr_reg)) % unsigned'(32'(DATA_BYTES))))/'h4;
    end

    always_comb begin : read_word_comb_func  // read_word_comb_func
        logic[31:0] addr;
        logic[31:0] hart;
        addr=read_addr_reg;
        read_word_comb=32'h0;
        hart=32'h0;
        if (addr>=unsigned'(32'(REG_MSIP)) && (64'(addr) < (unsigned'(64'(REG_MSIP)) + (HART_COUNT*64'h4)))) begin
            hart=((addr - REG_MSIP))/'h4;
            read_word_comb=((unsigned'(32'(msip_reg)) >>> hart)) & 'h1;
        end
        else begin
            if (addr>=unsigned'(32'(REG_MTIMECMP_LO)) && (64'(addr) < (unsigned'(64'(REG_MTIMECMP_LO)) + (HART_COUNT*64'h8)))) begin
                hart=((addr - REG_MTIMECMP_LO))/'h8;
                if (((((addr - REG_MTIMECMP_LO)) & 'h4)) == 32'h0) begin
                    read_word_comb=mtimecmp_reg[hart];
                end
                else begin
                    read_word_comb=(unsigned'(64'(mtimecmp_reg[hart])) >>> 32'sh20);
                end
            end
            else begin
                if (addr == unsigned'(32'(REG_MTIME_LO))) begin
                    read_word_comb=mtime_reg;
                end
                else begin
                    if (addr == unsigned'(32'(REG_MTIME_HI))) begin
                        read_word_comb=(unsigned'(64'(mtime_reg)) >>> 32'sh20);
                    end
                end
            end
        end
    end

    always_comb begin : read_data_comb_func  // read_data_comb_func
        logic[63:0] lane;
        read_data_comb = 'h0;
        for (lane=64'h0;lane < 64'(32'((unsigned'(32'(DATA_BYTES))/32'h4)));lane=lane+1) begin
            read_data_comb[lane*64'h20 +:32] = unsigned'(64'(read_word_comb));
        end
    end

    always_comb begin : write_word_comb_func  // write_word_comb_func
        logic[63:0] lane;
        write_word_comb=32'h0;
        for (lane=64'h0;lane < 64'(32'((unsigned'(32'(DATA_BYTES))/32'h4)));lane=lane+1) begin
            write_word_comb|=unsigned'(32'(axi_in__wdata_in[lane*64'h20 +:32]));
        end
    end

    generate  // _assign
        assign axi_in__awready_out = ((!((write_addr_valid_reg) != '0) && !((write_resp_valid_reg) != '0)) != '0);
        assign axi_in__wready_out = ((((write_addr_valid_reg) != '0) && !((write_resp_valid_reg) != '0)) != '0);
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = ((!((read_valid_reg) != '0)) != '0);
        assign axi_in__rvalid_out = read_valid_reg;
        assign axi_in__rdata_out = read_data_comb;
        assign axi_in__rlast_out = read_valid_reg;
        assign axi_in__rid_out = read_id_reg;
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] addr;
        logic[31:0] hart;
        logic[31:0] word;
        logic[31:0] msip_next;
        if (32'sh1<=32'sh1) begin
            mtime_reg_tmp = 64'(unsigned'(64'(mtime_reg)) + 64'h1);
        end
        else begin
        end
        if (set_mtimecmp_in) begin
            mtimecmp_reg_tmp['h0] = 64'(((unsigned'(64'(set_mtimecmp_hi_in)) <<< 'h20)) | unsigned'(64'(set_mtimecmp_lo_in)));
        end
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
            addr=write_addr_reg;
            word=write_word_comb;
            write_addr_valid_reg_tmp = 0;
            write_resp_valid_reg_tmp = 1;
            if (addr>=unsigned'(32'(REG_MSIP)) && (64'(addr) < (unsigned'(64'(REG_MSIP)) + (HART_COUNT*64'h4)))) begin
                hart=((addr - REG_MSIP))/'h4;
                msip_next=msip_reg;
                if (((word & 'h1)) != 32'h0) begin
                    msip_next|='h1 <<< hart;
                end
                else begin
                    msip_next&=~('h1 <<< hart);
                end
                msip_reg_tmp = msip_next;
            end
            else begin
                if (addr>=unsigned'(32'(REG_MTIMECMP_LO)) && (64'(addr) < (unsigned'(64'(REG_MTIMECMP_LO)) + (HART_COUNT*64'h8)))) begin
                    hart=((addr - REG_MTIMECMP_LO))/'h8;
                    if (((((addr - REG_MTIMECMP_LO)) & 'h4)) == 32'h0) begin
                        mtimecmp_reg_tmp[hart] = 64'(((64'(unsigned'(32'((unsigned'(64'(mtimecmp_reg[hart])) >>> 32'sh20)))) <<< 'h20)) | 64'(word));
                    end
                    else begin
                        mtimecmp_reg_tmp[hart] = 64'(((64'(word) <<< 'h20)) | 64'(unsigned'(32'(mtimecmp_reg[hart]))));
                    end
                end
                else begin
                    if (addr == unsigned'(32'(REG_MTIME_LO))) begin
                        mtime_reg_tmp = 64'(((64'(unsigned'(32'((unsigned'(64'(mtime_reg)) >>> 32'sh20)))) <<< 'h20)) | 64'(word));
                    end
                    else begin
                        if (addr == unsigned'(32'(REG_MTIME_HI))) begin
                            mtime_reg_tmp = 64'(((64'(word) <<< 'h20)) | 64'(unsigned'(32'(mtime_reg))));
                        end
                    end
                end
            end
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
            msip_reg_tmp = '0;
            mtime_reg_tmp = '0;
            mtime_div_reg_tmp = '0;
            for (hart=32'h0;64'(hart) < unsigned'(64'(HART_COUNT));hart=hart+1) begin
                mtimecmp_reg_tmp[hart] = ~64'h0;
            end
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

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign msip_out = msip_comb;

    assign mtip_out = mtip_comb;

    assign debug_mtime_lo_out = unsigned'(32'(mtime_reg));

    assign debug_mtime_hi_out = unsigned'(32'((unsigned'(64'(mtime_reg)) >>> 32'sh20)));

    assign debug_mtimecmp_lo_out = unsigned'(32'(mtimecmp_reg['h0]));

    assign debug_mtimecmp_hi_out = unsigned'(32'((unsigned'(64'(mtimecmp_reg['h0])) >>> 32'sh20)));


endmodule

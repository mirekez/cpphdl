`default_nettype none

import Predef_pkg::*;


module Accelerator #(
    parameter ADDR_WIDTH
,   parameter ID_WIDTH
,   parameter DATA_WIDTH
,   parameter MEM_WORDS
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
,   output wire dma_out__awvalid_out
,   input wire dma_out__awready_in
,   output wire[ADDR_WIDTH-1:0] dma_out__awaddr_out
,   output wire[ID_WIDTH-1:0] dma_out__awid_out
,   output wire dma_out__wvalid_out
,   input wire dma_out__wready_in
,   output wire[DATA_WIDTH-1:0] dma_out__wdata_out
,   output wire dma_out__wlast_out
,   input wire dma_out__bvalid_in
,   output wire dma_out__bready_out
,   input wire[ID_WIDTH-1:0] dma_out__bid_in
,   output wire dma_out__arvalid_out
,   input wire dma_out__arready_in
,   output wire[ADDR_WIDTH-1:0] dma_out__araddr_out
,   output wire[ID_WIDTH-1:0] dma_out__arid_out
,   input wire dma_out__rvalid_in
,   output wire dma_out__rready_out
,   input wire[DATA_WIDTH-1:0] dma_out__rdata_in
,   input wire dma_out__rlast_in
,   input wire[ID_WIDTH-1:0] dma_out__rid_in
);
    parameter  REG_SRC_ADDR = 'h0;
    parameter  REG_DST_ADDR = 'h4;
    parameter  REG_LEN_WORDS = 'h8;
    parameter  REG_CONTROL = 'hC;
    parameter  REG_STATUS = 'h10;
    parameter  REG_PRBS_SEED = 'h14;
    parameter  REG_MEM_BASE = 'h1000;
    parameter  CTRL_START = 'h1 <<< 'h0;
    parameter  CTRL_DIR_A2M = 'h1 <<< 'h1;
    parameter  CTRL_PRBS = 'h1 <<< 'h2;
    parameter  DATA_BYTES = DATA_WIDTH/'h8;
    parameter  ST_IDLE = 'h0;
    parameter  ST_DMA_AR = 'h1;
    parameter  ST_DMA_R = 'h2;
    parameter  ST_DMA_AW = 'h3;
    parameter  ST_DMA_W = 'h4;
    parameter  ST_DMA_B = 'h5;
    parameter  ST_DONE = 'h6;


    // regs and combs
    reg[ADDR_WIDTH-1:0] read_addr_reg;
    reg[ID_WIDTH-1:0] read_id_reg;
    reg read_valid_reg;
    reg[DATA_WIDTH-1:0] read_data_reg;
    reg[ADDR_WIDTH-1:0] write_addr_reg;
    reg[ID_WIDTH-1:0] write_id_reg;
    reg write_addr_valid_reg;
    reg write_resp_valid_reg;
    reg[32-1:0] src_addr_reg;
    reg[32-1:0] dst_addr_reg;
    reg[32-1:0] len_words_reg;
    reg[32-1:0] index_reg;
    reg[32-1:0] prbs_reg;
    reg busy_reg;
    reg done_reg;
    reg error_reg;
    reg dma_write_reg;
    reg[3-1:0] state_reg;
    reg[MEM_WORDS-1:0][32-1:0] accel_mem_reg;
    logic[31:0] read_word_comb;
;
    logic[DATA_WIDTH-1:0] read_data_comb;
;
    logic[31:0] write_word_comb;
;
    logic[31:0] dma_addr_comb;
;
    logic[31:0] dma_wdata_comb;
;
    logic[DATA_WIDTH-1:0] dma_wbeat_comb;
;
    logic[31:0] dma_rword_comb;
;
    logic[31:0] prbs_next_comb;
;

    // members

    // tmp variables
    logic[ADDR_WIDTH-1:0] read_addr_reg_tmp;
    logic[ID_WIDTH-1:0] read_id_reg_tmp;
    logic read_valid_reg_tmp;
    logic[DATA_WIDTH-1:0] read_data_reg_tmp;
    logic[ADDR_WIDTH-1:0] write_addr_reg_tmp;
    logic[ID_WIDTH-1:0] write_id_reg_tmp;
    logic write_addr_valid_reg_tmp;
    logic write_resp_valid_reg_tmp;
    logic[32-1:0] src_addr_reg_tmp;
    logic[32-1:0] dst_addr_reg_tmp;
    logic[32-1:0] len_words_reg_tmp;
    logic[32-1:0] index_reg_tmp;
    logic[32-1:0] prbs_reg_tmp;
    logic busy_reg_tmp;
    logic done_reg_tmp;
    logic error_reg_tmp;
    logic dma_write_reg_tmp;
    logic[3-1:0] state_reg_tmp;
    logic[MEM_WORDS-1:0][32-1:0] accel_mem_reg_tmp;


    always_comb begin : write_word_comb_func  // write_word_comb_func
        logic[31:0] lane;
        lane=((unsigned'(32'(write_addr_reg)) % DATA_BYTES))/'h4;
        write_word_comb=unsigned'(32'(axi_in__wdata_in[lane*'h20 +:32]));
    end

    always_comb begin : dma_addr_comb_func  // dma_addr_comb_func
        if (dma_write_reg) begin
            dma_addr_comb=dst_addr_reg + (unsigned'(32'(index_reg))*'h4);
        end
        else begin
            dma_addr_comb=src_addr_reg + (unsigned'(32'(index_reg))*'h4);
        end
    end

    always_comb begin : dma_wdata_comb_func  // dma_wdata_comb_func
        logic[31:0] mem_index;
        mem_index=unsigned'(32'(src_addr_reg)) + unsigned'(32'(index_reg));
        if (mem_index < MEM_WORDS) begin
            dma_wdata_comb=accel_mem_reg[mem_index];
        end
        else begin
            dma_wdata_comb='h0;
        end
    end

    always_comb begin : dma_wbeat_comb_func  // dma_wbeat_comb_func
        logic[31:0] lane;
        dma_wbeat_comb = 'h0;
        lane=((dma_addr_comb % DATA_BYTES))/'h4;
        dma_wbeat_comb[lane*'h20 +:32] = dma_wdata_comb;
    end

    always_comb begin : dma_rword_comb_func  // dma_rword_comb_func
        logic[31:0] lane;
        lane=((dma_addr_comb % DATA_BYTES))/'h4;
        dma_rword_comb=unsigned'(32'(dma_out__rdata_in[lane*'h20 +:32]));
    end

    always_comb begin : prbs_next_comb_func  // prbs_next_comb_func
        logic[31:0] x;
        x=prbs_reg;
        x^=x <<< 'hD;
        x^=x >>> 'h11;
        x^=x <<< 'h5;
        prbs_next_comb=x;
    end

    generate  // _assign
        assign axi_in__awready_out = !write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__wready_out = write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = !read_valid_reg;
        assign axi_in__rvalid_out = read_valid_reg;
        assign axi_in__rdata_out = read_data_reg;
        assign axi_in__rlast_out = read_valid_reg;
        assign axi_in__rid_out = read_id_reg;
        assign dma_out__awvalid_out = state_reg == ST_DMA_AW;
        assign dma_out__awaddr_out = unsigned'(ADDR_WIDTH'(unsigned'(ADDR_WIDTH'(dma_addr_comb))));
        assign dma_out__awid_out = unsigned'(ID_WIDTH'(unsigned'(ID_WIDTH'('h0))));
        assign dma_out__wvalid_out = state_reg == ST_DMA_W;
        assign dma_out__wdata_out = dma_wbeat_comb;
        assign dma_out__wlast_out = state_reg == ST_DMA_W;
        assign dma_out__bready_out = (state_reg == ST_DMA_B) && dma_out__bvalid_in;
        assign dma_out__arvalid_out = state_reg == ST_DMA_AR;
        assign dma_out__araddr_out = unsigned'(ADDR_WIDTH'(unsigned'(ADDR_WIDTH'(dma_addr_comb))));
        assign dma_out__arid_out = unsigned'(ID_WIDTH'(unsigned'(ID_WIDTH'('h0))));
        assign dma_out__rready_out = (state_reg == ST_DMA_R) && dma_out__rvalid_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] addr;
        logic[31:0] beat_base;
        logic[31:0] word;
        logic[31:0] mem_index;
        logic[64-1:0] read_data;
        logic[63:0] i;
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            addr=unsigned'(32'(axi_in__araddr_in));
            beat_base=addr & ~((DATA_BYTES - 'h1));
            read_data = 'h0;
            for (i='h0;i < (DATA_BYTES/'h4);i=i+1) begin
                addr=beat_base + (i*'h4);
                if (addr>='h100 && (addr < 'h120)) begin
                    addr-='h100;
                end
                word='h0;
                if (addr == REG_SRC_ADDR) begin
                    word=src_addr_reg;
                end
                else begin
                    if (addr == REG_DST_ADDR) begin
                        word=dst_addr_reg;
                    end
                    else begin
                        if (addr == REG_LEN_WORDS) begin
                            word=len_words_reg;
                        end
                        else begin
                            if (addr == REG_CONTROL) begin
                                word=(dma_write_reg) ? (CTRL_DIR_A2M) : ('h0);
                            end
                            else begin
                                if (addr == REG_STATUS) begin
                                    word=(((busy_reg) ? ('h1) : ('h0)) | ((done_reg) ? ('h2) : ('h0))) | ((error_reg) ? ('h4) : ('h0));
                                end
                                else begin
                                    if (addr == REG_PRBS_SEED) begin
                                        word=prbs_reg;
                                    end
                                    else begin
                                        if (addr>=REG_MEM_BASE && (addr < (REG_MEM_BASE + (MEM_WORDS*'h4)))) begin
                                            mem_index=((addr - REG_MEM_BASE))/'h4;
                                            word=accel_mem_reg[mem_index];
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
                read_data[i*'h20 +:32] = word;
            end
            read_addr_reg_tmp = axi_in__araddr_in;
            read_id_reg_tmp = axi_in__arid_in;
            read_data_reg_tmp = read_data;
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
            if (addr>='h100 && (addr < 'h120)) begin
                addr-='h100;
            end
            word=write_word_comb;
            write_addr_valid_reg_tmp = unsigned'(1'(0));
            write_resp_valid_reg_tmp = unsigned'(1'(1));
            if (addr == REG_SRC_ADDR) begin
                src_addr_reg_tmp = unsigned'(32'(word));
            end
            else begin
                if (addr == REG_DST_ADDR) begin
                    dst_addr_reg_tmp = unsigned'(32'(word));
                end
                else begin
                    if (addr == REG_LEN_WORDS) begin
                        len_words_reg_tmp = unsigned'(32'(word));
                    end
                    else begin
                        if (addr == REG_PRBS_SEED) begin
                            prbs_reg_tmp = unsigned'(32'((word) ? (word) : ('h1)));
                        end
                        else begin
                            if (((addr == REG_CONTROL) && ((word & CTRL_START))) && !busy_reg) begin
                                done_reg_tmp = unsigned'(1'(0));
                                error_reg_tmp = unsigned'(1'(0));
                                index_reg_tmp = unsigned'(32'('h0));
                                if (word & CTRL_PRBS) begin
                                    for (i='h0;i < MEM_WORDS;i=i+1) begin
                                        accel_mem_reg_tmp[i] = unsigned'(32'(prbs_next_comb + i));
                                    end
                                    prbs_reg_tmp = unsigned'(32'(prbs_next_comb));
                                    done_reg_tmp = unsigned'(1'(1));
                                end
                                else begin
                                    if ((((len_words_reg == 'h0) || (len_words_reg > MEM_WORDS)) || ((((((word & CTRL_DIR_A2M)) != 'h0)) && ((src_addr_reg + len_words_reg) > MEM_WORDS)))) || ((((((word & CTRL_DIR_A2M)) == 'h0)) && ((dst_addr_reg + len_words_reg) > MEM_WORDS)))) begin
                                        error_reg_tmp = unsigned'(1'(1));
                                        done_reg_tmp = unsigned'(1'(1));
                                    end
                                    else begin
                                        dma_write_reg_tmp = unsigned'(1'(((word & CTRL_DIR_A2M)) != 'h0));
                                        busy_reg_tmp = unsigned'(1'(1));
                                        state_reg_tmp = ((((word & CTRL_DIR_A2M)) != 'h0)) ? (ST_DMA_AW) : (ST_DMA_AR);
                                    end
                                end
                            end
                            else begin
                                if (addr>=REG_MEM_BASE && (addr < (REG_MEM_BASE + (MEM_WORDS*'h4)))) begin
                                    mem_index=((addr - REG_MEM_BASE))/'h4;
                                    accel_mem_reg_tmp[mem_index] = unsigned'(32'(word));
                                end
                            end
                        end
                    end
                end
            end
        end
        if (write_resp_valid_reg && axi_in__bready_in) begin
            write_resp_valid_reg_tmp = unsigned'(1'(0));
        end
        if (state_reg == ST_DMA_AR) begin
            if (dma_out__arvalid_out && dma_out__arready_in) begin
                state_reg_tmp = ST_DMA_R;
            end
        end
        else begin
            if (state_reg == ST_DMA_R) begin
                if (dma_out__rvalid_in && dma_out__rready_out) begin
                    accel_mem_reg_tmp[dst_addr_reg + index_reg] = unsigned'(32'(dma_rword_comb));
                    if (index_reg + 'h1>=len_words_reg) begin
                        state_reg_tmp = ST_DONE;
                    end
                    else begin
                        index_reg_tmp = unsigned'(32'(index_reg + 'h1));
                        state_reg_tmp = ST_DMA_AR;
                    end
                end
            end
            else begin
                if (state_reg == ST_DMA_AW) begin
                    if (dma_out__awvalid_out && dma_out__awready_in) begin
                        state_reg_tmp = ST_DMA_W;
                    end
                end
                else begin
                    if (state_reg == ST_DMA_W) begin
                        if (dma_out__wvalid_out && dma_out__wready_in) begin
                            state_reg_tmp = ST_DMA_B;
                        end
                    end
                    else begin
                        if (state_reg == ST_DMA_B) begin
                            if (dma_out__bvalid_in && dma_out__bready_out) begin
                                if (index_reg + 'h1>=len_words_reg) begin
                                    state_reg_tmp = ST_DONE;
                                end
                                else begin
                                    index_reg_tmp = unsigned'(32'(index_reg + 'h1));
                                    state_reg_tmp = ST_DMA_AW;
                                end
                            end
                        end
                        else begin
                            if (state_reg == ST_DONE) begin
                                busy_reg_tmp = unsigned'(1'(0));
                                done_reg_tmp = unsigned'(1'(1));
                                state_reg_tmp = ST_IDLE;
                            end
                        end
                    end
                end
            end
        end
        if (reset) begin
            read_addr_reg_tmp = '0;
            read_id_reg_tmp = '0;
            read_valid_reg_tmp = '0;
            read_data_reg_tmp = '0;
            write_addr_reg_tmp = '0;
            write_id_reg_tmp = '0;
            write_addr_valid_reg_tmp = '0;
            write_resp_valid_reg_tmp = '0;
            src_addr_reg_tmp = '0;
            dst_addr_reg_tmp = '0;
            len_words_reg_tmp = '0;
            index_reg_tmp = '0;
            prbs_reg_tmp = unsigned'(32'('h1));
            busy_reg_tmp = '0;
            done_reg_tmp = '0;
            error_reg_tmp = '0;
            dma_write_reg_tmp = '0;
            state_reg_tmp = '0;
            for (i='h0;i < MEM_WORDS;i=i+1) begin
                accel_mem_reg_tmp[i] = unsigned'(32'('h0));
            end
        end
    end
    endtask

    always @(posedge clk) begin
        read_addr_reg_tmp = read_addr_reg;
        read_id_reg_tmp = read_id_reg;
        read_valid_reg_tmp = read_valid_reg;
        read_data_reg_tmp = read_data_reg;
        write_addr_reg_tmp = write_addr_reg;
        write_id_reg_tmp = write_id_reg;
        write_addr_valid_reg_tmp = write_addr_valid_reg;
        write_resp_valid_reg_tmp = write_resp_valid_reg;
        src_addr_reg_tmp = src_addr_reg;
        dst_addr_reg_tmp = dst_addr_reg;
        len_words_reg_tmp = len_words_reg;
        index_reg_tmp = index_reg;
        prbs_reg_tmp = prbs_reg;
        busy_reg_tmp = busy_reg;
        done_reg_tmp = done_reg;
        error_reg_tmp = error_reg;
        dma_write_reg_tmp = dma_write_reg;
        state_reg_tmp = state_reg;
        accel_mem_reg_tmp = accel_mem_reg;

        _work(reset);

        read_addr_reg <= read_addr_reg_tmp;
        read_id_reg <= read_id_reg_tmp;
        read_valid_reg <= read_valid_reg_tmp;
        read_data_reg <= read_data_reg_tmp;
        write_addr_reg <= write_addr_reg_tmp;
        write_id_reg <= write_id_reg_tmp;
        write_addr_valid_reg <= write_addr_valid_reg_tmp;
        write_resp_valid_reg <= write_resp_valid_reg_tmp;
        src_addr_reg <= src_addr_reg_tmp;
        dst_addr_reg <= dst_addr_reg_tmp;
        len_words_reg <= len_words_reg_tmp;
        index_reg <= index_reg_tmp;
        prbs_reg <= prbs_reg_tmp;
        busy_reg <= busy_reg_tmp;
        done_reg <= done_reg_tmp;
        error_reg <= error_reg_tmp;
        dma_write_reg <= dma_write_reg_tmp;
        state_reg <= state_reg_tmp;
        accel_mem_reg <= accel_mem_reg_tmp;
    end


endmodule

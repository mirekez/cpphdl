`default_nettype none

import Predef_pkg::*;


module SDController #(
    parameter ADDR_WIDTH
,   parameter ID_WIDTH
,   parameter DATA_WIDTH
,   parameter FIFO_DEPTH
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
,   output wire sd_cmd_valid_out
,   output wire[8-1:0] sd_cmd_data_out
,   output wire sd_cmd_last_out
,   input wire sd_cmd_ready_in
,   input wire sd_rsp_valid_in
,   input wire[8-1:0] sd_rsp_data_in
,   input wire sd_rsp_last_in
,   output wire sd_rsp_ready_out
,   output wire irq_out
,   output wire dma_write_complete_out
,   output wire[31:0] debug_status_out
,   output wire[31:0] debug_state_out
,   output wire[31:0] debug_count_out
,   output wire[31:0] debug_len_out
);
    parameter  DATA_BYTES = DATA_WIDTH/'h8;
    parameter  HEADER_BYTES = 'h7;
    parameter  C_REG_TXDATA = 'h18;
    parameter  C_REG_DMA_DESC_ADDR = 'h28;
    parameter  C_REG_DMA_DESC_LEN = 'h2C;
    parameter  C_REG_DMA_DESC_PUSH = 'h30;
    parameter  C_REG_DMA_DESC_STATUS = 'h34;
    parameter  C_STATUS_DESC_READY = 'h1 <<< 'h6;
    parameter  C_DESC_STATUS_READY = 'h1 <<< 'h0;
    parameter  C_DESC_STATUS_EMPTY = 'h1 <<< 'h1;
    parameter  C_DESC_STATUS_FULL = 'h1 <<< 'h2;
    parameter  C_DESC_STATUS_COUNT_SHIFT = 'h8;
    parameter  ST_IDLE = 'h0;
    parameter  ST_HEADER = 'h1;
    parameter  ST_PIO_READ = 'h2;
    parameter  ST_PIO_WRITE = 'h3;
    parameter  ST_WAIT_ACK = 'h4;
    parameter  ST_DMA_READ_RECV = 'h5;
    parameter  ST_DMA_READ_AW = 'h6;
    parameter  ST_DMA_READ_W = 'h7;
    parameter  ST_DMA_READ_B = 'h8;
    parameter  ST_DMA_WRITE_AR = 'h9;
    parameter  ST_DMA_WRITE_R = 'hA;
    parameter  ST_DMA_WRITE_SEND = 'hB;
    parameter  ST_DONE = 'hC;
    parameter  ST_ERROR = 'hD;
    parameter  ST_DMA_LOAD_DESC = 'hE;
    parameter  FIFO_INDEX_BITS = $clog2(FIFO_DEPTH);
    parameter  FIFO_COUNT_BITS = $clog2(FIFO_DEPTH + 'h1);


    // regs and combs
    reg[FIFO_DEPTH-1:0][8-1:0] tx_fifo_data_reg;
    reg[FIFO_INDEX_BITS-1:0] tx_fifo_rd_reg;
    reg[FIFO_INDEX_BITS-1:0] tx_fifo_wr_reg;
    reg[FIFO_COUNT_BITS-1:0] tx_fifo_count_reg;
    reg[FIFO_DEPTH-1:0][8-1:0] rx_fifo_data_reg;
    reg[FIFO_INDEX_BITS-1:0] rx_fifo_rd_reg;
    reg[FIFO_INDEX_BITS-1:0] rx_fifo_wr_reg;
    reg[FIFO_COUNT_BITS-1:0] rx_fifo_count_reg;
    reg tx_valid_reg;
    reg[8-1:0] tx_data_reg;
    reg tx_last_reg;
    reg rx_valid_reg;
    reg[8-1:0] rx_data_reg;
    reg rx_last_reg;
    reg[ADDR_WIDTH-1:0] read_addr_reg;
    reg[ID_WIDTH-1:0] read_id_reg;
    reg read_valid_reg;
    reg[DATA_WIDTH-1:0] read_data_reg;
    reg[ADDR_WIDTH-1:0] write_addr_reg;
    reg[ID_WIDTH-1:0] write_id_reg;
    reg write_addr_valid_reg;
    reg write_resp_valid_reg;
    reg[8-1:0] cmd_reg;
    reg[32-1:0] arg_reg;
    reg[32-1:0] len_reg;
    reg[32-1:0] dma_addr_reg;
    reg[32-1:0] dma_desc_addr_reg;
    reg[32-1:0] dma_desc_len_reg;
    reg[32-1:0] count_reg;
    reg[5-1:0] state_reg;
    reg[8-1:0] header_index_reg;
    reg write_mode_reg;
    reg dma_mode_reg;
    reg busy_reg;
    reg done_reg;
    reg error_reg;
    reg irq_enable_reg;
    reg irq_pending_reg;
    reg dma_write_complete_reg;
    reg[DATA_WIDTH-1:0] dma_beat_reg;
    reg[32-1:0] dma_beat_base_reg;
    reg[32-1:0] dma_byte_index_reg;
    reg[32-1:0] dma_beat_limit_reg;
    reg[FIFO_DEPTH-1:0][32-1:0] desc_addr_data_reg;
    reg[FIFO_DEPTH-1:0][32-1:0] desc_len_data_reg;
    reg[FIFO_INDEX_BITS-1:0] desc_rd_reg;
    reg[FIFO_INDEX_BITS-1:0] desc_wr_reg;
    reg[FIFO_COUNT_BITS-1:0] desc_count_reg;
    reg[32-1:0] active_desc_addr_reg;
    reg[32-1:0] active_desc_remaining_reg;
    reg active_desc_valid_reg;
    logic tx_fifo_full_comb;
;
    logic tx_fifo_valid_comb;
;
    logic[8-1:0] tx_fifo_data_comb;
;
    logic[FIFO_INDEX_BITS-1:0] tx_fifo_rd_next_comb;
;
    logic[FIFO_INDEX_BITS-1:0] tx_fifo_wr_next_comb;
;
    logic rx_fifo_full_comb;
;
    logic rx_fifo_valid_comb;
;
    logic[8-1:0] rx_fifo_data_comb;
;
    logic[FIFO_INDEX_BITS-1:0] rx_fifo_rd_next_comb;
;
    logic[FIFO_INDEX_BITS-1:0] rx_fifo_wr_next_comb;
;
    logic desc_fifo_full_comb;
;
    logic desc_fifo_valid_comb;
;
    logic[FIFO_INDEX_BITS-1:0] desc_fifo_rd_next_comb;
;
    logic[FIFO_INDEX_BITS-1:0] desc_fifo_wr_next_comb;
;
    logic[31:0] dma_remaining_comb;
;
    logic[31:0] dma_next_beat_limit_comb;
;
    logic[31:0] write_word_comb;
;
    logic[8-1:0] header_byte_comb;
;
    logic[8-1:0] tx_byte_comb;
;
    logic phy_tx_valid_comb;
;
    logic[8-1:0] phy_tx_data_comb;
;
    logic phy_tx_last_comb;
;
    logic phy_rx_ready_comb;
;
    logic[DATA_WIDTH-1:0] read_data_comb;
;
    logic[31:0] debug_status_comb;
;
    logic[ADDR_WIDTH-1:0] dma_addr_comb;
;

    // members

    // tmp variables
    logic[FIFO_INDEX_BITS-1:0] tx_fifo_rd_reg_tmp;
    logic[FIFO_INDEX_BITS-1:0] tx_fifo_wr_reg_tmp;
    logic[FIFO_COUNT_BITS-1:0] tx_fifo_count_reg_tmp;
    logic[FIFO_INDEX_BITS-1:0] rx_fifo_rd_reg_tmp;
    logic[FIFO_INDEX_BITS-1:0] rx_fifo_wr_reg_tmp;
    logic[FIFO_COUNT_BITS-1:0] rx_fifo_count_reg_tmp;
    logic tx_valid_reg_tmp;
    logic[8-1:0] tx_data_reg_tmp;
    logic tx_last_reg_tmp;
    logic rx_valid_reg_tmp;
    logic[8-1:0] rx_data_reg_tmp;
    logic rx_last_reg_tmp;
    logic[ADDR_WIDTH-1:0] read_addr_reg_tmp;
    logic[ID_WIDTH-1:0] read_id_reg_tmp;
    logic read_valid_reg_tmp;
    logic[DATA_WIDTH-1:0] read_data_reg_tmp;
    logic[ADDR_WIDTH-1:0] write_addr_reg_tmp;
    logic[ID_WIDTH-1:0] write_id_reg_tmp;
    logic write_addr_valid_reg_tmp;
    logic write_resp_valid_reg_tmp;
    logic[8-1:0] cmd_reg_tmp;
    logic[32-1:0] arg_reg_tmp;
    logic[32-1:0] len_reg_tmp;
    logic[32-1:0] dma_addr_reg_tmp;
    logic[32-1:0] dma_desc_addr_reg_tmp;
    logic[32-1:0] dma_desc_len_reg_tmp;
    logic[32-1:0] count_reg_tmp;
    logic[5-1:0] state_reg_tmp;
    logic[8-1:0] header_index_reg_tmp;
    logic write_mode_reg_tmp;
    logic dma_mode_reg_tmp;
    logic busy_reg_tmp;
    logic done_reg_tmp;
    logic error_reg_tmp;
    logic irq_enable_reg_tmp;
    logic irq_pending_reg_tmp;
    logic dma_write_complete_reg_tmp;
    logic[DATA_WIDTH-1:0] dma_beat_reg_tmp;
    logic[32-1:0] dma_beat_base_reg_tmp;
    logic[32-1:0] dma_byte_index_reg_tmp;
    logic[32-1:0] dma_beat_limit_reg_tmp;
    logic[FIFO_DEPTH-1:0][32-1:0] desc_addr_data_reg_tmp;
    logic[FIFO_DEPTH-1:0][32-1:0] desc_len_data_reg_tmp;
    logic[FIFO_INDEX_BITS-1:0] desc_rd_reg_tmp;
    logic[FIFO_INDEX_BITS-1:0] desc_wr_reg_tmp;
    logic[FIFO_COUNT_BITS-1:0] desc_count_reg_tmp;
    logic[32-1:0] active_desc_addr_reg_tmp;
    logic[32-1:0] active_desc_remaining_reg_tmp;
    logic active_desc_valid_reg_tmp;


    always_comb begin : tx_fifo_full_comb_func  // tx_fifo_full_comb_func
        tx_fifo_full_comb=tx_fifo_count_reg == FIFO_DEPTH;
    end

    always_comb begin : tx_fifo_valid_comb_func  // tx_fifo_valid_comb_func
        tx_fifo_valid_comb=tx_fifo_count_reg != 'h0;
    end

    always_comb begin : tx_fifo_data_comb_func  // tx_fifo_data_comb_func
        tx_fifo_data_comb = tx_fifo_data_reg[unsigned'(32'(tx_fifo_rd_reg))];
    end

    always_comb begin : tx_fifo_rd_next_comb_func  // tx_fifo_rd_next_comb_func
        if (unsigned'(32'(tx_fifo_rd_reg)) + 'h1>=FIFO_DEPTH) begin
            tx_fifo_rd_next_comb = 'h0;
            disable tx_fifo_rd_next_comb_func;
        end
        tx_fifo_rd_next_comb = tx_fifo_rd_reg + 'h1;
    end

    always_comb begin : tx_fifo_wr_next_comb_func  // tx_fifo_wr_next_comb_func
        if (unsigned'(32'(tx_fifo_wr_reg)) + 'h1>=FIFO_DEPTH) begin
            tx_fifo_wr_next_comb = 'h0;
            disable tx_fifo_wr_next_comb_func;
        end
        tx_fifo_wr_next_comb = tx_fifo_wr_reg + 'h1;
    end

    always_comb begin : rx_fifo_full_comb_func  // rx_fifo_full_comb_func
        rx_fifo_full_comb=rx_fifo_count_reg == FIFO_DEPTH;
    end

    always_comb begin : rx_fifo_valid_comb_func  // rx_fifo_valid_comb_func
        rx_fifo_valid_comb=rx_fifo_count_reg != 'h0;
    end

    always_comb begin : rx_fifo_data_comb_func  // rx_fifo_data_comb_func
        rx_fifo_data_comb = rx_fifo_data_reg[unsigned'(32'(rx_fifo_rd_reg))];
    end

    always_comb begin : rx_fifo_rd_next_comb_func  // rx_fifo_rd_next_comb_func
        if (unsigned'(32'(rx_fifo_rd_reg)) + 'h1>=FIFO_DEPTH) begin
            rx_fifo_rd_next_comb = 'h0;
            disable rx_fifo_rd_next_comb_func;
        end
        rx_fifo_rd_next_comb = rx_fifo_rd_reg + 'h1;
    end

    always_comb begin : rx_fifo_wr_next_comb_func  // rx_fifo_wr_next_comb_func
        if (unsigned'(32'(rx_fifo_wr_reg)) + 'h1>=FIFO_DEPTH) begin
            rx_fifo_wr_next_comb = 'h0;
            disable rx_fifo_wr_next_comb_func;
        end
        rx_fifo_wr_next_comb = rx_fifo_wr_reg + 'h1;
    end

    always_comb begin : desc_fifo_full_comb_func  // desc_fifo_full_comb_func
        desc_fifo_full_comb=desc_count_reg == FIFO_DEPTH;
    end

    always_comb begin : desc_fifo_valid_comb_func  // desc_fifo_valid_comb_func
        desc_fifo_valid_comb=desc_count_reg != 'h0;
    end

    always_comb begin : desc_fifo_rd_next_comb_func  // desc_fifo_rd_next_comb_func
        if (unsigned'(32'(desc_rd_reg)) + 'h1>=FIFO_DEPTH) begin
            desc_fifo_rd_next_comb = 'h0;
            disable desc_fifo_rd_next_comb_func;
        end
        desc_fifo_rd_next_comb = desc_rd_reg + 'h1;
    end

    always_comb begin : desc_fifo_wr_next_comb_func  // desc_fifo_wr_next_comb_func
        if (unsigned'(32'(desc_wr_reg)) + 'h1>=FIFO_DEPTH) begin
            desc_fifo_wr_next_comb = 'h0;
            disable desc_fifo_wr_next_comb_func;
        end
        desc_fifo_wr_next_comb = desc_wr_reg + 'h1;
    end

    always_comb begin : dma_remaining_comb_func  // dma_remaining_comb_func
        logic[31:0] total_left;
        logic[31:0] desc_left;
        total_left=unsigned'(32'(len_reg)) - unsigned'(32'(count_reg));
        desc_left=(active_desc_valid_reg) ? (unsigned'(32'(active_desc_remaining_reg))) : (total_left);
        dma_remaining_comb=(desc_left < total_left) ? (desc_left) : (total_left);
    end

    always_comb begin : dma_next_beat_limit_comb_func  // dma_next_beat_limit_comb_func
        logic[31:0] limit;
        limit=dma_remaining_comb;
        if (limit > DATA_BYTES) begin
            limit=DATA_BYTES;
        end
        dma_next_beat_limit_comb=limit;
    end

    always_comb begin : write_word_comb_func  // write_word_comb_func
        logic[63:0] lane;
        write_word_comb='h0;
        for (lane='h0;lane < (DATA_BYTES/'h4);lane=lane+1) begin
            write_word_comb|=unsigned'(32'(axi_in__wdata_in[lane*'h20 +:32]));
        end
    end

    always_comb begin : header_byte_comb_func  // header_byte_comb_func
        logic[31:0] idx;
        idx=header_index_reg;
        if (idx == 'h0) begin
            header_byte_comb = cmd_reg;
            disable header_byte_comb_func;
        end
        if (idx>='h1 && idx<='h4) begin
            header_byte_comb = unsigned'(8'((((unsigned'(32'(arg_reg)) >>> ((((idx - 'h1))*'h8)))) & 'hFF)));
            disable header_byte_comb_func;
        end
        if (idx == 'h5) begin
            header_byte_comb = unsigned'(8'((unsigned'(32'(len_reg)) & 'hFF)));
            disable header_byte_comb_func;
        end
        header_byte_comb = unsigned'(8'((((unsigned'(32'(len_reg)) >>> 'h8)) & 'hFF)));
    end

    always_comb begin : tx_byte_comb_func  // tx_byte_comb_func
        logic[63:0] i;
        logic[31:0] idx;
        tx_byte_comb = 'h0;
        idx=unsigned'(32'(dma_byte_index_reg)) % DATA_BYTES;
        for (i='h0;i < DATA_BYTES;i=i+1) begin
            if (idx == i) begin
                tx_byte_comb = unsigned'(8'((unsigned'(32'(dma_beat_reg[i*'h8 +:8])))));
            end
        end
    end

    always_comb begin : phy_tx_valid_comb_func  // phy_tx_valid_comb_func
        if (state_reg == ST_HEADER) begin
            phy_tx_valid_comb=1;
            disable phy_tx_valid_comb_func;
        end
        if (state_reg == ST_PIO_WRITE) begin
            phy_tx_valid_comb=tx_fifo_valid_comb;
            disable phy_tx_valid_comb_func;
        end
        if (state_reg == ST_DMA_WRITE_SEND) begin
            phy_tx_valid_comb=count_reg < len_reg;
            disable phy_tx_valid_comb_func;
        end
        phy_tx_valid_comb=0;
    end

    always_comb begin : phy_tx_data_comb_func  // phy_tx_data_comb_func
        if (state_reg == ST_HEADER) begin
            phy_tx_data_comb = header_byte_comb;
            disable phy_tx_data_comb_func;
        end
        if (state_reg == ST_PIO_WRITE) begin
            phy_tx_data_comb = tx_fifo_data_comb;
            disable phy_tx_data_comb_func;
        end
        phy_tx_data_comb = tx_byte_comb;
    end

    always_comb begin : phy_tx_last_comb_func  // phy_tx_last_comb_func
        if (state_reg == ST_HEADER) begin
            phy_tx_last_comb=!write_mode_reg && (header_index_reg == (HEADER_BYTES - 'h1));
            disable phy_tx_last_comb_func;
        end
        if ((state_reg == ST_PIO_WRITE) || (state_reg == ST_DMA_WRITE_SEND)) begin
            phy_tx_last_comb=count_reg + 'h1>=len_reg;
            disable phy_tx_last_comb_func;
        end
        phy_tx_last_comb=0;
    end

    always_comb begin : phy_rx_ready_comb_func  // phy_rx_ready_comb_func
        if (state_reg == ST_PIO_READ) begin
            phy_rx_ready_comb=!rx_fifo_full_comb;
            disable phy_rx_ready_comb_func;
        end
        if ((state_reg == ST_DMA_READ_RECV) || (state_reg == ST_WAIT_ACK)) begin
            phy_rx_ready_comb=1;
            disable phy_rx_ready_comb_func;
        end
        phy_rx_ready_comb=0;
    end

    always_comb begin : debug_status_comb_func  // debug_status_comb_func
        debug_status_comb=(((((((busy_reg) ? ('h1) : ('h0)) | ((done_reg) ? ('h2) : ('h0))) | ((error_reg) ? ('h4) : ('h0))) | ((rx_fifo_valid_comb) ? ('h8) : ('h0))) | ((!tx_fifo_full_comb) ? ('h10) : ('h0))) | ((irq_pending_reg) ? ('h20) : ('h0))) | ((!desc_fifo_full_comb) ? (C_STATUS_DESC_READY) : ('h0));
    end

    always_comb begin : read_data_comb_func  // read_data_comb_func
        logic[31:0] raddr;
        logic[31:0] word;
        logic[63:0] lane;
        read_data_comb = 'h0;
        word='h0;
        raddr=(axi_in__arvalid_in) ? (unsigned'(32'(axi_in__araddr_in))) : (unsigned'(32'(read_addr_reg)));
        if (raddr == 'h4) begin
            word=debug_status_comb;
        end
        else begin
            if (raddr == 'h8) begin
                word=cmd_reg;
            end
            else begin
                if (raddr == 'hC) begin
                    word=arg_reg;
                end
                else begin
                    if (raddr == 'h10) begin
                        word=len_reg;
                    end
                    else begin
                        if (raddr == 'h14) begin
                            word=dma_addr_reg;
                        end
                        else begin
                            if (raddr == 'h1C) begin
                                word=(rx_fifo_valid_comb) ? (unsigned'(32'(rx_fifo_data_comb))) : ('h0);
                            end
                            else begin
                                if (raddr == 'h20) begin
                                    word=(irq_enable_reg) ? ('h1) : ('h0);
                                end
                                else begin
                                    if (raddr == 'h24) begin
                                        word=(irq_pending_reg) ? ('h1) : ('h0);
                                    end
                                    else begin
                                        if (raddr == C_REG_DMA_DESC_ADDR) begin
                                            word=dma_desc_addr_reg;
                                        end
                                        else begin
                                            if (raddr == C_REG_DMA_DESC_LEN) begin
                                                word=dma_desc_len_reg;
                                            end
                                            else begin
                                                if (raddr == C_REG_DMA_DESC_STATUS) begin
                                                    word=((((!desc_fifo_full_comb) ? (C_DESC_STATUS_READY) : ('h0)) | ((!desc_fifo_valid_comb) ? (C_DESC_STATUS_EMPTY) : ('h0))) | ((desc_fifo_full_comb) ? (C_DESC_STATUS_FULL) : ('h0))) | ((unsigned'(32'(desc_count_reg)) <<< C_DESC_STATUS_COUNT_SHIFT));
                                                end
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
        for (lane='h0;lane < (DATA_BYTES/'h4);lane=lane+1) begin
            read_data_comb[lane*'h20 +:32] = word;
        end
    end

    always_comb begin : dma_addr_comb_func  // dma_addr_comb_func
        logic[31:0] addr;
        addr=(active_desc_valid_reg) ? (unsigned'(32'(active_desc_addr_reg))) : (unsigned'(32'(dma_addr_reg)) + unsigned'(32'(dma_beat_base_reg)));
        dma_addr_comb = unsigned'(ADDR_WIDTH'(unsigned'(ADDR_WIDTH'(addr))));
    end

    generate  // _assign
        assign axi_in__awready_out = !write_addr_valid_reg && !write_resp_valid_reg;
        assign axi_in__wready_out = ((write_addr_valid_reg && !write_resp_valid_reg) && (((unsigned'(32'(write_addr_reg)) != C_REG_TXDATA) || !tx_fifo_full_comb))) && (((unsigned'(32'(write_addr_reg)) != C_REG_DMA_DESC_PUSH) || !desc_fifo_full_comb));
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = !read_valid_reg;
        assign axi_in__rvalid_out = read_valid_reg;
        assign axi_in__rdata_out = read_data_reg;
        assign axi_in__rlast_out = read_valid_reg;
        assign axi_in__rid_out = read_id_reg;
        assign dma_out__awvalid_out = state_reg == ST_DMA_READ_AW;
        assign dma_out__awaddr_out = unsigned'(ADDR_WIDTH'(dma_addr_comb));
        assign dma_out__awid_out = unsigned'(ID_WIDTH'(unsigned'(ID_WIDTH'('h0))));
        assign dma_out__wvalid_out = state_reg == ST_DMA_READ_W;
        assign dma_out__wdata_out = dma_beat_reg;
        assign dma_out__wlast_out = state_reg == ST_DMA_READ_W;
        assign dma_out__bready_out = state_reg == ST_DMA_READ_B;
        assign dma_out__arvalid_out = state_reg == ST_DMA_WRITE_AR;
        assign dma_out__araddr_out = unsigned'(ADDR_WIDTH'(dma_addr_comb));
        assign dma_out__arid_out = unsigned'(ID_WIDTH'(unsigned'(ID_WIDTH'('h0))));
        assign dma_out__rready_out = state_reg == ST_DMA_WRITE_R;
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] addr;
        logic[31:0] word;
        logic[31:0] next_count;
        logic[31:0] next_byte_index;
        logic[31:0] beat_limit;
        logic[63:0] i;
        logic[64-1:0] beat;
        logic tx_push;
        logic tx_pop;
        logic rx_push;
        logic rx_pop;
        logic desc_push;
        logic desc_pop;
        logic[31:0] next_desc_remaining;
        tx_push=0;
        desc_push=0;
        desc_pop=0;
        dma_write_complete_reg_tmp = unsigned'(1'(0));
        if (tx_valid_reg && sd_cmd_ready_in) begin
            tx_valid_reg_tmp = unsigned'(1'(0));
        end
        if (!tx_valid_reg && phy_tx_valid_comb) begin
            tx_valid_reg_tmp = unsigned'(1'(1));
            tx_data_reg_tmp = phy_tx_data_comb;
            tx_last_reg_tmp = unsigned'(1'(phy_tx_last_comb));
        end
        if (rx_valid_reg && phy_rx_ready_comb) begin
            rx_valid_reg_tmp = unsigned'(1'(0));
        end
        if (!rx_valid_reg && sd_rsp_valid_in) begin
            rx_valid_reg_tmp = unsigned'(1'(1));
            rx_data_reg_tmp = sd_rsp_data_in;
            rx_last_reg_tmp = unsigned'(1'(sd_rsp_last_in));
        end
        tx_pop=((state_reg == ST_PIO_WRITE) && !tx_valid_reg) && tx_fifo_valid_comb;
        rx_push=((state_reg == ST_PIO_READ) && rx_valid_reg) && !rx_fifo_full_comb;
        rx_pop=((axi_in__arvalid_in && axi_in__arready_out) && (unsigned'(32'(axi_in__araddr_in)) == 'h1C)) && rx_fifo_valid_comb;
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            read_addr_reg_tmp = axi_in__araddr_in;
            read_id_reg_tmp = axi_in__arid_in;
            read_data_reg_tmp = read_data_comb;
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
            if (addr == 'h0) begin
                if (((word & 'h10)) && !((word & 'h1))) begin
                    done_reg_tmp = unsigned'(1'(0));
                    error_reg_tmp = unsigned'(1'(0));
                    irq_pending_reg_tmp = unsigned'(1'(0));
                    desc_rd_reg_tmp = '0;
                    desc_wr_reg_tmp = '0;
                    desc_count_reg_tmp = '0;
                    active_desc_valid_reg_tmp = '0;
                    active_desc_addr_reg_tmp = '0;
                    active_desc_remaining_reg_tmp = '0;
                end
                if (((word & 'h1)) && !busy_reg) begin
                    busy_reg_tmp = unsigned'(1'(1));
                    done_reg_tmp = unsigned'(1'(0));
                    error_reg_tmp = unsigned'(1'(0));
                    irq_pending_reg_tmp = unsigned'(1'(0));
                    write_mode_reg_tmp = unsigned'(1'(((word & 'h2)) != 'h0));
                    dma_mode_reg_tmp = unsigned'(1'(((word & 'h4)) != 'h0));
                    header_index_reg_tmp = 'h0;
                    count_reg_tmp = unsigned'(32'('h0));
                    dma_beat_reg_tmp = 'h0;
                    dma_beat_base_reg_tmp = unsigned'(32'('h0));
                    dma_byte_index_reg_tmp = unsigned'(32'('h0));
                    dma_beat_limit_reg_tmp = unsigned'(32'('h0));
                    active_desc_valid_reg_tmp = unsigned'(1'(0));
                    active_desc_addr_reg_tmp = unsigned'(32'('h0));
                    active_desc_remaining_reg_tmp = unsigned'(32'('h0));
                    state_reg_tmp = ST_HEADER;
                    if (((word & 'h2)) != 'h0) begin
                        cmd_reg_tmp = 'h58;
                    end
                    else begin
                        cmd_reg_tmp = 'h51;
                    end
                end
            end
            else begin
                if (addr == 'h8) begin
                    cmd_reg_tmp = unsigned'(8'(word));
                end
                else begin
                    if (addr == 'hC) begin
                        arg_reg_tmp = unsigned'(32'(word));
                    end
                    else begin
                        if (addr == 'h10) begin
                            len_reg_tmp = unsigned'(32'(word));
                        end
                        else begin
                            if (addr == 'h14) begin
                                dma_addr_reg_tmp = unsigned'(32'(word));
                            end
                            else begin
                                if (addr == C_REG_DMA_DESC_ADDR) begin
                                    dma_desc_addr_reg_tmp = unsigned'(32'(word));
                                end
                                else begin
                                    if (addr == C_REG_DMA_DESC_LEN) begin
                                        dma_desc_len_reg_tmp = unsigned'(32'(word));
                                    end
                                    else begin
                                        if ((addr == C_REG_DMA_DESC_PUSH) && !desc_fifo_full_comb) begin
                                            desc_push=1;
                                            desc_addr_data_reg_tmp[unsigned'(32'(desc_wr_reg))] = dma_desc_addr_reg;
                                            desc_len_data_reg_tmp[unsigned'(32'(desc_wr_reg))] = dma_desc_len_reg;
                                            desc_wr_reg_tmp = desc_fifo_wr_next_comb;
                                        end
                                        else begin
                                            if (addr == 'h20) begin
                                                irq_enable_reg_tmp = unsigned'(1'(((word & 'h1)) != 'h0));
                                            end
                                            else begin
                                                if ((addr == 'h24) && ((word & 'h1))) begin
                                                    irq_pending_reg_tmp = unsigned'(1'(0));
                                                end
                                                else begin
                                                    if ((addr == 'h18) && !tx_fifo_full_comb) begin
                                                        tx_push=1;
                                                        tx_fifo_data_reg[unsigned'(32'(tx_fifo_wr_reg))] <= unsigned'(8'(unsigned'(8'((word & 'hFF)))));
                                                        tx_fifo_wr_reg_tmp = tx_fifo_wr_next_comb;
                                                    end
                                                end
                                            end
                                        end
                                    end
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
        if (tx_pop) begin
            tx_fifo_rd_reg_tmp = tx_fifo_rd_next_comb;
        end
        if (tx_push && !tx_pop) begin
            tx_fifo_count_reg_tmp = tx_fifo_count_reg + 'h1;
        end
        else begin
            if (!tx_push && tx_pop) begin
                tx_fifo_count_reg_tmp = tx_fifo_count_reg - 'h1;
            end
        end
        if (rx_push) begin
            rx_fifo_data_reg[unsigned'(32'(rx_fifo_wr_reg))] <= sd_rsp_data_in;
            rx_fifo_wr_reg_tmp = rx_fifo_wr_next_comb;
        end
        if (rx_pop) begin
            rx_fifo_rd_reg_tmp = rx_fifo_rd_next_comb;
        end
        if (rx_push && !rx_pop) begin
            rx_fifo_count_reg_tmp = rx_fifo_count_reg + 'h1;
        end
        else begin
            if (!rx_push && rx_pop) begin
                rx_fifo_count_reg_tmp = rx_fifo_count_reg - 'h1;
            end
        end
        if (state_reg == ST_HEADER) begin
            if (!tx_valid_reg) begin
                if (header_index_reg + 'h1>=HEADER_BYTES) begin
                    count_reg_tmp = unsigned'(32'('h0));
                    if (dma_mode_reg) begin
                        state_reg_tmp = ST_DMA_LOAD_DESC;
                    end
                    else begin
                        if (write_mode_reg) begin
                            state_reg_tmp = ST_PIO_WRITE;
                        end
                        else begin
                            state_reg_tmp = ST_PIO_READ;
                        end
                    end
                end
                else begin
                    header_index_reg_tmp = header_index_reg + 'h1;
                end
            end
        end
        else begin
            if (state_reg == ST_PIO_READ) begin
                if (rx_push) begin
                    if (count_reg + 'h1>=len_reg) begin
                        state_reg_tmp = ST_DONE;
                    end
                    count_reg_tmp = unsigned'(32'(count_reg + 'h1));
                end
            end
            else begin
                if (state_reg == ST_PIO_WRITE) begin
                    if (tx_pop) begin
                        if (count_reg + 'h1>=len_reg) begin
                            state_reg_tmp = ST_WAIT_ACK;
                        end
                        count_reg_tmp = unsigned'(32'(count_reg + 'h1));
                    end
                end
                else begin
                    if (state_reg == ST_WAIT_ACK) begin
                        if (rx_valid_reg) begin
                            if (unsigned'(32'(rx_data_reg)) == 'h0) begin
                                state_reg_tmp = ST_DONE;
                            end
                            else begin
                                state_reg_tmp = ST_ERROR;
                            end
                        end
                    end
                    else begin
                        if (state_reg == ST_DMA_LOAD_DESC) begin
                            dma_beat_reg_tmp = 'h0;
                            dma_byte_index_reg_tmp = unsigned'(32'('h0));
                            dma_beat_limit_reg_tmp = unsigned'(32'('h0));
                            if (count_reg>=len_reg) begin
                                state_reg_tmp = (write_mode_reg) ? (ST_WAIT_ACK) : (ST_DONE);
                            end
                            else begin
                                if (desc_fifo_valid_comb) begin
                                    desc_pop=1;
                                    active_desc_valid_reg_tmp = unsigned'(1'(1));
                                    active_desc_addr_reg_tmp = desc_addr_data_reg[unsigned'(32'(desc_rd_reg))];
                                    active_desc_remaining_reg_tmp = desc_len_data_reg[unsigned'(32'(desc_rd_reg))];
                                    desc_rd_reg_tmp = desc_fifo_rd_next_comb;
                                    state_reg_tmp = (write_mode_reg) ? (ST_DMA_WRITE_AR) : (ST_DMA_READ_RECV);
                                end
                                else begin
                                    active_desc_valid_reg_tmp = unsigned'(1'(1));
                                    active_desc_addr_reg_tmp = unsigned'(32'(unsigned'(32'(dma_addr_reg)) + unsigned'(32'(count_reg))));
                                    active_desc_remaining_reg_tmp = unsigned'(32'(unsigned'(32'(len_reg)) - unsigned'(32'(count_reg))));
                                    state_reg_tmp = (write_mode_reg) ? (ST_DMA_WRITE_AR) : (ST_DMA_READ_RECV);
                                end
                            end
                        end
                        else begin
                            if (state_reg == ST_DMA_READ_RECV) begin
                                beat_limit=(dma_beat_limit_reg) ? (unsigned'(32'(dma_beat_limit_reg))) : (dma_next_beat_limit_comb);
                                if (rx_valid_reg && (beat_limit != 'h0)) begin
                                    if (dma_byte_index_reg == 'h0) begin
                                        dma_beat_limit_reg_tmp = unsigned'(32'(beat_limit));
                                    end
                                    beat = dma_beat_reg;
                                    for (i='h0;i < DATA_BYTES;i=i+1) begin
                                        if (unsigned'(32'(dma_byte_index_reg)) == i) begin
                                            beat[i*'h8 +:8] = rx_data_reg;
                                        end
                                    end
                                    dma_beat_reg_tmp = beat;
                                    next_count=unsigned'(32'(count_reg)) + 'h1;
                                    next_byte_index=unsigned'(32'(dma_byte_index_reg)) + 'h1;
                                    next_desc_remaining=(active_desc_remaining_reg) ? (unsigned'(32'(active_desc_remaining_reg)) - 'h1) : ('h0);
                                    count_reg_tmp = unsigned'(32'(next_count));
                                    dma_byte_index_reg_tmp = unsigned'(32'(next_byte_index));
                                    active_desc_remaining_reg_tmp = unsigned'(32'(next_desc_remaining));
                                    if (next_byte_index>=beat_limit || next_count>=unsigned'(32'(len_reg))) begin
                                        dma_beat_limit_reg_tmp = unsigned'(32'(next_byte_index));
                                        state_reg_tmp = ST_DMA_READ_AW;
                                    end
                                end
                            end
                            else begin
                                if (state_reg == ST_DMA_READ_AW) begin
                                    if (dma_out__awvalid_out && dma_out__awready_in) begin
                                        state_reg_tmp = ST_DMA_READ_W;
                                    end
                                end
                                else begin
                                    if (state_reg == ST_DMA_READ_W) begin
                                        if (dma_out__wvalid_out && dma_out__wready_in) begin
                                            state_reg_tmp = ST_DMA_READ_B;
                                        end
                                    end
                                    else begin
                                        if (state_reg == ST_DMA_READ_B) begin
                                            if (dma_out__bvalid_in && dma_out__bready_out) begin
                                                dma_write_complete_reg_tmp = unsigned'(1'(1));
                                                if (count_reg>=len_reg) begin
                                                    state_reg_tmp = ST_DONE;
                                                end
                                                else begin
                                                    if (active_desc_remaining_reg == 'h0) begin
                                                        active_desc_valid_reg_tmp = unsigned'(1'(0));
                                                        state_reg_tmp = ST_DMA_LOAD_DESC;
                                                    end
                                                    else begin
                                                        active_desc_addr_reg_tmp = unsigned'(32'(unsigned'(32'(active_desc_addr_reg)) + unsigned'(32'(dma_beat_limit_reg))));
                                                        dma_beat_reg_tmp = 'h0;
                                                        dma_byte_index_reg_tmp = unsigned'(32'('h0));
                                                        dma_beat_limit_reg_tmp = unsigned'(32'('h0));
                                                        state_reg_tmp = ST_DMA_READ_RECV;
                                                    end
                                                end
                                            end
                                        end
                                        else begin
                                            if (state_reg == ST_DMA_WRITE_AR) begin
                                                if (dma_out__arvalid_out && dma_out__arready_in) begin
                                                    state_reg_tmp = ST_DMA_WRITE_R;
                                                end
                                            end
                                            else begin
                                                if (state_reg == ST_DMA_WRITE_R) begin
                                                    if (dma_out__rvalid_in && dma_out__rready_out) begin
                                                        dma_beat_reg_tmp = dma_out__rdata_in;
                                                        dma_byte_index_reg_tmp = unsigned'(32'('h0));
                                                        dma_beat_limit_reg_tmp = unsigned'(32'(dma_next_beat_limit_comb));
                                                        state_reg_tmp = ST_DMA_WRITE_SEND;
                                                    end
                                                end
                                                else begin
                                                    if (state_reg == ST_DMA_WRITE_SEND) begin
                                                        beat_limit=dma_beat_limit_reg;
                                                        if ((!tx_valid_reg && (count_reg < len_reg)) && (beat_limit != 'h0)) begin
                                                            next_count=unsigned'(32'(count_reg)) + 'h1;
                                                            next_byte_index=unsigned'(32'(dma_byte_index_reg)) + 'h1;
                                                            next_desc_remaining=(active_desc_remaining_reg) ? (unsigned'(32'(active_desc_remaining_reg)) - 'h1) : ('h0);
                                                            count_reg_tmp = unsigned'(32'(next_count));
                                                            dma_byte_index_reg_tmp = unsigned'(32'(next_byte_index));
                                                            active_desc_remaining_reg_tmp = unsigned'(32'(next_desc_remaining));
                                                            if (next_count>=unsigned'(32'(len_reg))) begin
                                                                state_reg_tmp = ST_WAIT_ACK;
                                                            end
                                                            else begin
                                                                if (next_byte_index>=beat_limit) begin
                                                                    if (next_desc_remaining == 'h0) begin
                                                                        active_desc_valid_reg_tmp = unsigned'(1'(0));
                                                                        state_reg_tmp = ST_DMA_LOAD_DESC;
                                                                    end
                                                                    else begin
                                                                        active_desc_addr_reg_tmp = unsigned'(32'(unsigned'(32'(active_desc_addr_reg)) + beat_limit));
                                                                        dma_byte_index_reg_tmp = unsigned'(32'('h0));
                                                                        dma_beat_limit_reg_tmp = unsigned'(32'('h0));
                                                                        state_reg_tmp = ST_DMA_WRITE_AR;
                                                                    end
                                                                end
                                                            end
                                                        end
                                                    end
                                                    else begin
                                                        if (state_reg == ST_DONE) begin
                                                            busy_reg_tmp = unsigned'(1'(0));
                                                            done_reg_tmp = unsigned'(1'(1));
                                                            irq_pending_reg_tmp = unsigned'(1'(1));
                                                            state_reg_tmp = ST_IDLE;
                                                        end
                                                        else begin
                                                            if (state_reg == ST_ERROR) begin
                                                                busy_reg_tmp = unsigned'(1'(0));
                                                                done_reg_tmp = unsigned'(1'(1));
                                                                error_reg_tmp = unsigned'(1'(1));
                                                                irq_pending_reg_tmp = unsigned'(1'(1));
                                                                state_reg_tmp = ST_IDLE;
                                                            end
                                                        end
                                                    end
                                                end
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
        if (desc_push && !desc_pop) begin
            desc_count_reg_tmp = desc_count_reg + 'h1;
        end
        else begin
            if (!desc_push && desc_pop) begin
                desc_count_reg_tmp = desc_count_reg - 'h1;
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
            cmd_reg_tmp = 'h51;
            arg_reg_tmp = '0;
            len_reg_tmp = unsigned'(32'('h200));
            dma_addr_reg_tmp = '0;
            dma_desc_addr_reg_tmp = '0;
            dma_desc_len_reg_tmp = '0;
            count_reg_tmp = '0;
            state_reg_tmp = '0;
            header_index_reg_tmp = '0;
            write_mode_reg_tmp = '0;
            dma_mode_reg_tmp = '0;
            busy_reg_tmp = '0;
            done_reg_tmp = '0;
            error_reg_tmp = '0;
            irq_enable_reg_tmp = '0;
            irq_pending_reg_tmp = '0;
            dma_write_complete_reg_tmp = '0;
            dma_beat_reg_tmp = '0;
            dma_beat_base_reg_tmp = '0;
            dma_byte_index_reg_tmp = '0;
            dma_beat_limit_reg_tmp = '0;
            desc_rd_reg_tmp = '0;
            desc_wr_reg_tmp = '0;
            desc_count_reg_tmp = '0;
            active_desc_addr_reg_tmp = '0;
            active_desc_remaining_reg_tmp = '0;
            active_desc_valid_reg_tmp = '0;
            tx_fifo_rd_reg_tmp = '0;
            tx_fifo_wr_reg_tmp = '0;
            tx_fifo_count_reg_tmp = '0;
            rx_fifo_rd_reg_tmp = '0;
            rx_fifo_wr_reg_tmp = '0;
            rx_fifo_count_reg_tmp = '0;
            tx_valid_reg_tmp = '0;
            tx_data_reg_tmp = '0;
            tx_last_reg_tmp = '0;
            rx_valid_reg_tmp = '0;
            rx_data_reg_tmp = '0;
            rx_last_reg_tmp = '0;
        end
    end
    endtask

    always @(posedge clk) begin
        tx_fifo_rd_reg_tmp = tx_fifo_rd_reg;
        tx_fifo_wr_reg_tmp = tx_fifo_wr_reg;
        tx_fifo_count_reg_tmp = tx_fifo_count_reg;
        rx_fifo_rd_reg_tmp = rx_fifo_rd_reg;
        rx_fifo_wr_reg_tmp = rx_fifo_wr_reg;
        rx_fifo_count_reg_tmp = rx_fifo_count_reg;
        tx_valid_reg_tmp = tx_valid_reg;
        tx_data_reg_tmp = tx_data_reg;
        tx_last_reg_tmp = tx_last_reg;
        rx_valid_reg_tmp = rx_valid_reg;
        rx_data_reg_tmp = rx_data_reg;
        rx_last_reg_tmp = rx_last_reg;
        read_addr_reg_tmp = read_addr_reg;
        read_id_reg_tmp = read_id_reg;
        read_valid_reg_tmp = read_valid_reg;
        read_data_reg_tmp = read_data_reg;
        write_addr_reg_tmp = write_addr_reg;
        write_id_reg_tmp = write_id_reg;
        write_addr_valid_reg_tmp = write_addr_valid_reg;
        write_resp_valid_reg_tmp = write_resp_valid_reg;
        cmd_reg_tmp = cmd_reg;
        arg_reg_tmp = arg_reg;
        len_reg_tmp = len_reg;
        dma_addr_reg_tmp = dma_addr_reg;
        dma_desc_addr_reg_tmp = dma_desc_addr_reg;
        dma_desc_len_reg_tmp = dma_desc_len_reg;
        count_reg_tmp = count_reg;
        state_reg_tmp = state_reg;
        header_index_reg_tmp = header_index_reg;
        write_mode_reg_tmp = write_mode_reg;
        dma_mode_reg_tmp = dma_mode_reg;
        busy_reg_tmp = busy_reg;
        done_reg_tmp = done_reg;
        error_reg_tmp = error_reg;
        irq_enable_reg_tmp = irq_enable_reg;
        irq_pending_reg_tmp = irq_pending_reg;
        dma_write_complete_reg_tmp = dma_write_complete_reg;
        dma_beat_reg_tmp = dma_beat_reg;
        dma_beat_base_reg_tmp = dma_beat_base_reg;
        dma_byte_index_reg_tmp = dma_byte_index_reg;
        dma_beat_limit_reg_tmp = dma_beat_limit_reg;
        desc_addr_data_reg_tmp = desc_addr_data_reg;
        desc_len_data_reg_tmp = desc_len_data_reg;
        desc_rd_reg_tmp = desc_rd_reg;
        desc_wr_reg_tmp = desc_wr_reg;
        desc_count_reg_tmp = desc_count_reg;
        active_desc_addr_reg_tmp = active_desc_addr_reg;
        active_desc_remaining_reg_tmp = active_desc_remaining_reg;
        active_desc_valid_reg_tmp = active_desc_valid_reg;

        _work(reset);

        tx_fifo_rd_reg <= tx_fifo_rd_reg_tmp;
        tx_fifo_wr_reg <= tx_fifo_wr_reg_tmp;
        tx_fifo_count_reg <= tx_fifo_count_reg_tmp;
        rx_fifo_rd_reg <= rx_fifo_rd_reg_tmp;
        rx_fifo_wr_reg <= rx_fifo_wr_reg_tmp;
        rx_fifo_count_reg <= rx_fifo_count_reg_tmp;
        tx_valid_reg <= tx_valid_reg_tmp;
        tx_data_reg <= tx_data_reg_tmp;
        tx_last_reg <= tx_last_reg_tmp;
        rx_valid_reg <= rx_valid_reg_tmp;
        rx_data_reg <= rx_data_reg_tmp;
        rx_last_reg <= rx_last_reg_tmp;
        read_addr_reg <= read_addr_reg_tmp;
        read_id_reg <= read_id_reg_tmp;
        read_valid_reg <= read_valid_reg_tmp;
        read_data_reg <= read_data_reg_tmp;
        write_addr_reg <= write_addr_reg_tmp;
        write_id_reg <= write_id_reg_tmp;
        write_addr_valid_reg <= write_addr_valid_reg_tmp;
        write_resp_valid_reg <= write_resp_valid_reg_tmp;
        cmd_reg <= cmd_reg_tmp;
        arg_reg <= arg_reg_tmp;
        len_reg <= len_reg_tmp;
        dma_addr_reg <= dma_addr_reg_tmp;
        dma_desc_addr_reg <= dma_desc_addr_reg_tmp;
        dma_desc_len_reg <= dma_desc_len_reg_tmp;
        count_reg <= count_reg_tmp;
        state_reg <= state_reg_tmp;
        header_index_reg <= header_index_reg_tmp;
        write_mode_reg <= write_mode_reg_tmp;
        dma_mode_reg <= dma_mode_reg_tmp;
        busy_reg <= busy_reg_tmp;
        done_reg <= done_reg_tmp;
        error_reg <= error_reg_tmp;
        irq_enable_reg <= irq_enable_reg_tmp;
        irq_pending_reg <= irq_pending_reg_tmp;
        dma_write_complete_reg <= dma_write_complete_reg_tmp;
        dma_beat_reg <= dma_beat_reg_tmp;
        dma_beat_base_reg <= dma_beat_base_reg_tmp;
        dma_byte_index_reg <= dma_byte_index_reg_tmp;
        dma_beat_limit_reg <= dma_beat_limit_reg_tmp;
        desc_addr_data_reg <= desc_addr_data_reg_tmp;
        desc_len_data_reg <= desc_len_data_reg_tmp;
        desc_rd_reg <= desc_rd_reg_tmp;
        desc_wr_reg <= desc_wr_reg_tmp;
        desc_count_reg <= desc_count_reg_tmp;
        active_desc_addr_reg <= active_desc_addr_reg_tmp;
        active_desc_remaining_reg <= active_desc_remaining_reg_tmp;
        active_desc_valid_reg <= active_desc_valid_reg_tmp;
    end

    assign sd_cmd_valid_out = tx_valid_reg;

    assign sd_cmd_data_out = tx_data_reg;

    assign sd_cmd_last_out = tx_last_reg;

    assign sd_rsp_ready_out = !rx_valid_reg;

    assign irq_out = (irq_pending_reg && irq_enable_reg);

    assign dma_write_complete_out = dma_write_complete_reg;

    assign debug_status_out = debug_status_comb;

    assign debug_state_out = unsigned'(32'(state_reg));

    assign debug_count_out = unsigned'(32'(count_reg));

    assign debug_len_out = unsigned'(32'(len_reg));


endmodule

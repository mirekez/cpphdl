`default_nettype none

import Predef_pkg::*;


module PLIC #(
    parameter ADDR_WIDTH
,   parameter ID_WIDTH
,   parameter DATA_WIDTH
,   parameter SOURCES
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
,   input wire source_irq_in[SOURCES]
,   output wire external_irq_out
);
    parameter  PRIORITY_BASE = 'h0;
    parameter  PENDING_BASE = 'h1000;
    parameter  ENABLE_BASE = 'h2000;
    parameter  CONTEXT_BASE = 'h200000;
    parameter  CLAIM_OFFSET = 'h4;


    // regs and combs
    reg[ADDR_WIDTH-1:0] read_addr_reg;
    reg[ID_WIDTH-1:0] read_id_reg;
    reg read_valid_reg;
    reg[DATA_WIDTH-1:0] read_data_reg;
    reg[ADDR_WIDTH-1:0] write_addr_reg;
    reg[ID_WIDTH-1:0] write_id_reg;
    reg write_addr_valid_reg;
    reg write_resp_valid_reg;
    reg[32-1:0] priority_reg[SOURCES];
    reg[32-1:0] enable_reg;
    reg[32-1:0] threshold_reg;
    reg[32-1:0] pending_reg;
    reg[32-1:0] gateway_busy_reg;
    logic[31:0] source_bits_comb;
;
    logic[31:0] pending_bits_comb;
;
    logic[31:0] claim_comb;
;
    logic external_irq_comb;
;
    logic[DATA_WIDTH-1:0] read_data_comb;
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
    logic[32-1:0] priority_reg_tmp[SOURCES];
    logic[32-1:0] enable_reg_tmp;
    logic[32-1:0] threshold_reg_tmp;
    logic[32-1:0] pending_reg_tmp;
    logic[32-1:0] gateway_busy_reg_tmp;


    always_comb begin : source_bits_comb_func  // source_bits_comb_func
        logic[63:0] i;
        source_bits_comb='h0;
        for (i='h1;(i < SOURCES) && (i < 'h20);i=i+1) begin
            if (source_irq_in[i]) begin
                source_bits_comb|='h1 <<< i;
            end
        end
    end

    always_comb begin : pending_bits_comb_func  // pending_bits_comb_func
        pending_bits_comb=pending_reg;
    end

    always_comb begin : claim_comb_func  // claim_comb_func
        logic[63:0] i;
        logic[31:0] pending;
        claim_comb='h0;
        pending=pending_bits_comb & enable_reg;
        for (i='h1;(i < SOURCES) && (i < 'h20);i=i+1) begin
            if (((claim_comb == 'h0) && ((((pending >>> i)) & 'h1))) && (unsigned'(32'(priority_reg[i])) > unsigned'(32'(threshold_reg)))) begin
                claim_comb=i;
            end
        end
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
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        logic[31:0] addr;
        logic[31:0] lane;
        logic[31:0] data;
        logic[31:0] source;
        logic[31:0] source_bits;
        logic[31:0] pending_next;
        logic[31:0] pending_work;
        logic[31:0] claim_on_read;
        logic[31:0] read_word;
        logic[31:0] completion_mask;
        logic[64-1:0] read_data;
        completion_mask='h0;
        if (axi_in__wvalid_in && axi_in__wready_out) begin
            addr=unsigned'(32'(write_addr_reg));
            lane=(unsigned'(32'(write_addr_reg)) % ((DATA_WIDTH/'h8)));
            data=unsigned'(32'(axi_in__wdata_in[lane*'h8 +:32]));
            if (((addr == (CONTEXT_BASE + CLAIM_OFFSET)) && (data > 'h0)) && (data < 'h20)) begin
                completion_mask='h1 <<< data;
            end
        end
        source_bits=source_bits_comb & ~completion_mask;
        pending_next=unsigned'(32'(pending_reg)) | ((source_bits & ~unsigned'(32'(gateway_busy_reg))));
        pending_work=pending_next;
        pending_reg_tmp = unsigned'(32'(pending_work));
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            addr=unsigned'(32'(axi_in__araddr_in));
            read_word='h0;
            if (addr>=PRIORITY_BASE && (addr < PENDING_BASE)) begin
                source=((addr - PRIORITY_BASE))/'h4;
                if (source < SOURCES) begin
                    read_word=priority_reg[source];
                end
            end
            else begin
                if (addr == PENDING_BASE) begin
                    read_word=pending_work;
                end
                else begin
                    if (addr == ENABLE_BASE) begin
                        read_word=enable_reg;
                    end
                    else begin
                        if (addr == CONTEXT_BASE) begin
                            read_word=threshold_reg;
                        end
                        else begin
                            if (addr == (CONTEXT_BASE + CLAIM_OFFSET)) begin
                                claim_on_read=claim_comb;
                                read_word=claim_on_read;
                                if ((claim_on_read > 'h0) && (claim_on_read < 'h20)) begin
                                    pending_work&=~('h1 <<< claim_on_read);
                                    pending_reg_tmp = unsigned'(32'(pending_work));
                                    gateway_busy_reg_tmp = unsigned'(32'(unsigned'(32'(gateway_busy_reg)) | (('h1 <<< claim_on_read))));
                                end
                            end
                        end
                    end
                end
            end
            lane=addr % ((DATA_WIDTH/'h8));
            read_data = 'h0;
            read_data[lane*'h8 +:32] = read_word;
            read_data_reg_tmp = read_data;
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
            lane=(unsigned'(32'(write_addr_reg)) % ((DATA_WIDTH/'h8)));
            data=unsigned'(32'(axi_in__wdata_in[lane*'h8 +:32]));
            write_addr_valid_reg_tmp = unsigned'(1'(0));
            write_resp_valid_reg_tmp = unsigned'(1'(1));
            if (addr>=PRIORITY_BASE && (addr < PENDING_BASE)) begin
                source=((addr - PRIORITY_BASE))/'h4;
                if (source < SOURCES) begin
                    priority_reg_tmp[source] = unsigned'(32'(data));
                end
            end
            else begin
                if (addr == ENABLE_BASE) begin
                    enable_reg_tmp = unsigned'(32'(data));
                end
                else begin
                    if (addr == CONTEXT_BASE) begin
                        threshold_reg_tmp = unsigned'(32'(data));
                    end
                    else begin
                        if (addr == (CONTEXT_BASE + CLAIM_OFFSET)) begin
                            if (completion_mask != 'h0) begin
                                gateway_busy_reg_tmp = unsigned'(32'(unsigned'(32'(gateway_busy_reg)) & ~completion_mask));
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
            read_data_reg_tmp = '0;
            write_addr_reg_tmp = '0;
            write_id_reg_tmp = '0;
            write_addr_valid_reg_tmp = '0;
            write_resp_valid_reg_tmp = '0;
            enable_reg_tmp = '0;
            threshold_reg_tmp = '0;
            pending_reg_tmp = '0;
            gateway_busy_reg_tmp = '0;
            for (i='h0;i < SOURCES;i=i+1) begin
                priority_reg_tmp[i] = unsigned'(32'('h0));
            end
        end
    end
    endtask

    always_comb begin : external_irq_comb_func  // external_irq_comb_func
        external_irq_comb=claim_comb != 'h0;
    end

    always @(posedge clk) begin
        read_addr_reg_tmp = read_addr_reg;
        read_id_reg_tmp = read_id_reg;
        read_valid_reg_tmp = read_valid_reg;
        read_data_reg_tmp = read_data_reg;
        write_addr_reg_tmp = write_addr_reg;
        write_id_reg_tmp = write_id_reg;
        write_addr_valid_reg_tmp = write_addr_valid_reg;
        write_resp_valid_reg_tmp = write_resp_valid_reg;
        priority_reg_tmp = priority_reg;
        enable_reg_tmp = enable_reg;
        threshold_reg_tmp = threshold_reg;
        pending_reg_tmp = pending_reg;
        gateway_busy_reg_tmp = gateway_busy_reg;

        _work(reset);

        read_addr_reg <= read_addr_reg_tmp;
        read_id_reg <= read_id_reg_tmp;
        read_valid_reg <= read_valid_reg_tmp;
        read_data_reg <= read_data_reg_tmp;
        write_addr_reg <= write_addr_reg_tmp;
        write_id_reg <= write_id_reg_tmp;
        write_addr_valid_reg <= write_addr_valid_reg_tmp;
        write_resp_valid_reg <= write_resp_valid_reg_tmp;
        priority_reg <= priority_reg_tmp;
        enable_reg <= enable_reg_tmp;
        threshold_reg <= threshold_reg_tmp;
        pending_reg <= pending_reg_tmp;
        gateway_busy_reg <= gateway_busy_reg_tmp;
    end

    assign external_irq_out = external_irq_comb;


endmodule

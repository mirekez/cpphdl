`default_nettype none

import Predef_pkg::*;


module PLIC #(
    parameter ADDR_WIDTH = 'h20
,   parameter ID_WIDTH = 'h4
,   parameter DATA_WIDTH = 'h100
,   parameter SOURCES = 'h20
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
,   input wire source_irq_in[SOURCES]
,   output wire external_irq_out
);
    localparam  PRIORITY_BASE = 'h0;
    localparam  PENDING_BASE = 'h1000;
    localparam  ENABLE_BASE = 'h2000;
    localparam  CONTEXT_BASE = 'h200000;
    localparam  CONTEXT_STRIDE = 'h1000;
    localparam  ENABLE_STRIDE = 'h80;
    localparam  CLAIM_OFFSET = 'h4;
    localparam  HART_COUNT = 64'h1;


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
    reg[32-1:0] enable_reg[1];
    reg[32-1:0] threshold_reg[1];
    reg[32-1:0] pending_reg;
    reg[32-1:0] gateway_busy_reg;
    logic[31:0] source_bits_comb;
    logic[31:0] pending_bits_comb;
    logic[31:0] claim_comb;
    logic external_irq_comb;
    logic[DATA_WIDTH-1:0] read_data_comb;

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
    logic[32-1:0] enable_reg_tmp[1];
    logic[32-1:0] threshold_reg_tmp[1];
    logic[32-1:0] pending_reg_tmp;
    logic[32-1:0] gateway_busy_reg_tmp;


    always_comb begin : pending_bits_comb_func  // pending_bits_comb_func
        pending_bits_comb=pending_reg;
    end

    function logic[31:0] claim_for_context (input logic[31:0] _context);
        logic[63:0] i;
        logic[31:0] pending;
        logic[31:0] claim;
        claim=32'h0;
        pending=unsigned'(64'(pending_bits_comb)) & enable_reg[_context];
        for (i=64'h1;(i < SOURCES) && (i < 64'h20);i=i+1) begin
            if (((claim == 32'h0) && ((((((pending >>> i)) & 'h1))) != '0)) && (unsigned'(32'(priority_reg[i])) > unsigned'(32'(threshold_reg[_context])))) begin
                claim=i;
            end
        end
        return claim;
    endfunction

    always_comb begin : claim_comb_func  // claim_comb_func
        claim_comb=claim_for_context(32'h0);
    end

    always_comb begin : external_irq_comb_func  // external_irq_comb_func
        external_irq_comb=claim_comb != 32'h0;
    end

    always_comb begin : source_bits_comb_func  // source_bits_comb_func
        logic[63:0] i;
        source_bits_comb=32'h0;
        for (i=64'h1;(i < SOURCES) && (i < 64'h20);i=i+1) begin
            if (source_irq_in[i]) begin
                source_bits_comb|='h1 <<< i;
            end
        end
    end

    always_comb begin : read_data_comb_func  // read_data_comb_func
        logic[31:0] addr;
        logic[31:0] data;
        logic[31:0] lane;
        logic[31:0] source;
        read_data_comb = 'h0;
        addr=read_addr_reg;
        data=32'h0;
        if (addr>=unsigned'(32'(PRIORITY_BASE)) && (addr < unsigned'(32'(PENDING_BASE)))) begin
            source=((addr - PRIORITY_BASE))/32'h4;
            if (64'(source) < SOURCES) begin
                data=priority_reg[source];
            end
        end
        else begin
            if (addr == unsigned'(32'(PENDING_BASE))) begin
                data=pending_bits_comb;
            end
            else begin
                if (addr == unsigned'(32'(ENABLE_BASE))) begin
                    data=enable_reg['h0];
                end
                else begin
                    if (addr == unsigned'(32'(CONTEXT_BASE))) begin
                        data=threshold_reg['h0];
                    end
                    else begin
                        if (addr == (CONTEXT_BASE + CLAIM_OFFSET)) begin
                            data=claim_comb;
                        end
                    end
                end
            end
        end
        lane=(64'(unsigned'(32'(read_addr_reg))) % ((DATA_WIDTH/64'h8)));
        read_data_comb[lane*32'h8 +:32] = 64'(data);
    end

    generate  // _assign
        assign axi_in__awready_out = ((!((write_addr_valid_reg) != '0) && !((write_resp_valid_reg) != '0)) != '0);
        assign axi_in__wready_out = ((((write_addr_valid_reg) != '0) && !((write_resp_valid_reg) != '0)) != '0);
        assign axi_in__bvalid_out = write_resp_valid_reg;
        assign axi_in__bid_out = write_id_reg;
        assign axi_in__arready_out = ((!((read_valid_reg) != '0)) != '0);
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
        logic[31:0] _context;
        logic[31:0] source_bits;
        logic[31:0] pending_next;
        logic[31:0] pending_work;
        logic[31:0] claim_on_read;
        logic[31:0] read_word;
        logic[31:0] completion_mask;
        logic[DATA_WIDTH-1:0] read_data;
        completion_mask=32'h0;
        if (axi_in__wvalid_in && axi_in__wready_out) begin
            addr=write_addr_reg;
            lane=(64'(unsigned'(32'(write_addr_reg))) % ((DATA_WIDTH/64'h8)));
            data=axi_in__wdata_in[lane*32'h8 +:32];
            _context=32'h0;
            if (addr>=unsigned'(32'(CONTEXT_BASE)) && (64'(addr) < (unsigned'(64'(CONTEXT_BASE)) + (HART_COUNT*unsigned'(64'(CONTEXT_STRIDE)))))) begin
                _context=((addr - CONTEXT_BASE))/unsigned'(32'(CONTEXT_STRIDE));
            end
            if ((((64'(_context) < unsigned'(64'(HART_COUNT))) && (addr == ((CONTEXT_BASE + (_context*CONTEXT_STRIDE)) + CLAIM_OFFSET))) && (data > 32'h0)) && (data < 32'h20)) begin
                completion_mask='h1 <<< data;
            end
        end
        source_bits=source_bits_comb & ~completion_mask;
        pending_next=unsigned'(32'(pending_reg)) | ((source_bits & ~unsigned'(32'(gateway_busy_reg))));
        pending_work=pending_next;
        pending_reg_tmp = pending_work;
        if (axi_in__arvalid_in && axi_in__arready_out) begin
            addr=axi_in__araddr_in;
            read_word=32'h0;
            if (addr>=unsigned'(32'(PRIORITY_BASE)) && (addr < unsigned'(32'(PENDING_BASE)))) begin
                source=((addr - PRIORITY_BASE))/32'h4;
                if (64'(source) < SOURCES) begin
                    read_word=priority_reg[source];
                end
            end
            else begin
                if (addr == unsigned'(32'(PENDING_BASE))) begin
                    read_word=pending_work;
                end
                else begin
                    if ((addr>=unsigned'(32'(ENABLE_BASE)) && (64'(addr) < (unsigned'(64'(ENABLE_BASE)) + (HART_COUNT*unsigned'(64'(ENABLE_STRIDE)))))) && (((((addr - ENABLE_BASE)) % unsigned'(32'(ENABLE_STRIDE)))) == 32'h0)) begin
                        _context=((addr - ENABLE_BASE))/unsigned'(32'(ENABLE_STRIDE));
                        read_word=enable_reg[_context];
                    end
                    else begin
                        if ((addr>=unsigned'(32'(CONTEXT_BASE)) && (64'(addr) < (unsigned'(64'(CONTEXT_BASE)) + (HART_COUNT*unsigned'(64'(CONTEXT_STRIDE)))))) && (((((addr - CONTEXT_BASE)) % unsigned'(32'(CONTEXT_STRIDE)))) == 32'h0)) begin
                            _context=((addr - CONTEXT_BASE))/unsigned'(32'(CONTEXT_STRIDE));
                            read_word=threshold_reg[_context];
                        end
                        else begin
                            if ((addr>=(CONTEXT_BASE + CLAIM_OFFSET) && (64'(addr) < ((unsigned'(64'(CONTEXT_BASE)) + (HART_COUNT*unsigned'(64'(CONTEXT_STRIDE)))) + unsigned'(64'(CLAIM_OFFSET))))) && (((((addr - CONTEXT_BASE)) % unsigned'(32'(CONTEXT_STRIDE)))) == unsigned'(32'(CLAIM_OFFSET)))) begin
                                _context=((addr - CONTEXT_BASE))/unsigned'(32'(CONTEXT_STRIDE));
                                claim_on_read=claim_for_context(_context);
                                read_word=claim_on_read;
                                if ((claim_on_read > 32'h0) && (claim_on_read < 32'h20)) begin
                                    pending_work&=~('h1 <<< claim_on_read);
                                    pending_reg_tmp = pending_work;
                                    gateway_busy_reg_tmp = 32'(unsigned'(32'(gateway_busy_reg)) | (('h1 <<< claim_on_read)));
                                end
                            end
                        end
                    end
                end
            end
            lane=64'(addr) % ((DATA_WIDTH/64'h8));
            read_data = 'h0;
            read_data[lane*32'h8 +:32] = 64'(read_word);
            read_data_reg_tmp = read_data;
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
            lane=(64'(unsigned'(32'(write_addr_reg))) % ((DATA_WIDTH/64'h8)));
            data=axi_in__wdata_in[lane*32'h8 +:32];
            write_addr_valid_reg_tmp = 0;
            write_resp_valid_reg_tmp = 1;
            if (addr>=unsigned'(32'(PRIORITY_BASE)) && (addr < unsigned'(32'(PENDING_BASE)))) begin
                source=((addr - PRIORITY_BASE))/32'h4;
                if (64'(source) < SOURCES) begin
                    priority_reg_tmp[source] = data;
                end
            end
            else begin
                if ((addr>=unsigned'(32'(ENABLE_BASE)) && (64'(addr) < (unsigned'(64'(ENABLE_BASE)) + (HART_COUNT*unsigned'(64'(ENABLE_STRIDE)))))) && (((((addr - ENABLE_BASE)) % unsigned'(32'(ENABLE_STRIDE)))) == 32'h0)) begin
                    _context=((addr - ENABLE_BASE))/unsigned'(32'(ENABLE_STRIDE));
                    enable_reg_tmp[_context] = data;
                end
                else begin
                    if ((addr>=unsigned'(32'(CONTEXT_BASE)) && (64'(addr) < (unsigned'(64'(CONTEXT_BASE)) + (HART_COUNT*unsigned'(64'(CONTEXT_STRIDE)))))) && (((((addr - CONTEXT_BASE)) % unsigned'(32'(CONTEXT_STRIDE)))) == 32'h0)) begin
                        _context=((addr - CONTEXT_BASE))/unsigned'(32'(CONTEXT_STRIDE));
                        threshold_reg_tmp[_context] = data;
                    end
                    else begin
                        if ((addr>=(CONTEXT_BASE + CLAIM_OFFSET) && (64'(addr) < ((unsigned'(64'(CONTEXT_BASE)) + (HART_COUNT*unsigned'(64'(CONTEXT_STRIDE)))) + unsigned'(64'(CLAIM_OFFSET))))) && (((((addr - CONTEXT_BASE)) % unsigned'(32'(CONTEXT_STRIDE)))) == unsigned'(32'(CLAIM_OFFSET)))) begin
                            if (completion_mask != 32'h0) begin
                                gateway_busy_reg_tmp = 32'(unsigned'(32'(gateway_busy_reg)) & ~completion_mask);
                            end
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
            read_data_reg_tmp = '0;
            write_addr_reg_tmp = '0;
            write_id_reg_tmp = '0;
            write_addr_valid_reg_tmp = '0;
            write_resp_valid_reg_tmp = '0;
            pending_reg_tmp = '0;
            gateway_busy_reg_tmp = '0;
            for (i=64'h0;i < unsigned'(64'(HART_COUNT));i=i+1) begin
                enable_reg_tmp[i] = '0;
                threshold_reg_tmp[i] = '0;
            end
            for (i=64'h0;i < SOURCES;i=i+1) begin
                priority_reg_tmp[i] = 32'h0;
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

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign external_irq_out = external_irq_comb;


endmodule

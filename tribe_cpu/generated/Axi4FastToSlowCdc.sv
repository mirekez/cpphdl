`default_nettype none

import Predef_pkg::*;


module Axi4FastToSlowCdc #(
    parameter ADDR_WIDTH = 32
,   parameter ID_WIDTH = 4
,   parameter DATA_WIDTH = 256
 )
 (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire fast_in__awvalid_in
,   output wire fast_in__awready_out
,   input wire[ADDR_WIDTH-1:0] fast_in__awaddr_in
,   input wire[ID_WIDTH-1:0] fast_in__awid_in
,   input wire fast_in__wvalid_in
,   output wire fast_in__wready_out
,   input wire[DATA_WIDTH-1:0] fast_in__wdata_in
,   input wire[unsigned'(64'(DATA_WIDTH))/64'h8-1:0] fast_in__wstrb_in
,   input wire fast_in__wlast_in
,   output wire fast_in__bvalid_out
,   input wire fast_in__bready_in
,   output wire[ID_WIDTH-1:0] fast_in__bid_out
,   input wire fast_in__arvalid_in
,   output wire fast_in__arready_out
,   input wire[ADDR_WIDTH-1:0] fast_in__araddr_in
,   input wire[ID_WIDTH-1:0] fast_in__arid_in
,   output wire fast_in__rvalid_out
,   input wire fast_in__rready_in
,   output wire[DATA_WIDTH-1:0] fast_in__rdata_out
,   output wire fast_in__rlast_out
,   output wire[ID_WIDTH-1:0] fast_in__rid_out
,   output wire slow_out__awvalid_out
,   input wire slow_out__awready_in
,   output wire[ADDR_WIDTH-1:0] slow_out__awaddr_out
,   output wire[ID_WIDTH-1:0] slow_out__awid_out
,   output wire slow_out__wvalid_out
,   input wire slow_out__wready_in
,   output wire[DATA_WIDTH-1:0] slow_out__wdata_out
,   output wire[unsigned'(64'(DATA_WIDTH))/64'h8-1:0] slow_out__wstrb_out
,   output wire slow_out__wlast_out
,   input wire slow_out__bvalid_in
,   output wire slow_out__bready_out
,   input wire[ID_WIDTH-1:0] slow_out__bid_in
,   output wire slow_out__arvalid_out
,   input wire slow_out__arready_in
,   output wire[ADDR_WIDTH-1:0] slow_out__araddr_out
,   output wire[ID_WIDTH-1:0] slow_out__arid_out
,   input wire slow_out__rvalid_in
,   output wire slow_out__rready_out
,   input wire[DATA_WIDTH-1:0] slow_out__rdata_in
,   input wire slow_out__rlast_in
,   input wire[ID_WIDTH-1:0] slow_out__rid_in
);


    // regs and combs
    reg[ADDR_WIDTH-1:0] aw_addr_fast_reg;
    reg[ID_WIDTH-1:0] aw_id_fast_reg;
    reg[DATA_WIDTH-1:0] w_data_fast_reg;
    reg[unsigned'(64'(DATA_WIDTH))/64'h8-1:0] w_strb_fast_reg;
    reg w_last_fast_reg;
    reg[ADDR_WIDTH-1:0] ar_addr_fast_reg;
    reg[ID_WIDTH-1:0] ar_id_fast_reg;
    reg aw_request_fast_reg;
    reg w_request_fast_reg;
    reg ar_request_fast_reg;
    (* ASYNC_REG = "TRUE" *)
    logic aw_ack_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic aw_ack_fast2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic w_ack_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic w_ack_fast2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic ar_ack_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic ar_ack_fast2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic b_request_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic b_request_fast2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic r_request_fast1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic r_request_fast2_reg;
    reg b_ack_fast_reg;
    reg r_ack_fast_reg;
    reg write_outstanding_fast_reg;
    reg read_outstanding_fast_reg;
    reg aw_seen_fast_reg;
    reg ar_seen_fast_reg;
    (* ASYNC_REG = "TRUE" *)
    logic aw_request_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic aw_request_slow2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic w_request_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic w_request_slow2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic ar_request_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic ar_request_slow2_reg;
    reg aw_ack_slow_reg;
    reg w_ack_slow_reg;
    reg ar_ack_slow_reg;
    reg[ID_WIDTH-1:0] b_id_slow_reg;
    reg[DATA_WIDTH-1:0] r_data_slow_reg;
    reg r_last_slow_reg;
    reg[ID_WIDTH-1:0] r_id_slow_reg;
    reg b_request_slow_reg;
    reg r_request_slow_reg;
    (* ASYNC_REG = "TRUE" *)
    logic b_ack_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic b_ack_slow2_reg;
    (* ASYNC_REG = "TRUE" *)
    logic r_ack_slow1_reg;
    (* ASYNC_REG = "TRUE" *)
    logic r_ack_slow2_reg;

    // members

    // tmp variables
    logic[ADDR_WIDTH-1:0] aw_addr_fast_reg_tmp;
    logic[ID_WIDTH-1:0] aw_id_fast_reg_tmp;
    logic[DATA_WIDTH-1:0] w_data_fast_reg_tmp;
    logic[unsigned'(64'(DATA_WIDTH))/64'h8-1:0] w_strb_fast_reg_tmp;
    logic w_last_fast_reg_tmp;
    logic[ADDR_WIDTH-1:0] ar_addr_fast_reg_tmp;
    logic[ID_WIDTH-1:0] ar_id_fast_reg_tmp;
    logic aw_request_fast_reg_tmp;
    logic w_request_fast_reg_tmp;
    logic ar_request_fast_reg_tmp;
    logic aw_ack_fast1_reg_tmp;
    logic aw_ack_fast2_reg_tmp;
    logic w_ack_fast1_reg_tmp;
    logic w_ack_fast2_reg_tmp;
    logic ar_ack_fast1_reg_tmp;
    logic ar_ack_fast2_reg_tmp;
    logic b_request_fast1_reg_tmp;
    logic b_request_fast2_reg_tmp;
    logic r_request_fast1_reg_tmp;
    logic r_request_fast2_reg_tmp;
    logic b_ack_fast_reg_tmp;
    logic r_ack_fast_reg_tmp;
    logic write_outstanding_fast_reg_tmp;
    logic read_outstanding_fast_reg_tmp;
    logic aw_seen_fast_reg_tmp;
    logic ar_seen_fast_reg_tmp;
    logic aw_request_slow1_reg_tmp;
    logic aw_request_slow2_reg_tmp;
    logic w_request_slow1_reg_tmp;
    logic w_request_slow2_reg_tmp;
    logic ar_request_slow1_reg_tmp;
    logic ar_request_slow2_reg_tmp;
    logic aw_ack_slow_reg_tmp;
    logic w_ack_slow_reg_tmp;
    logic ar_ack_slow_reg_tmp;
    logic[ID_WIDTH-1:0] b_id_slow_reg_tmp;
    logic[DATA_WIDTH-1:0] r_data_slow_reg_tmp;
    logic r_last_slow_reg_tmp;
    logic[ID_WIDTH-1:0] r_id_slow_reg_tmp;
    logic b_request_slow_reg_tmp;
    logic r_request_slow_reg_tmp;
    logic b_ack_slow1_reg_tmp;
    logic b_ack_slow2_reg_tmp;
    logic r_ack_slow1_reg_tmp;
    logic r_ack_slow2_reg_tmp;


    generate  // _assign
        assign fast_in__awready_out = (((!((write_outstanding_fast_reg) != '0) && (aw_request_fast_reg == aw_ack_fast2_reg)) && ((((!((aw_seen_fast_reg) != '0) || !fast_in__awvalid_in) || (fast_in__awaddr_in != aw_addr_fast_reg)) || (fast_in__awid_in != aw_id_fast_reg)))) != '0);
        assign fast_in__wready_out = ((w_request_fast_reg == w_ack_fast2_reg) != '0);
        assign fast_in__arready_out = (((!((read_outstanding_fast_reg) != '0) && (ar_request_fast_reg == ar_ack_fast2_reg)) && ((((!((ar_seen_fast_reg) != '0) || !fast_in__arvalid_in) || (fast_in__araddr_in != ar_addr_fast_reg)) || (fast_in__arid_in != ar_id_fast_reg)))) != '0);
        assign fast_in__bvalid_out = ((b_request_fast2_reg != b_ack_fast_reg) != '0);
        assign fast_in__bid_out = unsigned'(ID_WIDTH'(b_id_slow_reg));
        assign fast_in__rvalid_out = ((r_request_fast2_reg != r_ack_fast_reg) != '0);
        assign fast_in__rdata_out = unsigned'(DATA_WIDTH'(r_data_slow_reg));
        assign fast_in__rlast_out = ((r_last_slow_reg) != '0);
        assign fast_in__rid_out = unsigned'(ID_WIDTH'(r_id_slow_reg));
        assign slow_out__awvalid_out = ((aw_request_slow2_reg != aw_ack_slow_reg) != '0);
        assign slow_out__awaddr_out = unsigned'(ADDR_WIDTH'(aw_addr_fast_reg));
        assign slow_out__awid_out = unsigned'(ID_WIDTH'(aw_id_fast_reg));
        assign slow_out__wvalid_out = ((w_request_slow2_reg != w_ack_slow_reg) != '0);
        assign slow_out__wdata_out = unsigned'(DATA_WIDTH'(w_data_fast_reg));
        assign slow_out__wstrb_out = unsigned'((DATA_WIDTH/64'h8)'(w_strb_fast_reg));
        assign slow_out__wlast_out = ((w_last_fast_reg) != '0);
        assign slow_out__bready_out = ((b_request_slow_reg == b_ack_slow2_reg) != '0);
        assign slow_out__arvalid_out = ((ar_request_slow2_reg != ar_ack_slow_reg) != '0);
        assign slow_out__araddr_out = unsigned'(ADDR_WIDTH'(ar_addr_fast_reg));
        assign slow_out__arid_out = unsigned'(ID_WIDTH'(ar_id_fast_reg));
        assign slow_out__rready_out = ((r_request_slow_reg == r_ack_slow2_reg) != '0);
    endgenerate

    task work_clk_func (input logic reset);
    begin: work_clk_func
        aw_ack_fast1_reg_tmp = aw_ack_slow_reg;
        aw_ack_fast2_reg_tmp = aw_ack_fast1_reg;
        w_ack_fast1_reg_tmp = w_ack_slow_reg;
        w_ack_fast2_reg_tmp = w_ack_fast1_reg;
        ar_ack_fast1_reg_tmp = ar_ack_slow_reg;
        ar_ack_fast2_reg_tmp = ar_ack_fast1_reg;
        b_request_fast1_reg_tmp = b_request_slow_reg;
        b_request_fast2_reg_tmp = b_request_fast1_reg;
        r_request_fast1_reg_tmp = r_request_slow_reg;
        r_request_fast2_reg_tmp = r_request_fast1_reg;
        if (fast_in__awvalid_in && fast_in__awready_out) begin
            aw_addr_fast_reg_tmp = fast_in__awaddr_in;
            aw_id_fast_reg_tmp = fast_in__awid_in;
            aw_request_fast_reg_tmp = !((aw_request_fast_reg) != '0);
            write_outstanding_fast_reg_tmp = 1;
            aw_seen_fast_reg_tmp = 1;
        end
        if (fast_in__wvalid_in && fast_in__wready_out) begin
            w_data_fast_reg_tmp = fast_in__wdata_in;
            w_strb_fast_reg_tmp = fast_in__wstrb_in;
            w_last_fast_reg_tmp = fast_in__wlast_in;
            w_request_fast_reg_tmp = !((w_request_fast_reg) != '0);
        end
        if (fast_in__arvalid_in && fast_in__arready_out) begin
            ar_addr_fast_reg_tmp = fast_in__araddr_in;
            ar_id_fast_reg_tmp = fast_in__arid_in;
            ar_request_fast_reg_tmp = !((ar_request_fast_reg) != '0);
            read_outstanding_fast_reg_tmp = 1;
            ar_seen_fast_reg_tmp = 1;
        end
        if (fast_in__bvalid_out && fast_in__bready_in) begin
            b_ack_fast_reg_tmp = b_request_fast2_reg;
            write_outstanding_fast_reg_tmp = 0;
        end
        if (fast_in__rvalid_out && fast_in__rready_in) begin
            r_ack_fast_reg_tmp = r_request_fast2_reg;
            read_outstanding_fast_reg_tmp = 0;
        end
        if (!fast_in__awvalid_in) begin
            aw_seen_fast_reg_tmp = 0;
        end
        if (!fast_in__arvalid_in) begin
            ar_seen_fast_reg_tmp = 0;
        end
        if (reset) begin
            aw_addr_fast_reg_tmp = '0;
            aw_id_fast_reg_tmp = '0;
            w_data_fast_reg_tmp = '0;
            w_strb_fast_reg_tmp = '0;
            w_last_fast_reg_tmp = '0;
            ar_addr_fast_reg_tmp = '0;
            ar_id_fast_reg_tmp = '0;
            aw_request_fast_reg_tmp = '0;
            w_request_fast_reg_tmp = '0;
            ar_request_fast_reg_tmp = '0;
            aw_ack_fast1_reg_tmp = '0;
            aw_ack_fast2_reg_tmp = '0;
            w_ack_fast1_reg_tmp = '0;
            w_ack_fast2_reg_tmp = '0;
            ar_ack_fast1_reg_tmp = '0;
            ar_ack_fast2_reg_tmp = '0;
            b_request_fast1_reg_tmp = '0;
            b_request_fast2_reg_tmp = '0;
            r_request_fast1_reg_tmp = '0;
            r_request_fast2_reg_tmp = '0;
            b_ack_fast_reg_tmp = '0;
            r_ack_fast_reg_tmp = '0;
            write_outstanding_fast_reg_tmp = '0;
            read_outstanding_fast_reg_tmp = '0;
            aw_seen_fast_reg_tmp = '0;
            ar_seen_fast_reg_tmp = '0;
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        work_clk_func(reset);
    end
    endtask

    task _work_clk (input logic reset);
    begin: _work_clk
        work_clk_func(reset);
    end
    endtask

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
        aw_request_slow1_reg_tmp = aw_request_fast_reg;
        aw_request_slow2_reg_tmp = aw_request_slow1_reg;
        w_request_slow1_reg_tmp = w_request_fast_reg;
        w_request_slow2_reg_tmp = w_request_slow1_reg;
        ar_request_slow1_reg_tmp = ar_request_fast_reg;
        ar_request_slow2_reg_tmp = ar_request_slow1_reg;
        b_ack_slow1_reg_tmp = b_ack_fast_reg;
        b_ack_slow2_reg_tmp = b_ack_slow1_reg;
        r_ack_slow1_reg_tmp = r_ack_fast_reg;
        r_ack_slow2_reg_tmp = r_ack_slow1_reg;
        if (slow_out__awvalid_out && slow_out__awready_in) begin
            aw_ack_slow_reg_tmp = aw_request_slow2_reg;
        end
        if (slow_out__wvalid_out && slow_out__wready_in) begin
            w_ack_slow_reg_tmp = w_request_slow2_reg;
        end
        if (slow_out__arvalid_out && slow_out__arready_in) begin
            ar_ack_slow_reg_tmp = ar_request_slow2_reg;
        end
        if (slow_out__bvalid_in && slow_out__bready_out) begin
            b_id_slow_reg_tmp = slow_out__bid_in;
            b_request_slow_reg_tmp = !((b_request_slow_reg) != '0);
        end
        if (slow_out__rvalid_in && slow_out__rready_out) begin
            r_data_slow_reg_tmp = slow_out__rdata_in;
            r_last_slow_reg_tmp = slow_out__rlast_in;
            r_id_slow_reg_tmp = slow_out__rid_in;
            r_request_slow_reg_tmp = !((r_request_slow_reg) != '0);
        end
        if (reset) begin
            aw_request_slow1_reg_tmp = '0;
            aw_request_slow2_reg_tmp = '0;
            w_request_slow1_reg_tmp = '0;
            w_request_slow2_reg_tmp = '0;
            ar_request_slow1_reg_tmp = '0;
            ar_request_slow2_reg_tmp = '0;
            aw_ack_slow_reg_tmp = '0;
            w_ack_slow_reg_tmp = '0;
            ar_ack_slow_reg_tmp = '0;
            b_id_slow_reg_tmp = '0;
            r_data_slow_reg_tmp = '0;
            r_last_slow_reg_tmp = '0;
            r_id_slow_reg_tmp = '0;
            b_request_slow_reg_tmp = '0;
            r_request_slow_reg_tmp = '0;
            b_ack_slow1_reg_tmp = '0;
            b_ack_slow2_reg_tmp = '0;
            r_ack_slow1_reg_tmp = '0;
            r_ack_slow2_reg_tmp = '0;
        end
    end
    endtask

    always_ff @(posedge clk) begin
        aw_addr_fast_reg_tmp = aw_addr_fast_reg;
        aw_id_fast_reg_tmp = aw_id_fast_reg;
        w_data_fast_reg_tmp = w_data_fast_reg;
        w_strb_fast_reg_tmp = w_strb_fast_reg;
        w_last_fast_reg_tmp = w_last_fast_reg;
        ar_addr_fast_reg_tmp = ar_addr_fast_reg;
        ar_id_fast_reg_tmp = ar_id_fast_reg;
        aw_request_fast_reg_tmp = aw_request_fast_reg;
        w_request_fast_reg_tmp = w_request_fast_reg;
        ar_request_fast_reg_tmp = ar_request_fast_reg;
        aw_ack_fast1_reg_tmp = aw_ack_fast1_reg;
        aw_ack_fast2_reg_tmp = aw_ack_fast2_reg;
        w_ack_fast1_reg_tmp = w_ack_fast1_reg;
        w_ack_fast2_reg_tmp = w_ack_fast2_reg;
        ar_ack_fast1_reg_tmp = ar_ack_fast1_reg;
        ar_ack_fast2_reg_tmp = ar_ack_fast2_reg;
        b_request_fast1_reg_tmp = b_request_fast1_reg;
        b_request_fast2_reg_tmp = b_request_fast2_reg;
        r_request_fast1_reg_tmp = r_request_fast1_reg;
        r_request_fast2_reg_tmp = r_request_fast2_reg;
        b_ack_fast_reg_tmp = b_ack_fast_reg;
        r_ack_fast_reg_tmp = r_ack_fast_reg;
        write_outstanding_fast_reg_tmp = write_outstanding_fast_reg;
        read_outstanding_fast_reg_tmp = read_outstanding_fast_reg;
        aw_seen_fast_reg_tmp = aw_seen_fast_reg;
        ar_seen_fast_reg_tmp = ar_seen_fast_reg;

            _work_clk(reset);

        aw_addr_fast_reg <= aw_addr_fast_reg_tmp;
        aw_id_fast_reg <= aw_id_fast_reg_tmp;
        w_data_fast_reg <= w_data_fast_reg_tmp;
        w_strb_fast_reg <= w_strb_fast_reg_tmp;
        w_last_fast_reg <= w_last_fast_reg_tmp;
        ar_addr_fast_reg <= ar_addr_fast_reg_tmp;
        ar_id_fast_reg <= ar_id_fast_reg_tmp;
        aw_request_fast_reg <= aw_request_fast_reg_tmp;
        w_request_fast_reg <= w_request_fast_reg_tmp;
        ar_request_fast_reg <= ar_request_fast_reg_tmp;
        aw_ack_fast1_reg <= aw_ack_fast1_reg_tmp;
        aw_ack_fast2_reg <= aw_ack_fast2_reg_tmp;
        w_ack_fast1_reg <= w_ack_fast1_reg_tmp;
        w_ack_fast2_reg <= w_ack_fast2_reg_tmp;
        ar_ack_fast1_reg <= ar_ack_fast1_reg_tmp;
        ar_ack_fast2_reg <= ar_ack_fast2_reg_tmp;
        b_request_fast1_reg <= b_request_fast1_reg_tmp;
        b_request_fast2_reg <= b_request_fast2_reg_tmp;
        r_request_fast1_reg <= r_request_fast1_reg_tmp;
        r_request_fast2_reg <= r_request_fast2_reg_tmp;
        b_ack_fast_reg <= b_ack_fast_reg_tmp;
        r_ack_fast_reg <= r_ack_fast_reg_tmp;
        write_outstanding_fast_reg <= write_outstanding_fast_reg_tmp;
        read_outstanding_fast_reg <= read_outstanding_fast_reg_tmp;
        aw_seen_fast_reg <= aw_seen_fast_reg_tmp;
        ar_seen_fast_reg <= ar_seen_fast_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin
        aw_request_slow1_reg_tmp = aw_request_slow1_reg;
        aw_request_slow2_reg_tmp = aw_request_slow2_reg;
        w_request_slow1_reg_tmp = w_request_slow1_reg;
        w_request_slow2_reg_tmp = w_request_slow2_reg;
        ar_request_slow1_reg_tmp = ar_request_slow1_reg;
        ar_request_slow2_reg_tmp = ar_request_slow2_reg;
        aw_ack_slow_reg_tmp = aw_ack_slow_reg;
        w_ack_slow_reg_tmp = w_ack_slow_reg;
        ar_ack_slow_reg_tmp = ar_ack_slow_reg;
        b_id_slow_reg_tmp = b_id_slow_reg;
        r_data_slow_reg_tmp = r_data_slow_reg;
        r_last_slow_reg_tmp = r_last_slow_reg;
        r_id_slow_reg_tmp = r_id_slow_reg;
        b_request_slow_reg_tmp = b_request_slow_reg;
        r_request_slow_reg_tmp = r_request_slow_reg;
        b_ack_slow1_reg_tmp = b_ack_slow1_reg;
        b_ack_slow2_reg_tmp = b_ack_slow2_reg;
        r_ack_slow1_reg_tmp = r_ack_slow1_reg;
        r_ack_slow2_reg_tmp = r_ack_slow2_reg;

            _work_l2_clock(reset);

        aw_request_slow1_reg <= aw_request_slow1_reg_tmp;
        aw_request_slow2_reg <= aw_request_slow2_reg_tmp;
        w_request_slow1_reg <= w_request_slow1_reg_tmp;
        w_request_slow2_reg <= w_request_slow2_reg_tmp;
        ar_request_slow1_reg <= ar_request_slow1_reg_tmp;
        ar_request_slow2_reg <= ar_request_slow2_reg_tmp;
        aw_ack_slow_reg <= aw_ack_slow_reg_tmp;
        w_ack_slow_reg <= w_ack_slow_reg_tmp;
        ar_ack_slow_reg <= ar_ack_slow_reg_tmp;
        b_id_slow_reg <= b_id_slow_reg_tmp;
        r_data_slow_reg <= r_data_slow_reg_tmp;
        r_last_slow_reg <= r_last_slow_reg_tmp;
        r_id_slow_reg <= r_id_slow_reg_tmp;
        b_request_slow_reg <= b_request_slow_reg_tmp;
        r_request_slow_reg <= r_request_slow_reg_tmp;
        b_ack_slow1_reg <= b_ack_slow1_reg_tmp;
        b_ack_slow2_reg <= b_ack_slow2_reg_tmp;
        r_ack_slow1_reg <= r_ack_slow1_reg_tmp;
        r_ack_slow2_reg <= r_ack_slow2_reg_tmp;
    end


endmodule

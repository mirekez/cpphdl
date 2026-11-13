`default_nettype none

import Predef_pkg::*;


module InterruptController (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire[31:0] mstatus_in
,   input wire[31:0] mie_in
,   input wire[31:0] mideleg_in
,   input wire[31:0] mip_sw_in
,   input wire[2-1:0] priv_in
,   input wire clint_msip_in
,   input wire clint_mtip_in
,   input wire external_irq_in
,   output wire[31:0] mip_out
,   output wire interrupt_valid_out
,   output wire[31:0] interrupt_cause_out
,   output wire interrupt_to_supervisor_out
);
    localparam  MSTATUS_SIE = 'h2;
    localparam  MSTATUS_MIE = 'h8;
    localparam  PRIV_S = 'h1;
    localparam  PRIV_M = 'h3;
    localparam  IRQ_SSIP = 'h1;
    localparam  IRQ_MSIP = 'h3;
    localparam  IRQ_STIP = 'h5;
    localparam  IRQ_MTIP = 'h7;
    localparam  IRQ_SEIP = 'h9;
    localparam  IRQ_MEIP = 'hB;
    localparam  MIP_SOFTWARE_WRITABLE_MASK = 'h2;


    // regs and combs
    logic[31:0] mip_comb;
    logic[31:0] enabled_pending_comb;
    logic[31:0] interrupt_cause_comb;
    logic interrupt_to_supervisor_comb;
    logic interrupt_valid_comb;

    // members

    // tmp variables


    always_comb begin : mip_comb_func  // mip_comb_func
        mip_comb=mip_sw_in & MIP_SOFTWARE_WRITABLE_MASK;
        if (clint_msip_in) begin
            mip_comb|='h1 <<< (((priv_in == unsigned'(64'(PRIV_M)))) ? (IRQ_MSIP) : (IRQ_SSIP));
        end
        if (clint_mtip_in) begin
            mip_comb|='h1 <<< (((priv_in == unsigned'(64'(PRIV_M)))) ? (IRQ_MTIP) : (IRQ_STIP));
        end
        if (external_irq_in) begin
            mip_comb|='h1 <<< (((priv_in == unsigned'(64'(PRIV_M)))) ? (IRQ_MEIP) : (IRQ_SEIP));
        end
    end

    always_comb begin : enabled_pending_comb_func  // enabled_pending_comb_func
        enabled_pending_comb=mip_comb & mie_in;
    end

    always_comb begin : interrupt_cause_comb_func  // interrupt_cause_comb_func
        logic[31:0] pending;
        pending=enabled_pending_comb;
        interrupt_cause_comb=32'h0;
        if (((pending & (('h1 <<< IRQ_MEIP))) != '0)) begin
            interrupt_cause_comb=IRQ_MEIP;
        end
        else begin
            if ((((((pending & (('h1 <<< IRQ_STIP))))) != '0) && (priv_in != unsigned'(64'(PRIV_M)))) && ((((((mideleg_in >>> unsigned'(32'(IRQ_STIP)))) & 'h1))) != '0)) begin
                interrupt_cause_comb=IRQ_STIP;
            end
            else begin
                if (((pending & (('h1 <<< IRQ_MSIP))) != '0)) begin
                    interrupt_cause_comb=IRQ_MSIP;
                end
                else begin
                    if (((pending & (('h1 <<< IRQ_MTIP))) != '0)) begin
                        interrupt_cause_comb=IRQ_MTIP;
                    end
                    else begin
                        if (((pending & (('h1 <<< IRQ_SEIP))) != '0)) begin
                            interrupt_cause_comb=IRQ_SEIP;
                        end
                        else begin
                            if (((pending & (('h1 <<< IRQ_SSIP))) != '0)) begin
                                interrupt_cause_comb=IRQ_SSIP;
                            end
                            else begin
                                if (((pending & (('h1 <<< IRQ_STIP))) != '0)) begin
                                    interrupt_cause_comb=IRQ_STIP;
                                end
                            end
                        end
                    end
                end
            end
        end
    end

    always_comb begin : interrupt_to_supervisor_comb_func  // interrupt_to_supervisor_comb_func
        logic[31:0] cause;
        cause=interrupt_cause_comb;
        interrupt_to_supervisor_comb=0;
        if (((cause != 32'h0) && (priv_in != unsigned'(64'(PRIV_M)))) && ((((((mideleg_in >>> cause)) & 'h1))) != '0)) begin
            interrupt_to_supervisor_comb=1;
        end
    end

    always_comb begin : interrupt_valid_comb_func  // interrupt_valid_comb_func
        logic[31:0] cause;
        logic to_s;
        logic global_enable;
        cause=interrupt_cause_comb;
        to_s=interrupt_to_supervisor_comb;
        global_enable=0;
        if (cause != 32'h0) begin
            if (to_s) begin
                global_enable=(priv_in < unsigned'(64'(PRIV_S))) || ((((mstatus_in & MSTATUS_SIE))) != '0);
            end
            else begin
                global_enable=(priv_in < unsigned'(64'(PRIV_M))) || ((((mstatus_in & MSTATUS_MIE))) != '0);
            end
        end
        interrupt_valid_comb=(cause != 32'h0) && global_enable;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin

            _work(reset);

    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign mip_out = mip_comb;

    assign interrupt_valid_out = interrupt_valid_comb;

    assign interrupt_cause_out = interrupt_cause_comb;

    assign interrupt_to_supervisor_out = interrupt_to_supervisor_comb;


endmodule

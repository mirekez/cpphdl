`default_nettype none

import Predef_pkg::*;


module InterruptController (
    input wire clk
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
    parameter  MSTATUS_SIE = 'h1 <<< 'h1;
    parameter  MSTATUS_MIE = 'h1 <<< 'h3;
    parameter  PRIV_S = 'h1;
    parameter  PRIV_M = 'h3;
    parameter  IRQ_SSIP = 'h1;
    parameter  IRQ_MSIP = 'h3;
    parameter  IRQ_STIP = 'h5;
    parameter  IRQ_MTIP = 'h7;
    parameter  IRQ_SEIP = 'h9;
    parameter  IRQ_MEIP = 'hB;
    parameter  MIP_SOFTWARE_WRITABLE_MASK = 'h1 <<< IRQ_SSIP;


    // regs and combs
    logic[31:0] mip_comb;
;
    logic[31:0] enabled_pending_comb;
;
    logic[31:0] interrupt_cause_comb;
;
    logic interrupt_to_supervisor_comb;
;
    logic interrupt_valid_comb;
;

    // members

    // tmp variables


    always_comb begin : mip_comb_func  // mip_comb_func
        mip_comb=mip_sw_in & MIP_SOFTWARE_WRITABLE_MASK;
        if (clint_msip_in) begin
            mip_comb|='h1 <<< (((priv_in == PRIV_M)) ? (IRQ_MSIP) : (IRQ_SSIP));
        end
        if (clint_mtip_in) begin
            mip_comb|='h1 <<< (((priv_in == PRIV_M)) ? (IRQ_MTIP) : (IRQ_STIP));
        end
        if (external_irq_in) begin
            mip_comb|='h1 <<< (((priv_in == PRIV_M)) ? (IRQ_MEIP) : (IRQ_SEIP));
        end
    end

    always_comb begin : enabled_pending_comb_func  // enabled_pending_comb_func
        enabled_pending_comb=mip_comb & mie_in;
    end

    always_comb begin : interrupt_cause_comb_func  // interrupt_cause_comb_func
        logic[31:0] pending;
        pending=enabled_pending_comb;
        interrupt_cause_comb='h0;
        if (pending & (('h1 <<< IRQ_MEIP))) begin
            interrupt_cause_comb=IRQ_MEIP;
        end
        else begin
            if ((((pending & (('h1 <<< IRQ_STIP)))) && (priv_in != PRIV_M)) && ((((mideleg_in >>> IRQ_STIP)) & 'h1))) begin
                interrupt_cause_comb=IRQ_STIP;
            end
            else begin
                if (pending & (('h1 <<< IRQ_MSIP))) begin
                    interrupt_cause_comb=IRQ_MSIP;
                end
                else begin
                    if (pending & (('h1 <<< IRQ_MTIP))) begin
                        interrupt_cause_comb=IRQ_MTIP;
                    end
                    else begin
                        if (pending & (('h1 <<< IRQ_SEIP))) begin
                            interrupt_cause_comb=IRQ_SEIP;
                        end
                        else begin
                            if (pending & (('h1 <<< IRQ_SSIP))) begin
                                interrupt_cause_comb=IRQ_SSIP;
                            end
                            else begin
                                if (pending & (('h1 <<< IRQ_STIP))) begin
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
        if (((cause != 'h0) && (priv_in != PRIV_M)) && ((((mideleg_in >>> cause)) & 'h1))) begin
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
        if (cause != 'h0) begin
            if (to_s) begin
                global_enable=(priv_in < PRIV_S) || ((mstatus_in & MSTATUS_SIE));
            end
            else begin
                global_enable=(priv_in < PRIV_M) || ((mstatus_in & MSTATUS_MIE));
            end
        end
        interrupt_valid_comb=(cause != 'h0) && global_enable;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end

    assign mip_out = mip_comb;

    assign interrupt_valid_out = interrupt_valid_comb;

    assign interrupt_cause_out = interrupt_cause_comb;

    assign interrupt_to_supervisor_out = interrupt_to_supervisor_comb;


endmodule

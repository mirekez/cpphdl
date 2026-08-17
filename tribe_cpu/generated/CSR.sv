`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Sys_pkg::*;
import Trap_pkg::*;
import Csr_pkg::*;


module CSR (
    input wire clk
,   input wire reset
,   input State state_in
,   input State trap_check_state_in
,   input wire[2-1:0] reset_priv_in
,   input wire interrupt_valid_in
,   input wire[31:0] interrupt_cause_in
,   input wire interrupt_to_supervisor_in
,   input wire[31:0] irq_pending_bits_in
,   output wire[31:0] read_data_out
,   output wire[31:0] trap_vector_out
,   output wire[31:0] epc_out
,   output wire[31:0] mepc_out
,   output wire[31:0] mtvec_out
,   output wire[31:0] mcause_out
,   output wire[31:0] mtval_out
,   output wire[31:0] sepc_out
,   output wire[31:0] stvec_out
,   output wire[31:0] scause_out
,   output wire[31:0] stval_out
,   output wire illegal_trap_out
,   output wire[31:0] mstatus_out
,   output wire[31:0] mie_out
,   output wire[31:0] mideleg_out
,   output wire[31:0] mip_sw_out
,   output wire[31:0] satp_out
,   output wire[2-1:0] priv_out
);
    parameter  MISA_RV32IMC = ((('h40000000 | (('h1 <<< ((73 - 65))))) | (('h1 <<< ((77 - 65))))) | (('h1 <<< ((67 - 65))))) | (('h1 <<< ((65 - 65))));
    parameter  PRIV_U = 'h0;
    parameter  PRIV_S = 'h1;
    parameter  PRIV_M = 'h3;
    parameter  MSTATUS_UIE = 'h1 <<< 'h0;
    parameter  MSTATUS_SIE = 'h1 <<< 'h1;
    parameter  MSTATUS_MIE = 'h1 <<< 'h3;
    parameter  MSTATUS_UPIE = 'h1 <<< 'h4;
    parameter  MSTATUS_SPIE = 'h1 <<< 'h5;
    parameter  MSTATUS_MPIE = 'h1 <<< 'h7;
    parameter  MSTATUS_SPP = 'h1 <<< 'h8;
    parameter  MSTATUS_MPP_SHIFT = 'hB;
    parameter  MSTATUS_MPP_MASK = 'h3 <<< MSTATUS_MPP_SHIFT;
    parameter  MSTATUS_WRITABLE = (((((((((((MSTATUS_UIE | MSTATUS_SIE) | MSTATUS_MIE) | MSTATUS_UPIE) | MSTATUS_SPIE) | MSTATUS_MPIE) | MSTATUS_SPP) | MSTATUS_MPP_MASK) | (('h3 <<< 'hD))) | (('h3 <<< 'hF))) | (('h3 <<< 'h11))) | (('h1 <<< 'h12))) | (('h1 <<< 'h13));
    parameter  SSTATUS_MASK = ((((((MSTATUS_UIE | MSTATUS_SIE) | MSTATUS_UPIE) | MSTATUS_SPIE) | MSTATUS_SPP) | (('h3 <<< 'hD))) | (('h3 <<< 'hF))) | (('h1 <<< 'h12));
    parameter  IRQ_SSIP = 'h1 <<< 'h1;
    parameter  IRQ_STIP = 'h1 <<< 'h5;
    parameter  IRQ_SEIP = 'h1 <<< 'h9;
    parameter  XIP_VISIBLE_MASK = (IRQ_SSIP | IRQ_STIP) | IRQ_SEIP;
    parameter  XIP_SOFTWARE_WRITABLE_MASK = IRQ_SSIP;


    // regs and combs
    reg[32-1:0] mstatus_reg;
    reg[32-1:0] mtvec_reg;
    reg[32-1:0] medeleg_reg;
    reg[32-1:0] mideleg_reg;
    reg[32-1:0] mie_reg;
    reg[32-1:0] mscratch_reg;
    reg[32-1:0] mepc_reg;
    reg[32-1:0] mcause_reg;
    reg[32-1:0] mtval_reg;
    reg[32-1:0] mip_reg;
    reg[32-1:0] mcounteren_reg;
    reg[32-1:0] mcountinhibit_reg;
    reg[32-1:0] mscratchcsw_reg;
    reg[32-1:0] mscratchcswl_reg;
    reg[32-1:0] sstatus_reg;
    reg[32-1:0] stvec_reg;
    reg[32-1:0] sie_reg;
    reg[32-1:0] sscratch_reg;
    reg[32-1:0] sepc_reg;
    reg[32-1:0] scause_reg;
    reg[32-1:0] stval_reg;
    reg[32-1:0] sip_reg;
    reg[32-1:0] scounteren_reg;
    reg[32-1:0] satp_reg;
    reg[32-1:0] dcsr_reg;
    reg[32-1:0] dpc_reg;
    reg[32-1:0] dscratch0_reg;
    reg[32-1:0] dscratch1_reg;
    reg[64-1:0] cycle_reg;
    reg[64-1:0] instret_reg;
    reg[2-1:0] priv_reg;
    logic[31:0] interrupt_enable_comb;
;
    logic[31:0] trap_vector_comb;
;
    logic[31:0] epc_comb;
;
    logic illegal_trap_comb;
;
    logic[31:0] read_data_comb;
;

    // members

    // tmp variables
    logic[32-1:0] mstatus_reg_tmp;
    logic[32-1:0] mtvec_reg_tmp;
    logic[32-1:0] medeleg_reg_tmp;
    logic[32-1:0] mideleg_reg_tmp;
    logic[32-1:0] mie_reg_tmp;
    logic[32-1:0] mscratch_reg_tmp;
    logic[32-1:0] mepc_reg_tmp;
    logic[32-1:0] mcause_reg_tmp;
    logic[32-1:0] mtval_reg_tmp;
    logic[32-1:0] mip_reg_tmp;
    logic[32-1:0] mcounteren_reg_tmp;
    logic[32-1:0] mcountinhibit_reg_tmp;
    logic[32-1:0] mscratchcsw_reg_tmp;
    logic[32-1:0] mscratchcswl_reg_tmp;
    logic[32-1:0] sstatus_reg_tmp;
    logic[32-1:0] stvec_reg_tmp;
    logic[32-1:0] sie_reg_tmp;
    logic[32-1:0] sscratch_reg_tmp;
    logic[32-1:0] sepc_reg_tmp;
    logic[32-1:0] scause_reg_tmp;
    logic[32-1:0] stval_reg_tmp;
    logic[32-1:0] sip_reg_tmp;
    logic[32-1:0] scounteren_reg_tmp;
    logic[32-1:0] satp_reg_tmp;
    logic[32-1:0] dcsr_reg_tmp;
    logic[32-1:0] dpc_reg_tmp;
    logic[32-1:0] dscratch0_reg_tmp;
    logic[32-1:0] dscratch1_reg_tmp;
    logic[64-1:0] cycle_reg_tmp;
    logic[64-1:0] instret_reg_tmp;
    logic[2-1:0] priv_reg_tmp;


    function logic[31:0] csr_read (input logic[31:0] addr);
        logic[63:0] cycle_value;
        logic[63:0] instret_value;
        cycle_value=cycle_reg;
        instret_value=instret_reg;
        if (addr == 'h1) begin
            return 'h0;
        end
        if (addr == 'h2) begin
            return 'h0;
        end
        if (addr == 'h3) begin
            return 'h0;
        end
        if ((addr == 'hC00) || (addr == 'hB00)) begin
            return unsigned'(32'(cycle_value));
        end
        if ((addr == 'hC80) || (addr == 'hB80)) begin
            return unsigned'(32'((cycle_value >>> 'h20)));
        end
        if (addr == 'hC01) begin
            return unsigned'(32'(cycle_value));
        end
        if (addr == 'hC81) begin
            return unsigned'(32'((cycle_value >>> 'h20)));
        end
        if ((addr == 'hC02) || (addr == 'hB02)) begin
            return unsigned'(32'(instret_value));
        end
        if ((addr == 'hC82) || (addr == 'hB82)) begin
            return unsigned'(32'((instret_value >>> 'h20)));
        end
        if (((((addr>='hC03 && addr<='hC1F)) || ((addr>='hC83 && addr<='hC9F))) || ((addr>='hB03 && addr<='hB1F))) || ((addr>='hB83 && addr<='hB9F))) begin
            return 'h0;
        end
        if (addr == 'h100) begin
            return mstatus_reg & SSTATUS_MASK;
        end
        if (addr == 'h104) begin
            return sie_reg;
        end
        if (addr == 'h105) begin
            return stvec_reg;
        end
        if (addr == 'h106) begin
            return scounteren_reg;
        end
        if (addr == 'h140) begin
            return sscratch_reg;
        end
        if (addr == 'h141) begin
            return sepc_reg;
        end
        if (addr == 'h142) begin
            return scause_reg;
        end
        if (addr == 'h143) begin
            return stval_reg;
        end
        if (addr == 'h144) begin
            return ((((sip_reg & XIP_SOFTWARE_WRITABLE_MASK)) | irq_pending_bits_in)) & XIP_VISIBLE_MASK;
        end
        if (addr == 'h180) begin
            return satp_reg;
        end
        if (addr == 'h300) begin
            return mstatus_reg;
        end
        if (addr == 'h301) begin
            return MISA_RV32IMC;
        end
        if (addr == 'h302) begin
            return medeleg_reg;
        end
        if (addr == 'h303) begin
            return mideleg_reg;
        end
        if (addr == 'h304) begin
            return mie_reg;
        end
        if (addr == 'h305) begin
            return mtvec_reg;
        end
        if (addr == 'h306) begin
            return mcounteren_reg;
        end
        if (addr == 'h310) begin
            return mstatus_reg >>> 'h20;
        end
        if (addr == 'h320) begin
            return mcountinhibit_reg;
        end
        if (addr == 'h340) begin
            return mscratch_reg;
        end
        if (addr == 'h341) begin
            return mepc_reg;
        end
        if (addr == 'h342) begin
            return mcause_reg;
        end
        if (addr == 'h343) begin
            return mtval_reg;
        end
        if (addr == 'h344) begin
            return ((mip_reg & XIP_SOFTWARE_WRITABLE_MASK)) | irq_pending_bits_in;
        end
        if (addr == 'h348) begin
            return mscratchcsw_reg;
        end
        if (addr == 'h349) begin
            return mscratchcswl_reg;
        end
        if (addr == 'hF11) begin
            return 'h0;
        end
        if (addr == 'hF12) begin
            return 'h0;
        end
        if (addr == 'hF13) begin
            return 'h0;
        end
        if (addr == 'hF14) begin
            return 'h0;
        end
        if (addr == 'hF15) begin
            return 'h0;
        end
        if (addr == 'h7B0) begin
            return dcsr_reg;
        end
        if (addr == 'h7B1) begin
            return dpc_reg;
        end
        if (addr == 'h7B2) begin
            return dscratch0_reg;
        end
        if (addr == 'h7B3) begin
            return dscratch1_reg;
        end
        if (((((((addr>='h323 && addr<='h33F)) || ((addr>='h3A0 && addr<='h3EF))) || ((addr>='h5A8 && addr<='h5AF))) || ((addr>='h7C0 && addr<='h7FF))) || ((addr>='h9C0 && addr<='h9FF))) || ((addr>='hA00 && addr<='hAFF))) begin
            return 'h0;
        end
        return 'h0;
    endfunction

    always_comb begin : read_data_comb_func  // read_data_comb_func
        read_data_comb=csr_read(state_in.csr_addr);
    end

    function logic[31:0] trap_cause_code ();
        if (interrupt_valid_in) begin
            return interrupt_cause_in;
        end
        if (state_in.sys_op == Sys_pkg::ECALL) begin
            if (priv_reg == PRIV_U) begin
                return 'h8;
            end
            if (priv_reg == PRIV_S) begin
                return 'h9;
            end
            return 'hB;
        end
        if (state_in.sys_op == Sys_pkg::EBREAK) begin
            return 'h3;
        end
        case (state_in.trap_op)
        Trap_pkg::TNONE: begin
            return 'h2;
        end
        Trap_pkg::INST_MISALIGNED: begin
            return 'h0;
        end
        Trap_pkg::ILLEGAL_INST: begin
            return 'h2;
        end
        Trap_pkg::BREAKPOINT: begin
            return 'h3;
        end
        Trap_pkg::LOAD_MISALIGNED: begin
            return 'h4;
        end
        Trap_pkg::STORE_MISALIGNED: begin
            return 'h6;
        end
        Trap_pkg::INST_PAGE_FAULT: begin
            return 'hC;
        end
        Trap_pkg::LOAD_PAGE_FAULT: begin
            return 'hD;
        end
        Trap_pkg::STORE_PAGE_FAULT: begin
            return 'hF;
        end
        Trap_pkg::ECALL_U: begin
            return 'h8;
        end
        Trap_pkg::ECALL_S: begin
            return 'h9;
        end
        Trap_pkg::ECALL_M: begin
            return 'hB;
        end
        default: begin
        end
        endcase
        return 'h2;
    endfunction

    function logic trap_to_supervisor (input logic[31:0] cause);
        if (interrupt_valid_in) begin
            return interrupt_to_supervisor_in;
        end
        return (priv_reg != PRIV_M) && ((((medeleg_reg >>> cause)) & 'h1));
    endfunction

    always_comb begin : trap_vector_comb_func  // trap_vector_comb_func
        logic[31:0] cause;
        logic[31:0] tvec;
        cause=trap_cause_code();
        tvec=(trap_to_supervisor(cause)) ? (unsigned'(32'(stvec_reg))) : (unsigned'(32'(mtvec_reg)));
        trap_vector_comb=tvec & ~'h3;
        if (interrupt_valid_in && ((((tvec & 'h3)) == 'h1))) begin
            trap_vector_comb=((tvec & ~'h3)) + (cause*'h4);
        end
    end

    always_comb begin : epc_comb_func  // epc_comb_func
        epc_comb=mepc_reg;
        if (state_in.sys_op == Sys_pkg::SRET) begin
            epc_comb=sepc_reg;
        end
    end

    function logic csr_state_writes (input State st);
        logic[31:0] op; op = st.csr_op;
        if (!st.valid || (op == Csr_pkg::CNONE)) begin
            return 0;
        end
        if ((op == Csr_pkg::CSRRS) || (op == Csr_pkg::CSRRC)) begin
            return st.rs1 != 'h0;
        end
        if ((op == Csr_pkg::CSRRSI) || (op == Csr_pkg::CSRRCI)) begin
            return st.csr_imm != 'h0;
        end
        return 1;
    endfunction

    function logic csr_supported (input logic[31:0] addr);
        if (((addr == 'h1) || (addr == 'h2)) || (addr == 'h3)) begin
            return 1;
        end
        if ((((((((((addr == 'hC00) || (addr == 'hC80)) || (addr == 'hC01)) || (addr == 'hC81)) || (addr == 'hC02)) || (addr == 'hC82)) || (addr == 'hB00)) || (addr == 'hB80)) || (addr == 'hB02)) || (addr == 'hB82)) begin
            return 1;
        end
        if (((((addr>='hC03 && addr<='hC1F)) || ((addr>='hC83 && addr<='hC9F))) || ((addr>='hB03 && addr<='hB1F))) || ((addr>='hB83 && addr<='hB9F))) begin
            return 1;
        end
        if ((((addr>='h100 && addr<='h106)) || ((addr>='h140 && addr<='h144))) || (addr == 'h180)) begin
            return 1;
        end
        if (((((((((addr>='h300 && addr<='h306)) || (addr == 'h310)) || (addr == 'h320)) || ((addr>='h340 && addr<='h344))) || (addr == 'h348)) || (addr == 'h349)) || ((addr>='h323 && addr<='h33F))) || ((addr>='h3A0 && addr<='h3EF))) begin
            return 1;
        end
        if (addr>='hF11 && addr<='hF15) begin
            return 1;
        end
        if (addr>='h7B0 && addr<='h7B3) begin
            return 1;
        end
        if (((((addr>='h5A8 && addr<='h5AF)) || ((addr>='h7C0 && addr<='h7FF))) || ((addr>='h9C0 && addr<='h9FF))) || ((addr>='hA00 && addr<='hAFF))) begin
            return 1;
        end
        return 0;
    endfunction

    function logic state_causes_illegal_trap (input State st);
        logic[31:0] addr;
        logic[31:0] csr_priv;
        logic[31:0] index;
        if (!st.valid) begin
            return 0;
        end
        if (st.sys_op == Sys_pkg::MRET) begin
            return priv_reg != PRIV_M;
        end
        if (st.sys_op == Sys_pkg::SRET) begin
            return priv_reg < PRIV_S;
        end
        if (st.sys_op == Sys_pkg::SFENCE_VMA) begin
            return priv_reg < PRIV_S;
        end
        if (st.csr_op == Csr_pkg::CNONE) begin
            return 0;
        end
        addr=st.csr_addr;
        csr_priv=((addr >>> 'h8)) & 'h3;
        if (csr_priv > priv_reg) begin
            return 1;
        end
        if ((((((addr >>> 'hA)) & 'h3)) == 'h3) && csr_state_writes(st)) begin
            return 1;
        end
        if (!csr_supported(addr)) begin
            return 1;
        end
        if (((priv_reg != PRIV_M) && addr>='hC00) && addr<='hC9F) begin
            index=addr & 'h1F;
            if (((((mcounteren_reg >>> index)) & 'h1)) == 'h0) begin
                return 1;
            end
        end
        return 0;
    endfunction

    always_comb begin : illegal_trap_comb_func  // illegal_trap_comb_func
        illegal_trap_comb=state_causes_illegal_trap(trap_check_state_in);
    end

    always_comb begin : interrupt_enable_comb_func  // interrupt_enable_comb_func
        interrupt_enable_comb=unsigned'(32'(mie_reg)) | unsigned'(32'(sie_reg));
    end

    function logic[31:0] sanitize_mstatus (input logic[31:0] value);
        return value & MSTATUS_WRITABLE;
    endfunction

    function logic[31:0] csr_write_value (input logic[31:0] old_value);
        logic[31:0] mask; mask = state_in.rs1_val;
        if (((state_in.csr_op == Csr_pkg::CSRRWI) || (state_in.csr_op == Csr_pkg::CSRRSI)) || (state_in.csr_op == Csr_pkg::CSRRCI)) begin
            mask=state_in.csr_imm;
        end
        if ((state_in.csr_op == Csr_pkg::CSRRW) || (state_in.csr_op == Csr_pkg::CSRRWI)) begin
            return mask;
        end
        if ((state_in.csr_op == Csr_pkg::CSRRS) || (state_in.csr_op == Csr_pkg::CSRRSI)) begin
            return old_value | mask;
        end
        if ((state_in.csr_op == Csr_pkg::CSRRC) || (state_in.csr_op == Csr_pkg::CSRRCI)) begin
            return old_value & ~mask;
        end
        return old_value;
    endfunction

    function logic sync_trap ();
        return state_in.valid && ((((((interrupt_valid_in || (state_in.sys_op == Sys_pkg::ECALL)) || (state_in.sys_op == Sys_pkg::EBREAK)) || (state_in.sys_op == Sys_pkg::TRAP)) || (state_in.trap_op != Trap_pkg::TNONE)) || state_causes_illegal_trap(state_in)));
    endfunction

    function logic csr_writes ();
        if (state_causes_illegal_trap(state_in)) begin
            return 0;
        end
        return csr_state_writes(state_in);
    endfunction

    task csr_write (
        input logic[31:0] addr
,       input logic[31:0] value
    );
    begin: csr_write
        case (addr)
        'h100: begin
            mstatus_reg_tmp = unsigned'(32'(((mstatus_reg & ~SSTATUS_MASK)) | ((value & SSTATUS_MASK))));
            sstatus_reg_tmp = unsigned'(32'(value & SSTATUS_MASK));
        end
        'h104: begin
            sie_reg_tmp = unsigned'(32'(value));
        end
        'h105: begin
            stvec_reg_tmp = unsigned'(32'(value));
        end
        'h106: begin
            scounteren_reg_tmp = unsigned'(32'(value));
        end
        'h140: begin
            sscratch_reg_tmp = unsigned'(32'(value));
        end
        'h141: begin
            sepc_reg_tmp = unsigned'(32'(value & ~'h1));
        end
        'h142: begin
            scause_reg_tmp = unsigned'(32'(value));
        end
        'h143: begin
            stval_reg_tmp = unsigned'(32'(value));
        end
        'h144: begin
            sip_reg_tmp = unsigned'(32'(value & XIP_SOFTWARE_WRITABLE_MASK));
        end
        'h180: begin
            satp_reg_tmp = unsigned'(32'(value & 'h803FFFFF));
        end
        'h300: begin
            mstatus_reg_tmp = unsigned'(32'(sanitize_mstatus(value)));
            sstatus_reg_tmp = unsigned'(32'(value & SSTATUS_MASK));
        end
        'h302: begin
            medeleg_reg_tmp = unsigned'(32'(value));
        end
        'h303: begin
            mideleg_reg_tmp = unsigned'(32'(value));
        end
        'h304: begin
            mie_reg_tmp = unsigned'(32'(value));
        end
        'h305: begin
            mtvec_reg_tmp = unsigned'(32'(value));
        end
        'h306: begin
            mcounteren_reg_tmp = unsigned'(32'(value));
        end
        'h320: begin
            mcountinhibit_reg_tmp = unsigned'(32'(value));
        end
        'h340: begin
            mscratch_reg_tmp = unsigned'(32'(value));
        end
        'h341: begin
            mepc_reg_tmp = unsigned'(32'(value & ~'h1));
        end
        'h342: begin
            mcause_reg_tmp = unsigned'(32'(value));
        end
        'h343: begin
            mtval_reg_tmp = unsigned'(32'(value));
        end
        'h344: begin
            mip_reg_tmp = unsigned'(32'(value & XIP_SOFTWARE_WRITABLE_MASK));
        end
        'h348: begin
            mscratchcsw_reg_tmp = unsigned'(32'(value));
        end
        'h349: begin
            mscratchcswl_reg_tmp = unsigned'(32'(value));
        end
        'hB00: begin
            cycle_reg_tmp = unsigned'(64'(((unsigned'(64'(unsigned'(32'((unsigned'(64'(cycle_reg)) >>> 'h20))))) <<< 'h20)) | value));
        end
        'hB80: begin
            cycle_reg_tmp = unsigned'(64'(((unsigned'(64'(value)) <<< 'h20)) | unsigned'(32'(cycle_reg))));
        end
        'hB02: begin
            instret_reg_tmp = unsigned'(64'(((unsigned'(64'(unsigned'(32'((unsigned'(64'(instret_reg)) >>> 'h20))))) <<< 'h20)) | value));
        end
        'hB82: begin
            instret_reg_tmp = unsigned'(64'(((unsigned'(64'(value)) <<< 'h20)) | unsigned'(32'(instret_reg))));
        end
        'h7B0: begin
            dcsr_reg_tmp = unsigned'(32'(value));
        end
        'h7B1: begin
            dpc_reg_tmp = unsigned'(32'(value & ~'h1));
        end
        'h7B2: begin
            dscratch0_reg_tmp = unsigned'(32'(value));
        end
        'h7B3: begin
            dscratch1_reg_tmp = unsigned'(32'(value));
        end
        endcase
    end
    endtask

    task _work (input logic reset);
    begin: _work
        logic inhibit_cycle;
        logic inhibit_instret;
        logic[31:0] cause;
        logic[31:0] tval;
        logic is_interrupt;
        logic to_s;
        logic trace_csr_events;
        inhibit_cycle=mcountinhibit_reg & 'h1;
        inhibit_instret=((mcountinhibit_reg >>> 'h2)) & 'h1;
        trace_csr_events=0;
        cycle_reg_tmp = unsigned'(64'((inhibit_cycle) ? (cycle_reg) : (unsigned'(64'(cycle_reg)) + 'h1)));
        instret_reg_tmp = unsigned'(64'(((inhibit_instret || !state_in.valid)) ? (instret_reg) : (unsigned'(64'(instret_reg)) + 'h1)));
        if (csr_writes()) begin
            if (trace_csr_events && ((((state_in.csr_addr == 'h100) || (state_in.csr_addr == 'h141)) || (state_in.csr_addr == 'h180)))) begin
                $write("trace-csr-write pc=%08x addr=%03x old=%08x new=%08x priv=%x\n", state_in.pc, unsigned'(32'(state_in.csr_addr)), read_data_comb, csr_write_value(read_data_comb), unsigned'(32'(priv_reg)));
            end
            csr_write(state_in.csr_addr, csr_write_value(read_data_comb));
        end
        if (sync_trap()) begin
            cause=trap_cause_code();
            is_interrupt=0;
            is_interrupt=interrupt_valid_in;
            tval=((!is_interrupt && (((((cause == 'h2) || (cause == 'hC)) || (cause == 'hD)) || (cause == 'hF))))) ? (state_in.imm) : ('h0);
            to_s=trap_to_supervisor(cause);
            if (to_s) begin
                if (trace_csr_events) begin
                    $write("trace-trap-to-s pc=%08x cause=%x tval=%08x priv=%x stvec=%08x mstatus=%08x\n", state_in.pc, cause, tval, unsigned'(32'(priv_reg)), unsigned'(32'(stvec_reg)), unsigned'(32'(mstatus_reg)));
                end
                sepc_reg_tmp = unsigned'(32'(state_in.pc & ~'h1));
                scause_reg_tmp = unsigned'(32'((is_interrupt) ? ((cause | 'h80000000)) : (cause)));
                stval_reg_tmp = unsigned'(32'(tval));
                mstatus_reg_tmp = unsigned'(32'((((mstatus_reg & ~(((MSTATUS_SPIE | MSTATUS_SIE) | MSTATUS_SPP)))) | ((((mstatus_reg & MSTATUS_SIE))) ? (MSTATUS_SPIE) : ('h0))) | ((((priv_reg == PRIV_S))) ? (MSTATUS_SPP) : ('h0))));
                priv_reg_tmp = PRIV_S;
            end
            else begin
                mepc_reg_tmp = unsigned'(32'(state_in.pc & ~'h1));
                mcause_reg_tmp = unsigned'(32'((is_interrupt) ? ((cause | 'h80000000)) : (cause)));
                mtval_reg_tmp = unsigned'(32'(tval));
                mstatus_reg_tmp = unsigned'(32'((((mstatus_reg & ~(((MSTATUS_MPIE | MSTATUS_MIE) | MSTATUS_MPP_MASK)))) | ((((mstatus_reg & MSTATUS_MIE))) ? (MSTATUS_MPIE) : ('h0))) | ((unsigned'(32'(priv_reg)) <<< MSTATUS_MPP_SHIFT))));
                priv_reg_tmp = PRIV_M;
            end
        end
        if (state_in.valid && (state_in.sys_op == Sys_pkg::MRET)) begin
            logic[31:0] mpp;
            logic[31:0] mie_restore;
            mpp=((mstatus_reg & MSTATUS_MPP_MASK)) >>> MSTATUS_MPP_SHIFT;
            mie_restore=((mstatus_reg & MSTATUS_MPIE)) ? (MSTATUS_MIE) : ('h0);
            priv_reg_tmp = mpp;
            mstatus_reg_tmp = unsigned'(32'((((((mstatus_reg & ~MSTATUS_MIE)) | mie_restore) | MSTATUS_MPIE)) & ~MSTATUS_MPP_MASK));
        end
        if (state_in.valid && (state_in.sys_op == Sys_pkg::SRET)) begin
            logic[31:0] spp;
            logic[31:0] sie_restore;
            spp=((mstatus_reg & MSTATUS_SPP)) ? (PRIV_S) : (PRIV_U);
            sie_restore=((mstatus_reg & MSTATUS_SPIE)) ? (MSTATUS_SIE) : ('h0);
            if (trace_csr_events) begin
                $write("trace-sret pc=%08x sepc=%08x mstatus=%08x next_priv=%x\n", state_in.pc, unsigned'(32'(sepc_reg)), unsigned'(32'(mstatus_reg)), spp);
            end
            priv_reg_tmp = spp;
            mstatus_reg_tmp = unsigned'(32'((((((mstatus_reg & ~MSTATUS_SIE)) | sie_restore) | MSTATUS_SPIE)) & ~MSTATUS_SPP));
        end
        if (reset) begin
            mstatus_reg_tmp = '0;
            mtvec_reg_tmp = '0;
            if (reset_priv_in == PRIV_S) begin
                medeleg_reg_tmp = unsigned'(32'(((((((((((('h1 <<< 'h0)) | (('h1 <<< 'h1))) | (('h1 <<< 'h3))) | (('h1 <<< 'h4))) | (('h1 <<< 'h5))) | (('h1 <<< 'h6))) | (('h1 <<< 'h7))) | (('h1 <<< 'h8))) | (('h1 <<< 'hC))) | (('h1 <<< 'hD))) | (('h1 <<< 'hF))));
                mideleg_reg_tmp = unsigned'(32'(((('h1 <<< 'h1)) | (('h1 <<< 'h5))) | (('h1 <<< 'h9))));
                mcounteren_reg_tmp = unsigned'(32'('hFFFFFFFF));
            end
            else begin
                medeleg_reg_tmp = '0;
                mideleg_reg_tmp = '0;
                mcounteren_reg_tmp = '0;
            end
            mie_reg_tmp = '0;
            mscratch_reg_tmp = '0;
            mepc_reg_tmp = '0;
            mcause_reg_tmp = '0;
            mtval_reg_tmp = '0;
            mip_reg_tmp = '0;
            mcountinhibit_reg_tmp = '0;
            mscratchcsw_reg_tmp = '0;
            mscratchcswl_reg_tmp = '0;
            sstatus_reg_tmp = '0;
            stvec_reg_tmp = '0;
            sie_reg_tmp = '0;
            sscratch_reg_tmp = '0;
            sepc_reg_tmp = '0;
            scause_reg_tmp = '0;
            stval_reg_tmp = '0;
            sip_reg_tmp = '0;
            scounteren_reg_tmp = '0;
            satp_reg_tmp = '0;
            dcsr_reg_tmp = '0;
            dpc_reg_tmp = '0;
            dscratch0_reg_tmp = '0;
            dscratch1_reg_tmp = '0;
            cycle_reg_tmp = '0;
            instret_reg_tmp = '0;
            priv_reg_tmp = reset_priv_in;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        mstatus_reg_tmp = mstatus_reg;
        mtvec_reg_tmp = mtvec_reg;
        medeleg_reg_tmp = medeleg_reg;
        mideleg_reg_tmp = mideleg_reg;
        mie_reg_tmp = mie_reg;
        mscratch_reg_tmp = mscratch_reg;
        mepc_reg_tmp = mepc_reg;
        mcause_reg_tmp = mcause_reg;
        mtval_reg_tmp = mtval_reg;
        mip_reg_tmp = mip_reg;
        mcounteren_reg_tmp = mcounteren_reg;
        mcountinhibit_reg_tmp = mcountinhibit_reg;
        mscratchcsw_reg_tmp = mscratchcsw_reg;
        mscratchcswl_reg_tmp = mscratchcswl_reg;
        sstatus_reg_tmp = sstatus_reg;
        stvec_reg_tmp = stvec_reg;
        sie_reg_tmp = sie_reg;
        sscratch_reg_tmp = sscratch_reg;
        sepc_reg_tmp = sepc_reg;
        scause_reg_tmp = scause_reg;
        stval_reg_tmp = stval_reg;
        sip_reg_tmp = sip_reg;
        scounteren_reg_tmp = scounteren_reg;
        satp_reg_tmp = satp_reg;
        dcsr_reg_tmp = dcsr_reg;
        dpc_reg_tmp = dpc_reg;
        dscratch0_reg_tmp = dscratch0_reg;
        dscratch1_reg_tmp = dscratch1_reg;
        cycle_reg_tmp = cycle_reg;
        instret_reg_tmp = instret_reg;
        priv_reg_tmp = priv_reg;

        _work(reset);

        mstatus_reg <= mstatus_reg_tmp;
        mtvec_reg <= mtvec_reg_tmp;
        medeleg_reg <= medeleg_reg_tmp;
        mideleg_reg <= mideleg_reg_tmp;
        mie_reg <= mie_reg_tmp;
        mscratch_reg <= mscratch_reg_tmp;
        mepc_reg <= mepc_reg_tmp;
        mcause_reg <= mcause_reg_tmp;
        mtval_reg <= mtval_reg_tmp;
        mip_reg <= mip_reg_tmp;
        mcounteren_reg <= mcounteren_reg_tmp;
        mcountinhibit_reg <= mcountinhibit_reg_tmp;
        mscratchcsw_reg <= mscratchcsw_reg_tmp;
        mscratchcswl_reg <= mscratchcswl_reg_tmp;
        sstatus_reg <= sstatus_reg_tmp;
        stvec_reg <= stvec_reg_tmp;
        sie_reg <= sie_reg_tmp;
        sscratch_reg <= sscratch_reg_tmp;
        sepc_reg <= sepc_reg_tmp;
        scause_reg <= scause_reg_tmp;
        stval_reg <= stval_reg_tmp;
        sip_reg <= sip_reg_tmp;
        scounteren_reg <= scounteren_reg_tmp;
        satp_reg <= satp_reg_tmp;
        dcsr_reg <= dcsr_reg_tmp;
        dpc_reg <= dpc_reg_tmp;
        dscratch0_reg <= dscratch0_reg_tmp;
        dscratch1_reg <= dscratch1_reg_tmp;
        cycle_reg <= cycle_reg_tmp;
        instret_reg <= instret_reg_tmp;
        priv_reg <= priv_reg_tmp;
    end

    assign read_data_out = read_data_comb;

    assign trap_vector_out = trap_vector_comb;

    assign epc_out = epc_comb;

    assign mepc_out = mepc_reg;

    assign mtvec_out = mtvec_reg;

    assign mcause_out = mcause_reg;

    assign mtval_out = mtval_reg;

    assign sepc_out = sepc_reg;

    assign stvec_out = stvec_reg;

    assign scause_out = scause_reg;

    assign stval_out = stval_reg;

    assign illegal_trap_out = illegal_trap_comb;

    assign mstatus_out = mstatus_reg;

    assign mie_out = interrupt_enable_comb;

    assign mideleg_out = mideleg_reg;

    assign mip_sw_out = mip_reg;

    assign satp_out = satp_reg;

    assign priv_out = priv_reg;


endmodule

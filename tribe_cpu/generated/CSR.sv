`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Sys_pkg::*;
import Csr_pkg::*;
import Trap_pkg::*;


module CSR (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire State state_in
,   input wire commit_in
,   input wire State read_state_in
,   input wire State trap_check_state_in
,   input wire State legality_state_in
,   output wire legality_out
,   input wire State redirect_state_in
,   input wire[2-1:0] reset_priv_in
,   input wire[31:0] hartid_in
,   input wire interrupt_valid_in
,   input wire[31:0] interrupt_cause_in
,   input wire interrupt_to_supervisor_in
,   input wire redirect_interrupt_valid_in
,   input wire[31:0] redirect_interrupt_cause_in
,   input wire redirect_interrupt_to_supervisor_in
,   input wire[31:0] irq_pending_bits_in
,   input wire software_irq_set_in
,   output wire write_pending_out
,   output wire[31:0] read_data_out
,   input wire[31:0] time_lo_in
,   input wire[31:0] time_hi_in
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
,   output wire[31:0] medeleg_out
,   output wire[31:0] mip_sw_out
,   output wire[31:0] satp_out
,   output wire[2-1:0] priv_out
);
    localparam  MISA_RV32IMC = 'h40001105;
    localparam  PRIV_U = 'h0;
    localparam  PRIV_S = 'h1;
    localparam  PRIV_M = 'h3;
    localparam  MSTATUS_UIE = 'h1;
    localparam  MSTATUS_SIE = 'h2;
    localparam  MSTATUS_MIE = 'h8;
    localparam  MSTATUS_UPIE = 'h10;
    localparam  MSTATUS_SPIE = 'h20;
    localparam  MSTATUS_MPIE = 'h80;
    localparam  MSTATUS_SPP = 'h100;
    localparam  MSTATUS_MPP_SHIFT = 'hB;
    localparam  MSTATUS_MPP_MASK = 'h1800;
    localparam  MSTATUS_WRITABLE = 'hFF9BB;
    localparam  SSTATUS_MASK = 'h5E133;
    localparam  IRQ_SSIP = 'h2;
    localparam  IRQ_STIP = 'h20;
    localparam  IRQ_SEIP = 'h200;
    localparam  XIP_VISIBLE_MASK = 'h222;
    localparam  XIP_SOFTWARE_WRITABLE_MASK = 'h2;


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
    reg[32-1:0] csr_write_select_reg;
    reg[32-1:0] csr_write_value_reg;
    reg[32-1:0] csr_read_data_reg;
    reg[2-1:0] priv_reg;
    logic commit_new_comb;
    logic write_pending_comb;
    logic[31:0] software_pending_comb;
    logic[31:0] interrupt_enable_comb;
    logic[31:0] trap_vector_comb;
    logic[31:0] epc_comb;
    logic illegal_trap_comb;
    logic legality_comb;
    logic[31:0] commit_read_data_comb;
    logic[31:0] execute_read_data_comb;

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
    logic[32-1:0] csr_write_select_reg_tmp;
    logic[32-1:0] csr_write_value_reg_tmp;
    logic[32-1:0] csr_read_data_reg_tmp;
    logic[2-1:0] priv_reg_tmp;


    function logic csr_state_writes (input State st);
        logic[31:0] op;
        op = 32'(st.csr_op);
        if (!((st.valid) != '0) || (op == unsigned'(32'(Csr_pkg::CNONE)))) begin
            return 0;
        end
        if ((op == unsigned'(32'(Csr_pkg::CSRRS))) || (op == unsigned'(32'(Csr_pkg::CSRRC)))) begin
            return signed'(32'(st.rs1)) != 32'sh0;
        end
        if ((op == unsigned'(32'(Csr_pkg::CSRRSI))) || (op == unsigned'(32'(Csr_pkg::CSRRCI)))) begin
            return signed'(32'(st.csr_imm)) != 32'sh0;
        end
        return 1;
    endfunction

    function logic csr_supported (input logic[31:0] addr);
        if (((addr == 32'h1) || (addr == 32'h2)) || (addr == 32'h3)) begin
            return 1;
        end
        if ((((((((((addr == 32'hC00) || (addr == 32'hC80)) || (addr == 32'hC01)) || (addr == 32'hC81)) || (addr == 32'hC02)) || (addr == 32'hC82)) || (addr == 32'hB00)) || (addr == 32'hB80)) || (addr == 32'hB02)) || (addr == 32'hB82)) begin
            return 1;
        end
        if (((((addr>=32'hC03 && addr<=32'hC1F)) || ((addr>=32'hC83 && addr<=32'hC9F))) || ((addr>=32'hB03 && addr<=32'hB1F))) || ((addr>=32'hB83 && addr<=32'hB9F))) begin
            return 1;
        end
        if ((((addr>=32'h100 && addr<=32'h106)) || ((addr>=32'h140 && addr<=32'h144))) || (addr == 32'h180)) begin
            return 1;
        end
        if (((((((((addr>=32'h300 && addr<=32'h306)) || (addr == 32'h310)) || (addr == 32'h320)) || ((addr>=32'h340 && addr<=32'h344))) || (addr == 32'h348)) || (addr == 32'h349)) || ((addr>=32'h323 && addr<=32'h33F))) || ((addr>=32'h3A0 && addr<=32'h3EF))) begin
            return 1;
        end
        if (addr>=32'hF11 && addr<=32'hF15) begin
            return 1;
        end
        if (addr>=32'h7B0 && addr<=32'h7B3) begin
            return 1;
        end
        if (((((addr>=32'h5A8 && addr<=32'h5AF)) || ((addr>=32'h7C0 && addr<=32'h7FF))) || ((addr>=32'h9C0 && addr<=32'h9FF))) || ((addr>=32'hA00 && addr<=32'hAFF))) begin
            return 1;
        end
        return 0;
    endfunction

    function logic state_causes_illegal_trap (input State st);
        logic[31:0] addr;
        logic[31:0] csr_priv;
        logic[31:0] index;
        if (!((st.valid) != '0)) begin
            return 0;
        end
        if (signed'(32'(st.sys_op)) == signed'(32'(Sys_pkg::MRET))) begin
            return priv_reg != unsigned'(64'(PRIV_M));
        end
        if (signed'(32'(st.sys_op)) == signed'(32'(Sys_pkg::SRET))) begin
            return priv_reg < unsigned'(64'(PRIV_S));
        end
        if (signed'(32'(st.sys_op)) == signed'(32'(Sys_pkg::SFENCE_VMA))) begin
            return priv_reg < unsigned'(64'(PRIV_S));
        end
        if (signed'(32'(st.csr_op)) == signed'(32'(Csr_pkg::CNONE))) begin
            return 0;
        end
        addr=st.csr_addr;
        csr_priv=((addr >>> 32'sh8)) & 'h3;
        if (64'(csr_priv) > priv_reg) begin
            return 1;
        end
        if ((((((addr >>> 32'shA)) & 'h3)) == 'h3) && csr_state_writes(st)) begin
            return 1;
        end
        if (!csr_supported(addr)) begin
            return 1;
        end
        if (((priv_reg != unsigned'(64'(PRIV_M))) && addr>=32'hC00) && addr<=32'hC9F) begin
            index=addr & 'h1F;
            if (((((mcounteren_reg >>> index)) & 64'h1)) == 64'h0) begin
                return 1;
            end
        end
        return 0;
    endfunction

    always_comb begin : legality_comb_func  // legality_comb_func
        legality_comb=state_causes_illegal_trap(legality_state_in);
    end

    always_comb begin : write_pending_comb_func  // write_pending_comb_func
        write_pending_comb=csr_write_select_reg != 64'h0;
    end

    function logic[31:0] csr_read (input logic[31:0] addr);
        logic[63:0] cycle_value;
        logic[63:0] time_value;
        logic[63:0] instret_value;
        cycle_value=cycle_reg;
        time_value=((unsigned'(64'(time_hi_in)) <<< 'h20)) | unsigned'(64'(time_lo_in));
        instret_value=instret_reg;
        if (addr == 32'h1) begin
            return 32'h0;
        end
        if (addr == 32'h2) begin
            return 32'h0;
        end
        if (addr == 32'h3) begin
            return 32'h0;
        end
        if ((addr == 32'hC00) || (addr == 32'hB00)) begin
            return 32'(cycle_value);
        end
        if ((addr == 32'hC80) || (addr == 32'hB80)) begin
            return 32'((cycle_value >>> 32'sh20));
        end
        if (addr == 32'hC01) begin
            return 32'(time_value);
        end
        if (addr == 32'hC81) begin
            return 32'((time_value >>> 32'sh20));
        end
        if ((addr == 32'hC02) || (addr == 32'hB02)) begin
            return 32'(instret_value);
        end
        if ((addr == 32'hC82) || (addr == 32'hB82)) begin
            return 32'((instret_value >>> 32'sh20));
        end
        if (((((addr>=32'hC03 && addr<=32'hC1F)) || ((addr>=32'hC83 && addr<=32'hC9F))) || ((addr>=32'hB03 && addr<=32'hB1F))) || ((addr>=32'hB83 && addr<=32'hB9F))) begin
            return 32'h0;
        end
        if (addr == 32'h100) begin
            return 32'(mstatus_reg & unsigned'(64'(SSTATUS_MASK)));
        end
        if (addr == 32'h104) begin
            return unsigned'(32'(sie_reg));
        end
        if (addr == 32'h105) begin
            return unsigned'(32'(stvec_reg));
        end
        if (addr == 32'h106) begin
            return unsigned'(32'(scounteren_reg));
        end
        if (addr == 32'h140) begin
            return unsigned'(32'(sscratch_reg));
        end
        if (addr == 32'h141) begin
            return unsigned'(32'(sepc_reg));
        end
        if (addr == 32'h142) begin
            return unsigned'(32'(scause_reg));
        end
        if (addr == 32'h143) begin
            return unsigned'(32'(stval_reg));
        end
        if (addr == 32'h144) begin
            return 32'(((((sip_reg & unsigned'(64'(XIP_SOFTWARE_WRITABLE_MASK)))) | unsigned'(64'(irq_pending_bits_in)))) & unsigned'(64'(XIP_VISIBLE_MASK)));
        end
        if (addr == 32'h180) begin
            return unsigned'(32'(satp_reg));
        end
        if (addr == 32'h300) begin
            return unsigned'(32'(mstatus_reg));
        end
        if (addr == 32'h301) begin
            return MISA_RV32IMC;
        end
        if (addr == 32'h302) begin
            return unsigned'(32'(medeleg_reg));
        end
        if (addr == 32'h303) begin
            return unsigned'(32'(mideleg_reg));
        end
        if (addr == 32'h304) begin
            return unsigned'(32'(mie_reg));
        end
        if (addr == 32'h305) begin
            return unsigned'(32'(mtvec_reg));
        end
        if (addr == 32'h306) begin
            return unsigned'(32'(mcounteren_reg));
        end
        if (addr == 32'h310) begin
            return unsigned'(32'(mstatus_reg >>> 32'sh20));
        end
        if (addr == 32'h320) begin
            return unsigned'(32'(mcountinhibit_reg));
        end
        if (addr == 32'h340) begin
            return unsigned'(32'(mscratch_reg));
        end
        if (addr == 32'h341) begin
            return unsigned'(32'(mepc_reg));
        end
        if (addr == 32'h342) begin
            return unsigned'(32'(mcause_reg));
        end
        if (addr == 32'h343) begin
            return unsigned'(32'(mtval_reg));
        end
        if (addr == 32'h344) begin
            return 32'(((mip_reg & unsigned'(64'(XIP_SOFTWARE_WRITABLE_MASK)))) | unsigned'(64'(irq_pending_bits_in)));
        end
        if (addr == 32'h348) begin
            return unsigned'(32'(mscratchcsw_reg));
        end
        if (addr == 32'h349) begin
            return unsigned'(32'(mscratchcswl_reg));
        end
        if (addr == 32'hF11) begin
            return 32'h0;
        end
        if (addr == 32'hF12) begin
            return 32'h0;
        end
        if (addr == 32'hF13) begin
            return 32'h0;
        end
        if (addr == 32'hF14) begin
            return hartid_in;
        end
        if (addr == 32'hF15) begin
            return 32'h0;
        end
        if (addr == 32'h7B0) begin
            return unsigned'(32'(dcsr_reg));
        end
        if (addr == 32'h7B1) begin
            return unsigned'(32'(dpc_reg));
        end
        if (addr == 32'h7B2) begin
            return unsigned'(32'(dscratch0_reg));
        end
        if (addr == 32'h7B3) begin
            return unsigned'(32'(dscratch1_reg));
        end
        if (((((((addr>=32'h323 && addr<=32'h33F)) || ((addr>=32'h3A0 && addr<=32'h3EF))) || ((addr>=32'h5A8 && addr<=32'h5AF))) || ((addr>=32'h7C0 && addr<=32'h7FF))) || ((addr>=32'h9C0 && addr<=32'h9FF))) || ((addr>=32'hA00 && addr<=32'hAFF))) begin
            return 32'h0;
        end
        return 32'h0;
    endfunction

    always_comb begin : execute_read_data_comb_func  // execute_read_data_comb_func
        logic matching_commit;
        matching_commit=((((((state_in.valid) != '0) && ((read_state_in.valid) != '0)) && (signed'(32'(state_in.csr_op)) != signed'(32'(Csr_pkg::CNONE)))) && (signed'(32'(read_state_in.csr_op)) != signed'(32'(Csr_pkg::CNONE)))) && (state_in.pc == read_state_in.pc)) && (signed'(32'(state_in.csr_addr)) == signed'(32'(read_state_in.csr_addr)));
        execute_read_data_comb=(matching_commit) ? (unsigned'(32'(csr_read_data_reg))) : (csr_read(32'(read_state_in.csr_addr)));
    end

    function logic[31:0] redirect_trap_cause_code (input State state);
        if (redirect_interrupt_valid_in) begin
            return redirect_interrupt_cause_in;
        end
        if (signed'(32'(state.sys_op)) == signed'(32'(Sys_pkg::ECALL))) begin
            if (priv_reg == unsigned'(64'(PRIV_U))) begin
                return 32'h8;
            end
            if (priv_reg == unsigned'(64'(PRIV_S))) begin
                return 32'h9;
            end
            return 32'hB;
        end
        if (signed'(32'(state.sys_op)) == signed'(32'(Sys_pkg::EBREAK))) begin
            return 32'h3;
        end
        case (signed'(32'(state.trap_op)))
        signed'(32'(Trap_pkg::TNONE)): begin
            return 32'h2;
        end
        signed'(32'(Trap_pkg::INST_MISALIGNED)): begin
            return 32'h0;
        end
        signed'(32'(Trap_pkg::ILLEGAL_INST)): begin
            return 32'h2;
        end
        signed'(32'(Trap_pkg::BREAKPOINT)): begin
            return 32'h3;
        end
        signed'(32'(Trap_pkg::LOAD_MISALIGNED)): begin
            return 32'h4;
        end
        signed'(32'(Trap_pkg::STORE_MISALIGNED)): begin
            return 32'h6;
        end
        signed'(32'(Trap_pkg::INST_PAGE_FAULT)): begin
            return 32'hC;
        end
        signed'(32'(Trap_pkg::LOAD_PAGE_FAULT)): begin
            return 32'hD;
        end
        signed'(32'(Trap_pkg::STORE_PAGE_FAULT)): begin
            return 32'hF;
        end
        signed'(32'(Trap_pkg::ECALL_U)): begin
            return 32'h8;
        end
        signed'(32'(Trap_pkg::ECALL_S)): begin
            return 32'h9;
        end
        signed'(32'(Trap_pkg::ECALL_M)): begin
            return 32'hB;
        end
        default: begin
            return 32'h2;
        end
        endcase
        return 32'h2;
    endfunction

    function logic redirect_trap_to_supervisor (input logic[31:0] cause);
        if (redirect_interrupt_valid_in) begin
            return redirect_interrupt_to_supervisor_in;
        end
        return (priv_reg != unsigned'(64'(PRIV_M))) && ((((((medeleg_reg >>> cause)) & 64'h1))) != '0);
    endfunction

    always_comb begin : trap_vector_comb_func  // trap_vector_comb_func
        logic[31:0] cause;
        logic[31:0] tvec;
        cause=redirect_trap_cause_code(redirect_state_in);
        tvec=(redirect_trap_to_supervisor(cause)) ? (unsigned'(32'(stvec_reg))) : (unsigned'(32'(mtvec_reg)));
        trap_vector_comb=tvec & ~'h3;
        if (redirect_interrupt_valid_in && ((((tvec & 'h3)) == 'h1))) begin
            trap_vector_comb=((tvec & ~'h3)) + (cause*'h4);
        end
    end

    always_comb begin : epc_comb_func  // epc_comb_func
        epc_comb=mepc_reg;
        if (signed'(32'(redirect_state_in.sys_op)) == signed'(32'(Sys_pkg::SRET))) begin
            epc_comb=sepc_reg;
        end
    end

    always_comb begin : illegal_trap_comb_func  // illegal_trap_comb_func
        illegal_trap_comb=((trap_check_state_in.csr_illegal) != '0);
    end

    always_comb begin : interrupt_enable_comb_func  // interrupt_enable_comb_func
        interrupt_enable_comb=unsigned'(32'(mie_reg)) | unsigned'(32'(sie_reg));
    end

    always_comb begin : software_pending_comb_func  // software_pending_comb_func
        software_pending_comb=unsigned'(32'(mip_reg)) | unsigned'(32'(sip_reg));
    end

    always_comb begin : commit_new_comb_func  // commit_new_comb_func
        commit_new_comb=((state_in.valid) != '0) && commit_in;
    end

    function logic[31:0] sanitize_mstatus (input logic[31:0] value);
        return value & MSTATUS_WRITABLE;
    endfunction

    function logic[31:0] trap_cause_code (input State state);
        if (interrupt_valid_in) begin
            return interrupt_cause_in;
        end
        if (signed'(32'(state.sys_op)) == signed'(32'(Sys_pkg::ECALL))) begin
            if (priv_reg == unsigned'(64'(PRIV_U))) begin
                return 32'h8;
            end
            if (priv_reg == unsigned'(64'(PRIV_S))) begin
                return 32'h9;
            end
            return 32'hB;
        end
        if (signed'(32'(state.sys_op)) == signed'(32'(Sys_pkg::EBREAK))) begin
            return 32'h3;
        end
        case (signed'(32'(state.trap_op)))
        signed'(32'(Trap_pkg::TNONE)): begin
            return 32'h2;
        end
        signed'(32'(Trap_pkg::INST_MISALIGNED)): begin
            return 32'h0;
        end
        signed'(32'(Trap_pkg::ILLEGAL_INST)): begin
            return 32'h2;
        end
        signed'(32'(Trap_pkg::BREAKPOINT)): begin
            return 32'h3;
        end
        signed'(32'(Trap_pkg::LOAD_MISALIGNED)): begin
            return 32'h4;
        end
        signed'(32'(Trap_pkg::STORE_MISALIGNED)): begin
            return 32'h6;
        end
        signed'(32'(Trap_pkg::INST_PAGE_FAULT)): begin
            return 32'hC;
        end
        signed'(32'(Trap_pkg::LOAD_PAGE_FAULT)): begin
            return 32'hD;
        end
        signed'(32'(Trap_pkg::STORE_PAGE_FAULT)): begin
            return 32'hF;
        end
        signed'(32'(Trap_pkg::ECALL_U)): begin
            return 32'h8;
        end
        signed'(32'(Trap_pkg::ECALL_S)): begin
            return 32'h9;
        end
        signed'(32'(Trap_pkg::ECALL_M)): begin
            return 32'hB;
        end
        default: begin
            return 32'h2;
        end
        endcase
        return 32'h2;
    endfunction

    function logic trap_to_supervisor (input logic[31:0] cause);
        if (interrupt_valid_in) begin
            return interrupt_to_supervisor_in;
        end
        return (priv_reg != unsigned'(64'(PRIV_M))) && ((((((medeleg_reg >>> cause)) & 64'h1))) != '0);
    endfunction

    function logic[31:0] csr_write_value (input logic[31:0] old_value);
        logic[31:0] mask;
        mask = state_in.rs1_val;
        if (((signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRWI))) || (signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRSI)))) || (signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRCI)))) begin
            mask=state_in.csr_imm;
        end
        if ((signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRW))) || (signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRWI)))) begin
            return mask;
        end
        if ((signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRS))) || (signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRSI)))) begin
            return old_value | mask;
        end
        if ((signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRC))) || (signed'(32'(state_in.csr_op)) == signed'(32'(Csr_pkg::CSRRCI)))) begin
            return old_value & ~mask;
        end
        return old_value;
    endfunction

    function logic sync_trap ();
        return commit_new_comb && (((((interrupt_valid_in || (signed'(32'(state_in.sys_op)) == signed'(32'(Sys_pkg::ECALL)))) || (signed'(32'(state_in.sys_op)) == signed'(32'(Sys_pkg::EBREAK)))) || (signed'(32'(state_in.sys_op)) == signed'(32'(Sys_pkg::TRAP)))) || (signed'(32'(state_in.trap_op)) != signed'(32'(Trap_pkg::TNONE)))));
    endfunction

    function logic csr_writes ();
        return commit_new_comb && csr_state_writes(state_in);
    endfunction

    function logic[31:0] csr_write_select (input logic[31:0] addr);
        case (addr)
        32'h100: begin
            return 'h1 <<< 'h0;
        end
        32'h104: begin
            return 'h1 <<< 'h1;
        end
        32'h105: begin
            return 'h1 <<< 'h2;
        end
        32'h106: begin
            return 'h1 <<< 'h3;
        end
        32'h140: begin
            return 'h1 <<< 'h4;
        end
        32'h141: begin
            return 'h1 <<< 'h5;
        end
        32'h142: begin
            return 'h1 <<< 'h6;
        end
        32'h143: begin
            return 'h1 <<< 'h7;
        end
        32'h144: begin
            return 'h1 <<< 'h8;
        end
        32'h180: begin
            return 'h1 <<< 'h9;
        end
        32'h300: begin
            return 'h1 <<< 'hA;
        end
        32'h302: begin
            return 'h1 <<< 'hB;
        end
        32'h303: begin
            return 'h1 <<< 'hC;
        end
        32'h304: begin
            return 'h1 <<< 'hD;
        end
        32'h305: begin
            return 'h1 <<< 'hE;
        end
        32'h306: begin
            return 'h1 <<< 'hF;
        end
        32'h320: begin
            return 'h1 <<< 'h10;
        end
        32'h340: begin
            return 'h1 <<< 'h11;
        end
        32'h341: begin
            return 'h1 <<< 'h12;
        end
        32'h342: begin
            return 'h1 <<< 'h13;
        end
        32'h343: begin
            return 'h1 <<< 'h14;
        end
        32'h344: begin
            return 'h1 <<< 'h15;
        end
        32'h348: begin
            return 'h1 <<< 'h16;
        end
        32'h349: begin
            return 'h1 <<< 'h17;
        end
        32'hB00: begin
            return 'h1 <<< 'h18;
        end
        32'hB80: begin
            return 'h1 <<< 'h19;
        end
        32'hB02: begin
            return 'h1 <<< 'h1A;
        end
        32'hB82: begin
            return 'h1 <<< 'h1B;
        end
        32'h7B0: begin
            return 'h1 <<< 'h1C;
        end
        32'h7B1: begin
            return 'h1 <<< 'h1D;
        end
        32'h7B2: begin
            return 'h1 <<< 'h1E;
        end
        32'h7B3: begin
            return 'h1 <<< 'h1F;
        end
        endcase
        return 32'h0;
    endfunction

    task csr_write_selected (
        input logic[31:0] select
,       input logic[31:0] value
    );
    begin: csr_write_selected
        if (((select & (('h1 <<< 'h0))) != '0)) begin
            mstatus_reg_tmp = ((mstatus_reg & 64'(unsigned'(32'(~SSTATUS_MASK))))) | 64'(32'(((value & SSTATUS_MASK))));
            sstatus_reg_tmp = 32'(value & SSTATUS_MASK);
        end
        if (((select & (('h1 <<< 'h1))) != '0)) begin
            sie_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h2))) != '0)) begin
            stvec_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h3))) != '0)) begin
            scounteren_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h4))) != '0)) begin
            sscratch_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h5))) != '0)) begin
            sepc_reg_tmp = 32'(value & ~'h1);
        end
        if (((select & (('h1 <<< 'h6))) != '0)) begin
            scause_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h7))) != '0)) begin
            stval_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h8))) != '0)) begin
            sip_reg_tmp = 32'(value & XIP_SOFTWARE_WRITABLE_MASK);
        end
        if (((select & (('h1 <<< 'h9))) != '0)) begin
            satp_reg_tmp = 32'(value & 'h803FFFFF);
        end
        if (((select & (('h1 <<< 'hA))) != '0)) begin
            mstatus_reg_tmp = sanitize_mstatus(value);
            sstatus_reg_tmp = 32'(value & SSTATUS_MASK);
        end
        if (((select & (('h1 <<< 'hB))) != '0)) begin
            medeleg_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'hC))) != '0)) begin
            mideleg_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'hD))) != '0)) begin
            mie_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'hE))) != '0)) begin
            mtvec_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'hF))) != '0)) begin
            mcounteren_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h10))) != '0)) begin
            mcountinhibit_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h11))) != '0)) begin
            mscratch_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h12))) != '0)) begin
            mepc_reg_tmp = 32'(value & ~'h1);
        end
        if (((select & (('h1 <<< 'h13))) != '0)) begin
            mcause_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h14))) != '0)) begin
            mtval_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h15))) != '0)) begin
            mip_reg_tmp = 32'(value & XIP_SOFTWARE_WRITABLE_MASK);
        end
        if (((select & (('h1 <<< 'h16))) != '0)) begin
            mscratchcsw_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h17))) != '0)) begin
            mscratchcswl_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h18))) != '0)) begin
            cycle_reg_tmp = 64'(((64'(unsigned'(32'((unsigned'(64'(cycle_reg)) >>> 32'sh20)))) <<< 'h20)) | 64'(value));
        end
        if (((select & (('h1 <<< 'h19))) != '0)) begin
            cycle_reg_tmp = 64'(((64'(value) <<< 'h20)) | 64'(unsigned'(32'(cycle_reg))));
        end
        if (((select & (('h1 <<< 'h1A))) != '0)) begin
            instret_reg_tmp = 64'(((64'(unsigned'(32'((unsigned'(64'(instret_reg)) >>> 32'sh20)))) <<< 'h20)) | 64'(value));
        end
        if (((select & (('h1 <<< 'h1B))) != '0)) begin
            instret_reg_tmp = 64'(((64'(value) <<< 'h20)) | 64'(unsigned'(32'(instret_reg))));
        end
        if (((select & (('h1 <<< 'h1C))) != '0)) begin
            dcsr_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h1D))) != '0)) begin
            dpc_reg_tmp = 32'(value & ~'h1);
        end
        if (((select & (('h1 <<< 'h1E))) != '0)) begin
            dscratch0_reg_tmp = value;
        end
        if (((select & (('h1 <<< 'h1F))) != '0)) begin
            dscratch1_reg_tmp = value;
        end
    end
    endtask

    always_comb begin : commit_read_data_comb_func  // commit_read_data_comb_func
        commit_read_data_comb=csr_read(32'(state_in.csr_addr));
    end

    task _work (input logic reset);
    begin: _work
        logic inhibit_cycle;
        logic inhibit_instret;
        logic[31:0] cause;
        logic[31:0] tval;
        logic is_interrupt;
        logic to_s;
        logic trace_csr_events;
        inhibit_cycle=((mcountinhibit_reg & 64'h1) != '0);
        inhibit_instret=((((mcountinhibit_reg >>> 32'sh2)) & 64'h1) != '0);
        trace_csr_events=0;
        cycle_reg_tmp = unsigned'(64'((inhibit_cycle) ? (cycle_reg) : (unsigned'(64'(cycle_reg)) + 64'h1)));
        instret_reg_tmp = unsigned'(64'(((inhibit_instret || !commit_new_comb)) ? (instret_reg) : (unsigned'(64'(instret_reg)) + 64'h1)));
        csr_write_select_reg_tmp = 32'h0;
        csr_write_value_reg_tmp = csr_write_value_reg;
        if (csr_write_select_reg != 64'h0) begin
            csr_write_selected(unsigned'(32'(csr_write_select_reg)), unsigned'(32'(csr_write_value_reg)));
        end
        if (csr_writes()) begin
            if (trace_csr_events && (((((signed'(32'(state_in.csr_addr)) == 32'sh100) || (signed'(32'(state_in.csr_addr)) == 32'sh140)) || (signed'(32'(state_in.csr_addr)) == 32'sh141)) || (signed'(32'(state_in.csr_addr)) == 32'sh180)))) begin
                $write("trace-csr-write pc=%08x addr=%03x old=%08x new=%08x priv=%x\n", state_in.pc, 32'(state_in.csr_addr), commit_read_data_comb, csr_write_value(commit_read_data_comb), unsigned'(32'(priv_reg)));
            end
            csr_write_select_reg_tmp = csr_write_select(32'(state_in.csr_addr));
            csr_write_value_reg_tmp = csr_write_value(commit_read_data_comb);
        end
        if (commit_new_comb && (signed'(32'(state_in.csr_op)) != signed'(32'(Csr_pkg::CNONE)))) begin
            csr_read_data_reg_tmp = commit_read_data_comb;
        end
        if (sync_trap()) begin
            cause=trap_cause_code(state_in);
            is_interrupt=0;
            is_interrupt=interrupt_valid_in;
            tval=((!is_interrupt && (((((cause == 32'h2) || (cause == 32'hC)) || (cause == 32'hD)) || (cause == 32'hF))))) ? (state_in.imm) : (32'h0);
            to_s=trap_to_supervisor(cause);
            if (to_s) begin
                if (trace_csr_events) begin
                    $write("trace-trap-to-s pc=%08x cause=%x tval=%08x priv=%x stvec=%08x mstatus=%08x\n", state_in.pc, cause, tval, unsigned'(32'(priv_reg)), unsigned'(32'(stvec_reg)), unsigned'(32'(mstatus_reg)));
                end
                sepc_reg_tmp = 32'(state_in.pc & ~'h1);
                scause_reg_tmp = unsigned'(32'((is_interrupt) ? ((cause | 'h80000000)) : (cause)));
                stval_reg_tmp = tval;
                mstatus_reg_tmp = (((mstatus_reg & 64'(unsigned'(32'(~(((MSTATUS_SPIE | MSTATUS_SIE) | MSTATUS_SPP))))))) | 64'(unsigned'(32'(((((((mstatus_reg & unsigned'(64'(MSTATUS_SIE))))) != '0)) ? (MSTATUS_SPIE) : (32'h0)))))) | 64'(unsigned'(32'(((((priv_reg == unsigned'(64'(PRIV_S))))) ? (MSTATUS_SPP) : (32'h0)))));
                priv_reg_tmp = PRIV_S;
            end
            else begin
                mepc_reg_tmp = 32'(state_in.pc & ~'h1);
                mcause_reg_tmp = unsigned'(32'((is_interrupt) ? ((cause | 'h80000000)) : (cause)));
                mtval_reg_tmp = tval;
                mstatus_reg_tmp = (((mstatus_reg & 64'(unsigned'(32'(~(((MSTATUS_MPIE | MSTATUS_MIE) | MSTATUS_MPP_MASK))))))) | 64'(unsigned'(32'(((((((mstatus_reg & unsigned'(64'(MSTATUS_MIE))))) != '0)) ? (MSTATUS_MPIE) : (32'h0)))))) | 64'(32'(((unsigned'(32'(priv_reg)) <<< MSTATUS_MPP_SHIFT))));
                priv_reg_tmp = PRIV_M;
            end
        end
        if (commit_new_comb && (signed'(32'(state_in.sys_op)) == signed'(32'(Sys_pkg::MRET)))) begin
            logic[31:0] mpp;
            logic[31:0] mie_restore;
            mpp=((mstatus_reg & unsigned'(64'(MSTATUS_MPP_MASK)))) >>> unsigned'(32'(MSTATUS_MPP_SHIFT));
            mie_restore=((((mstatus_reg & unsigned'(64'(MSTATUS_MPIE)))) != '0)) ? (MSTATUS_MIE) : (32'h0);
            priv_reg_tmp = mpp;
            mstatus_reg_tmp = (((((mstatus_reg & 64'(unsigned'(32'(~MSTATUS_MIE))))) | 64'(mie_restore)) | unsigned'(64'(MSTATUS_MPIE)))) & 64'(unsigned'(32'(~MSTATUS_MPP_MASK)));
        end
        if (commit_new_comb && (signed'(32'(state_in.sys_op)) == signed'(32'(Sys_pkg::SRET)))) begin
            logic[31:0] spp;
            logic[31:0] sie_restore;
            spp=((((mstatus_reg & unsigned'(64'(MSTATUS_SPP)))) != '0)) ? (PRIV_S) : (PRIV_U);
            sie_restore=((((mstatus_reg & unsigned'(64'(MSTATUS_SPIE)))) != '0)) ? (MSTATUS_SIE) : (32'h0);
            if (trace_csr_events) begin
                $write("trace-sret pc=%08x sepc=%08x mstatus=%08x next_priv=%x\n", state_in.pc, unsigned'(32'(sepc_reg)), unsigned'(32'(mstatus_reg)), spp);
            end
            priv_reg_tmp = spp;
            mstatus_reg_tmp = (((((mstatus_reg & 64'(unsigned'(32'(~MSTATUS_SIE))))) | 64'(sie_restore)) | unsigned'(64'(MSTATUS_SPIE)))) & 64'(unsigned'(32'(~MSTATUS_SPP)));
        end
        if (software_irq_set_in) begin
            sip_reg_tmp = 32'(unsigned'(32'(sip_reg)) | IRQ_SSIP);
        end
        if (reset) begin
            mstatus_reg_tmp = '0;
            mtvec_reg_tmp = '0;
            if (reset_priv_in == unsigned'(64'(PRIV_S))) begin
                medeleg_reg_tmp = 32'(((((((((((('h1 <<< 'h0)) | (('h1 <<< 'h1))) | (('h1 <<< 'h3))) | (('h1 <<< 'h4))) | (('h1 <<< 'h5))) | (('h1 <<< 'h6))) | (('h1 <<< 'h7))) | (('h1 <<< 'h8))) | (('h1 <<< 'hC))) | (('h1 <<< 'hD))) | (('h1 <<< 'hF)));
                mideleg_reg_tmp = 32'(((('h1 <<< 'h1)) | (('h1 <<< 'h5))) | (('h1 <<< 'h9)));
                mcounteren_reg_tmp = 'hFFFFFFFF;
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
            csr_write_select_reg_tmp = '0;
            csr_write_value_reg_tmp = '0;
            csr_read_data_reg_tmp = '0;
            priv_reg_tmp = reset_priv_in;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin
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
        csr_write_select_reg_tmp = csr_write_select_reg;
        csr_write_value_reg_tmp = csr_write_value_reg;
        csr_read_data_reg_tmp = csr_read_data_reg;
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
        csr_write_select_reg <= csr_write_select_reg_tmp;
        csr_write_value_reg <= csr_write_value_reg_tmp;
        csr_read_data_reg <= csr_read_data_reg_tmp;
        priv_reg <= priv_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign legality_out = legality_comb;

    assign write_pending_out = write_pending_comb;

    assign read_data_out = execute_read_data_comb;

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

    assign medeleg_out = medeleg_reg;

    assign mip_sw_out = software_pending_comb;

    assign satp_out = satp_reg;

    assign priv_out = priv_reg;


endmodule

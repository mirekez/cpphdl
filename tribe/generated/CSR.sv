`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Csr_pkg::*;
import Sys_pkg::*;


module CSR (
    input wire clk
,   input wire reset
,   input State state_in
,   output wire[31:0] read_data_out
,   output wire[31:0] trap_vector_out
,   output wire[31:0] epc_out
);
    parameter  MISA_RV32IMC = 'h40001104;


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
    reg[32-1:0] dcsr_reg;
    reg[32-1:0] dpc_reg;
    reg[32-1:0] dscratch0_reg;
    reg[32-1:0] dscratch1_reg;
    reg[64-1:0] cycle_reg;
    reg[64-1:0] instret_reg;
    logic[31:0] read_data_comb;
;

    // members
    genvar gi, gj, gk;

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
    logic[32-1:0] dcsr_reg_tmp;
    logic[32-1:0] dpc_reg_tmp;
    logic[32-1:0] dscratch0_reg_tmp;
    logic[32-1:0] dscratch1_reg_tmp;
    logic[64-1:0] cycle_reg_tmp;
    logic[64-1:0] instret_reg_tmp;


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
            return sstatus_reg;
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
            return sip_reg;
        end
        if (addr == 'h180) begin
            return 'h0;
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
            return mip_reg;
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
        disable read_data_comb_func;
    end

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

    function logic csr_writes ();
        logic[31:0] op; op = state_in.csr_op;
        if (!state_in.valid || (op == Csr_pkg::CNONE)) begin
            return 0;
        end
        if ((op == Csr_pkg::CSRRS) || (op == Csr_pkg::CSRRC)) begin
            return state_in.rs1 != 'h0;
        end
        if ((op == Csr_pkg::CSRRSI) || (op == Csr_pkg::CSRRCI)) begin
            return state_in.csr_imm != 'h0;
        end
        return 1;
    endfunction

    task csr_write (
        input logic[31:0] addr
,       input logic[31:0] value
    );
    begin: csr_write
        case (addr)
        'h100: begin
            sstatus_reg_tmp = value;
        end
        'h104: begin
            sie_reg_tmp = value;
        end
        'h105: begin
            stvec_reg_tmp = value;
        end
        'h106: begin
            scounteren_reg_tmp = value;
        end
        'h140: begin
            sscratch_reg_tmp = value;
        end
        'h141: begin
            sepc_reg_tmp = value & ~'h1;
        end
        'h142: begin
            scause_reg_tmp = value;
        end
        'h143: begin
            stval_reg_tmp = value;
        end
        'h144: begin
            sip_reg_tmp = value;
        end
        'h300: begin
            mstatus_reg_tmp = value;
        end
        'h302: begin
            medeleg_reg_tmp = value;
        end
        'h303: begin
            mideleg_reg_tmp = value;
        end
        'h304: begin
            mie_reg_tmp = value;
        end
        'h305: begin
            mtvec_reg_tmp = value;
        end
        'h306: begin
            mcounteren_reg_tmp = value;
        end
        'h320: begin
            mcountinhibit_reg_tmp = value;
        end
        'h340: begin
            mscratch_reg_tmp = value;
        end
        'h341: begin
            mepc_reg_tmp = value & ~'h1;
        end
        'h342: begin
            mcause_reg_tmp = value;
        end
        'h343: begin
            mtval_reg_tmp = value;
        end
        'h344: begin
            mip_reg_tmp = value;
        end
        'h348: begin
            mscratchcsw_reg_tmp = value;
        end
        'h349: begin
            mscratchcswl_reg_tmp = value;
        end
        'hB00: begin
            cycle_reg_tmp = ((unsigned'(64'(cycle_reg)) & 'hFFFFFFFF00000000)) | value;
        end
        'hB80: begin
            cycle_reg_tmp = ((unsigned'(64'(value)) <<< 'h20)) | unsigned'(32'(cycle_reg));
        end
        'hB02: begin
            instret_reg_tmp = ((unsigned'(64'(instret_reg)) & 'hFFFFFFFF00000000)) | value;
        end
        'hB82: begin
            instret_reg_tmp = ((unsigned'(64'(value)) <<< 'h20)) | unsigned'(32'(instret_reg));
        end
        'h7B0: begin
            dcsr_reg_tmp = value;
        end
        'h7B1: begin
            dpc_reg_tmp = value & ~'h1;
        end
        'h7B2: begin
            dscratch0_reg_tmp = value;
        end
        'h7B3: begin
            dscratch1_reg_tmp = value;
        end
        endcase
    end
    endtask

    task _work (input logic reset);
    begin: _work
        logic inhibit_cycle;
        logic inhibit_instret;
        inhibit_cycle=mcountinhibit_reg & 'h1;
        inhibit_instret=((mcountinhibit_reg >>> 'h2)) & 'h1;
        cycle_reg_tmp = (inhibit_cycle) ? (cycle_reg) : (unsigned'(64'(cycle_reg)) + 'h1);
        instret_reg_tmp = ((inhibit_instret || !state_in.valid)) ? (instret_reg) : (unsigned'(64'(instret_reg)) + 'h1);
        if (csr_writes()) begin
            csr_write(state_in.csr_addr, csr_write_value(read_data_comb));
        end
        if (state_in.valid && (state_in.sys_op == Sys_pkg::ECALL)) begin
            mepc_reg_tmp = state_in.pc;
            mcause_reg_tmp = 'hB;
        end
        if (reset) begin
            mstatus_reg_tmp = '0;
            mtvec_reg_tmp = '0;
            medeleg_reg_tmp = '0;
            mideleg_reg_tmp = '0;
            mie_reg_tmp = '0;
            mscratch_reg_tmp = '0;
            mepc_reg_tmp = '0;
            mcause_reg_tmp = '0;
            mtval_reg_tmp = '0;
            mip_reg_tmp = '0;
            mcounteren_reg_tmp = '0;
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
            dcsr_reg_tmp = '0;
            dpc_reg_tmp = '0;
            dscratch0_reg_tmp = '0;
            dscratch1_reg_tmp = '0;
            cycle_reg_tmp = '0;
            instret_reg_tmp = '0;
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
        dcsr_reg_tmp = dcsr_reg;
        dpc_reg_tmp = dpc_reg;
        dscratch0_reg_tmp = dscratch0_reg;
        dscratch1_reg_tmp = dscratch1_reg;
        cycle_reg_tmp = cycle_reg;
        instret_reg_tmp = instret_reg;

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
        dcsr_reg <= dcsr_reg_tmp;
        dpc_reg <= dpc_reg_tmp;
        dscratch0_reg <= dscratch0_reg_tmp;
        dscratch1_reg <= dscratch1_reg_tmp;
        cycle_reg <= cycle_reg_tmp;
        instret_reg <= instret_reg_tmp;
    end

    assign read_data_out = read_data_comb;

    assign trap_vector_out = mtvec_reg;

    assign epc_out = mepc_reg;


endmodule

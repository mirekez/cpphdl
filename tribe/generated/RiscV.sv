`default_nettype none

import Predef_pkg::*;
import Alu_pkg::*;
import Br_pkg::*;
import Mem_pkg::*;
import Rv32i_pkg::*;
import Rv32ic_pkg::*;
import Rv32ic_rv16_pkg::*;
import Rv32im_pkg::*;
import State_pkg::*;
import Wb_pkg::*;


module RiscV (
    input wire clk
,   input wire reset
,   output wire dmem_write_out
,   output logic[31:0] dmem_write_addr_out
,   output logic[31:0] dmem_write_data_out
,   output logic[7:0] dmem_write_mask_out
,   output wire dmem_read_out
,   output logic[31:0] dmem_read_addr_out
,   input logic[31:0] dmem_read_data_in
,   output logic[31:0] imem_read_addr_out
,   input logic[31:0] imem_read_data_in
,   input wire debugen_in
);

    reg[31:0] pc;
    reg valid;
    reg[31:0] alu_result_reg;
    State[2-1:0] state_reg;
    reg[31:0] debug_alu_a_reg;
    reg[31:0] debug_alu_b_reg;
    reg[31:0] debug_branch_target_reg;
    reg debug_branch_taken_reg;
    logic stall_comb;
;

      logic[31:0] dec__pc_in;
      wire dec__instr_valid_in;
      logic[31:0] dec__instr_in;
      logic[31:0] dec__regs_data0_in;
      logic[31:0] dec__regs_data1_in;
      wire[5-1:0] dec__rs1_out;
      wire[5-1:0] dec__rs2_out;
      State dec__state_out;
    Decode      dec (
        .clk(clk)
,       .reset(reset)
,       .pc_in(dec__pc_in)
,       .instr_valid_in(dec__instr_valid_in)
,       .instr_in(dec__instr_in)
,       .regs_data0_in(dec__regs_data0_in)
,       .regs_data1_in(dec__regs_data1_in)
,       .rs1_out(dec__rs1_out)
,       .rs2_out(dec__rs2_out)
,       .state_out(dec__state_out)
    );
      State exe__state_in;
      wire exe__mem_write_out;
      logic[31:0] exe__mem_write_addr_out;
      logic[31:0] exe__mem_write_data_out;
      logic[7:0] exe__mem_write_mask_out;
      wire exe__mem_read_out;
      logic[31:0] exe__mem_read_addr_out;
      logic[31:0] exe__alu_result_out;
      logic[31:0] exe__debug_alu_a_out;
      logic[31:0] exe__debug_alu_b_out;
      wire exe__branch_taken_out;
      logic[31:0] exe__branch_target_out;
    Execute      exe (
        .clk(clk)
,       .reset(reset)
,       .state_in(exe__state_in)
,       .mem_write_out(exe__mem_write_out)
,       .mem_write_addr_out(exe__mem_write_addr_out)
,       .mem_write_data_out(exe__mem_write_data_out)
,       .mem_write_mask_out(exe__mem_write_mask_out)
,       .mem_read_out(exe__mem_read_out)
,       .mem_read_addr_out(exe__mem_read_addr_out)
,       .alu_result_out(exe__alu_result_out)
,       .debug_alu_a_out(exe__debug_alu_a_out)
,       .debug_alu_b_out(exe__debug_alu_b_out)
,       .branch_taken_out(exe__branch_taken_out)
,       .branch_target_out(exe__branch_target_out)
    );
      State wb__state_in;
      logic[31:0] wb__alu_result_in;
      logic[31:0] wb__mem_data_in;
      logic[31:0] wb__regs_data_out;
      logic[7:0] wb__regs_wr_id_out;
      wire wb__regs_write_out;
    Writeback      wb (
        .clk(clk)
,       .reset(reset)
,       .state_in(wb__state_in)
,       .alu_result_in(wb__alu_result_in)
,       .mem_data_in(wb__mem_data_in)
,       .regs_data_out(wb__regs_data_out)
,       .regs_wr_id_out(wb__regs_wr_id_out)
,       .regs_write_out(wb__regs_write_out)
    );
      logic[7:0] regs__write_addr_in;
      wire regs__write_in;
      logic[31:0] regs__write_data_in;
      logic[7:0] regs__read_addr0_in;
      logic[7:0] regs__read_addr1_in;
      wire regs__read_in;
      logic[31:0] regs__read_data0_out;
      logic[31:0] regs__read_data1_out;
      wire regs__debugen_in;
    File #(
        32
,       32
    ) regs (
        .clk(clk)
,       .reset(reset)
,       .write_addr_in(regs__write_addr_in)
,       .write_in(regs__write_in)
,       .write_data_in(regs__write_data_in)
,       .read_addr0_in(regs__read_addr0_in)
,       .read_addr1_in(regs__read_addr1_in)
,       .read_in(regs__read_in)
,       .read_data0_out(regs__read_data0_out)
,       .read_data1_out(regs__read_data1_out)
,       .debugen_in(regs__debugen_in)
    );

    reg[31:0] pc_next;
    reg valid_next;
    reg[31:0] alu_result_reg_next;
    State[2-1:0] state_reg_next;
    reg[31:0] debug_alu_a_reg_next;
    reg[31:0] debug_alu_b_reg_next;
    reg[31:0] debug_branch_target_reg_next;
    reg debug_branch_taken_reg_next;


    always @(*) begin  // stall_comb_func
        stall_comb = 0;
        if (state_reg[0].valid && state_reg[0].wb_op == Wb_pkg::MEM && state_reg[0].rd != 0) begin
            if (state_reg[0].rd == dec__state_out.rs1) begin
                stall_comb = 1;
            end
            if (state_reg[0].rd == dec__state_out.rs2) begin
                stall_comb = 1;
            end
        end
        if ((state_reg[0].valid && state_reg[0].br_op != Br_pkg::BNONE)) begin
            stall_comb = 1;
        end
    end

    task forward ();
    begin: forward
        if (state_reg[1].valid && state_reg[1].wb_op == Wb_pkg::ALU && state_reg[1].rd != 0) begin
            if (dec__state_out.rs1 == state_reg[1].rd) begin
                state_reg_next[0].rs1_val = alu_result_reg;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS1\n", unsigned'(32'(alu_result_reg)));
                end
            end
            if (dec__state_out.rs2 == state_reg[1].rd) begin
                state_reg_next[0].rs2_val = alu_result_reg;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS2\n", unsigned'(32'(alu_result_reg)));
                end
            end
        end
        if (state_reg[0].valid && state_reg[0].wb_op == Wb_pkg::ALU && state_reg[0].rd != 0) begin
            if (dec__state_out.rs1 == state_reg[0].rd) begin
                state_reg_next[0].rs1_val = exe__alu_result_out;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS1\n", unsigned'(32'(exe__alu_result_out)));
                end
            end
            if (dec__state_out.rs2 == state_reg[0].rd) begin
                state_reg_next[0].rs2_val = exe__alu_result_out;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS2\n", unsigned'(32'(exe__alu_result_out)));
                end
            end
        end
        if (state_reg[1].valid && state_reg[1].wb_op == Wb_pkg::MEM && state_reg[1].rd != 0) begin
            if (dec__state_out.rs1 == state_reg[1].rd) begin
                state_reg_next[0].rs1_val = dmem_read_data_in;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS1\n", unsigned'(32'(dmem_read_data_in)));
                end
            end
            if (dec__state_out.rs2 == state_reg[1].rd) begin
                state_reg_next[0].rs2_val = dmem_read_data_in;
                if (debugen_in) begin
                    $write("forwarding %.08x from ALU to RS2\n", unsigned'(32'(dmem_read_data_in)));
                end
            end
        end
    end
    endtask

    function logic signed[31:0] Rv32i___sext (
        input Rv32i _this
,       input logic[31:0] val
,       input logic[31:0] bits
    );
        integer m; m = 1 <<< (bits - 1);
        return (val ^ m) - m;
    endfunction

    function logic signed[31:0] Rv32i___imm_I (input Rv32i _this);
        return Rv32i___sext(_this, _this._.i.imm11_0, 12);
    endfunction

    function logic signed[31:0] Rv32i___imm_S (input Rv32i _this);
        return Rv32i___sext(_this, _this._.s.imm4_0 | (_this._.s.imm11_5 <<< 5), 12);
    endfunction

    function logic signed[31:0] Rv32i___imm_B (input Rv32i _this);
        return Rv32i___sext(_this, (_this._.b.imm4_1 <<< 1) | (_this._.b.imm11 <<< 11) | (_this._.b.imm10_5 <<< 5) | (_this._.b.imm12 <<< 12), 13);
    endfunction

    function logic signed[31:0] Rv32i___imm_J (input Rv32i _this);
        return Rv32i___sext(_this, (_this._.j.imm10_1 <<< 1) | (_this._.j.imm11 <<< 11) | (_this._.j.imm19_12 <<< 12) | (_this._.j.imm20 <<< 20), 21);
    endfunction

    function logic signed[31:0] Rv32i___imm_U (input Rv32i _this);
        return signed'(32'(_this._.u.imm31_12 <<< 12));
    endfunction

    task Rv32i___decode (
        input Rv32i _this
,       output State state_out
    );
    begin: Rv32i___decode
        state_out = 0;
        if (_this._.r.opcode == 3) begin
            state_out.rd = _this._.i.rd;
            state_out.imm = Rv32i___imm_I(_this);
            state_out.mem_op = Mem_pkg::LOAD;
            state_out.alu_op = Alu_pkg::ADD;
            state_out.wb_op = Wb_pkg::MEM;
            state_out.funct3 = _this._.i.funct3;
            state_out.rs1 = _this._.i.rs1;
        end
        else begin
            if (_this._.r.opcode == 35) begin
                state_out.imm = Rv32i___imm_S(_this);
                state_out.mem_op = Mem_pkg::STORE;
                state_out.alu_op = Alu_pkg::ADD;
                state_out.funct3 = _this._.s.funct3;
                state_out.rs1 = _this._.s.rs1;
                state_out.rs2 = _this._.s.rs2;
            end
            else begin
                if (_this._.r.opcode == 19) begin
                    state_out.rd = _this._.i.rd;
                    state_out.imm = Rv32i___imm_I(_this);
                    state_out.wb_op = Wb_pkg::ALU;
                    case (_this._.i.funct3)
                    0: begin
                        state_out.alu_op = Alu_pkg::ADD;
                    end
                    2: begin
                        state_out.alu_op = Alu_pkg::SLT;
                    end
                    3: begin
                        state_out.alu_op = Alu_pkg::SLTU;
                    end
                    4: begin
                        state_out.alu_op = Alu_pkg::XOR;
                    end
                    6: begin
                        state_out.alu_op = Alu_pkg::OR;
                    end
                    7: begin
                        state_out.alu_op = Alu_pkg::AND;
                    end
                    1: begin
                        state_out.alu_op = Alu_pkg::SLL;
                    end
                    5: begin
                        state_out.alu_op = (_this._.i.imm11_0 >>> 10) & 1 ? Alu_pkg::SRA : Alu_pkg::SRL;
                    end
                    endcase
                    state_out.funct3 = _this._.i.funct3;
                    state_out.rs1 = _this._.i.rs1;
                end
                else begin
                    if (_this._.r.opcode == 51) begin
                        state_out.rd = _this._.r.rd;
                        state_out.wb_op = Wb_pkg::ALU;
                        case (_this._.r.funct3)
                        0: begin
                            state_out.alu_op = (_this._.r.funct7 == 32) ? Alu_pkg::SUB : Alu_pkg::ADD;
                        end
                        7: begin
                            state_out.alu_op = (_this._.r.funct7 == 1) ? Alu_pkg::REM : Alu_pkg::AND;
                        end
                        6: begin
                            state_out.alu_op = Alu_pkg::OR;
                        end
                        4: begin
                            state_out.alu_op = Alu_pkg::XOR;
                        end
                        1: begin
                            state_out.alu_op = Alu_pkg::SLL;
                        end
                        5: begin
                            state_out.alu_op = (_this._.r.funct7 == 32) ? Alu_pkg::SRA : Alu_pkg::SRL;
                        end
                        2: begin
                            state_out.alu_op = Alu_pkg::SLT;
                        end
                        3: begin
                            state_out.alu_op = Alu_pkg::SLTU;
                        end
                        endcase
                        state_out.funct3 = _this._.r.funct3;
                        state_out.rs1 = _this._.r.rs1;
                        state_out.rs2 = _this._.r.rs2;
                    end
                    else begin
                        if (_this._.r.opcode == 99) begin
                            state_out.imm = Rv32i___imm_B(_this);
                            state_out.br_op = Br_pkg::BNONE;
                            case (_this._.b.funct3)
                            0: begin
                                state_out.br_op = Br_pkg::BEQ;
                                state_out.alu_op = Alu_pkg::SLTU;
                            end
                            1: begin
                                state_out.br_op = Br_pkg::BNE;
                                state_out.alu_op = Alu_pkg::SLTU;
                            end
                            4: begin
                                state_out.br_op = Br_pkg::BLT;
                                state_out.alu_op = Alu_pkg::SLT;
                            end
                            5: begin
                                state_out.br_op = Br_pkg::BGE;
                                state_out.alu_op = Alu_pkg::SLT;
                            end
                            6: begin
                                state_out.br_op = Br_pkg::BLTU;
                                state_out.alu_op = Alu_pkg::SLTU;
                            end
                            7: begin
                                state_out.br_op = Br_pkg::BGEU;
                                state_out.alu_op = Alu_pkg::SLTU;
                            end
                            endcase
                            state_out.funct3 = _this._.b.funct3;
                            state_out.rs1 = _this._.b.rs1;
                            state_out.rs2 = _this._.b.rs2;
                        end
                        else begin
                            if (_this._.r.opcode == 111) begin
                                state_out.rd = _this._.j.rd;
                                state_out.imm = Rv32i___imm_J(_this);
                                state_out.br_op = Br_pkg::JAL;
                                state_out.wb_op = Wb_pkg::PC4;
                            end
                            else begin
                                if (_this._.r.opcode == 103) begin
                                    state_out.rd = _this._.i.rd;
                                    state_out.imm = Rv32i___imm_I(_this);
                                    state_out.br_op = Br_pkg::JALR;
                                    state_out.wb_op = Wb_pkg::PC4;
                                    state_out.rs1 = _this._.i.rs1;
                                end
                                else begin
                                    if (_this._.r.opcode == 55) begin
                                        state_out.rd = _this._.u.rd;
                                        state_out.imm = Rv32i___imm_U(_this);
                                        state_out.alu_op = Alu_pkg::PASS;
                                        state_out.wb_op = Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (_this._.r.opcode == 23) begin
                                            state_out.rd = _this._.u.rd;
                                            state_out.imm = Rv32i___imm_U(_this);
                                            state_out.alu_op = Alu_pkg::ADD;
                                            state_out.wb_op = Wb_pkg::ALU;
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
    endtask

    function logic[31:0] Rv32ic___bits (
        input Rv32ic _this
,       input integer hi
,       input integer lo
    );
        return (_this._.raw >>> lo) & ((1 <<< (hi - lo + 1)) - 1);
    endfunction

    function logic[31:0] Rv32ic___bit (
        input Rv32ic _this
,       input integer lo
    );
        return (_this._.raw >>> lo) & 1;
    endfunction

    task Rv32ic___decode (
        input Rv32ic _this
,       output State state_out
    );
    begin: Rv32ic___decode
        integer imm_tmp;
        Rv32ic_rv16 i; i = {unsigned'(16'(_this._.raw))};
        state_out = 0;
        if ((_this._.raw & 3) == 3) begin
            Rv32i___decode(_this, state_out);
            disable Rv32ic___decode;
        end
        state_out.funct3 = 2;
        if (i.base.opcode == 0) begin
            if (i.base.funct3 == 0) begin
                state_out.rd = i.base.rd_p + 8;
                state_out.rs1 = 2;
                state_out.imm = (Rv32ic___bits(_this, 10, 7) <<< 6) | (Rv32ic___bits(_this, 12, 11) <<< 4) | (Rv32ic___bits(_this, 6, 5) <<< 2);
                state_out.alu_op = Alu_pkg::ADD;
                state_out.wb_op = Wb_pkg::ALU;
            end
            else begin
                if (i.base.funct3 == 2) begin
                    state_out.rd = i.base.rd_p + 8;
                    state_out.rs1 = i.base.rs1_p + 8;
                    state_out.imm = (Rv32ic___bit(_this, 5) <<< 6) | (Rv32ic___bits(_this, 12, 10) <<< 3) | (Rv32ic___bit(_this, 6) <<< 2);
                    state_out.alu_op = Alu_pkg::ADD;
                    state_out.mem_op = Mem_pkg::LOAD;
                    state_out.wb_op = Wb_pkg::MEM;
                end
                else begin
                    if (i.base.funct3 == 6) begin
                        state_out.rs1 = i.base.rs1_p + 8;
                        state_out.rs2 = i.base.rd_p + 8;
                        state_out.imm = (Rv32ic___bit(_this, 5) <<< 6) | (Rv32ic___bits(_this, 12, 10) <<< 3) | (Rv32ic___bit(_this, 6) <<< 2);
                        state_out.alu_op = Alu_pkg::ADD;
                        state_out.mem_op = Mem_pkg::STORE;
                    end
                end
            end
        end
        else begin
            if (i.base.opcode == 1) begin
                if (i.base.funct3 == 0) begin
                    state_out.rd = i.avg.rs1;
                    state_out.rs1 = i.avg.rs1;
                    imm_tmp = (Rv32ic___bit(_this, 12) <<< 5) | Rv32ic___bits(_this, 6, 2);
                    imm_tmp = (imm_tmp <<< 26) >>> 26;
                    state_out.imm = imm_tmp;
                    state_out.alu_op = Alu_pkg::ADD;
                    state_out.wb_op = Wb_pkg::ALU;
                end
                else begin
                    if (i.base.funct3 == 1) begin
                        state_out.rd = 1;
                        state_out.wb_op = Wb_pkg::PC2;
                        state_out.br_op = Br_pkg::JAL;
                        state_out.imm = (i.base.b12 <<< 11) | (Rv32ic___bit(_this, 8) <<< 10) | (Rv32ic___bits(_this, 10, 9) <<< 8) | (Rv32ic___bit(_this, 6) <<< 7) | (Rv32ic___bit(_this, 7) <<< 6) | (Rv32ic___bit(_this, 2) <<< 5) | (Rv32ic___bit(_this, 11) <<< 4) | (Rv32ic___bits(_this, 5, 3) <<< 1);
                    end
                    else begin
                        if (i.base.funct3 == 2) begin
                            state_out.rd = i.avg.rs1;
                            imm_tmp = (Rv32ic___bit(_this, 12) <<< 5) | Rv32ic___bits(_this, 6, 2);
                            imm_tmp = (imm_tmp <<< 26) >>> 26;
                            state_out.imm = imm_tmp;
                            state_out.alu_op = Alu_pkg::PASS;
                            state_out.wb_op = Wb_pkg::ALU;
                        end
                        else begin
                            if (i.base.funct3 == 3) begin
                                state_out.rd = 2;
                                state_out.rs1 = 2;
                                imm_tmp = (Rv32ic___bit(_this, 12) <<< 9) | (Rv32ic___bit(_this, 4) <<< 8) | (Rv32ic___bit(_this, 3) <<< 7) | (Rv32ic___bit(_this, 5) <<< 6) | (Rv32ic___bit(_this, 2) <<< 5) | (Rv32ic___bit(_this, 6) <<< 4);
                                imm_tmp = (imm_tmp <<< 22) >>> 22;
                                state_out.imm = imm_tmp;
                                state_out.alu_op = Alu_pkg::ADD;
                                state_out.wb_op = Wb_pkg::ALU;
                            end
                            else begin
                                if (i.base.funct3 == 4) begin
                                    if (i.base.bits11_10 == 0) begin
                                        state_out.rd = i.base.rs1_p + 8;
                                        state_out.rs1 = i.base.rs1_p + 8;
                                        state_out.imm = Rv32ic___bits(_this, 6, 2);
                                        state_out.alu_op = Alu_pkg::SRL;
                                        state_out.wb_op = Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (i.base.bits11_10 == 1) begin
                                            state_out.rd = i.base.rs1_p + 8;
                                            state_out.rs1 = i.base.rs1_p + 8;
                                            state_out.imm = Rv32ic___bits(_this, 6, 2);
                                            state_out.alu_op = Alu_pkg::SRA;
                                            state_out.wb_op = Wb_pkg::ALU;
                                        end
                                        else begin
                                            if (i.base.bits11_10 == 2) begin
                                                state_out.rd = i.base.rs1_p + 8;
                                                state_out.rs1 = i.base.rs1_p + 8;
                                                imm_tmp = (Rv32ic___bit(_this, 12) <<< 5) | Rv32ic___bits(_this, 6, 2);
                                                imm_tmp = (imm_tmp <<< 26) >>> 26;
                                                state_out.imm = imm_tmp;
                                                state_out.alu_op = Alu_pkg::AND;
                                                state_out.wb_op = Wb_pkg::ALU;
                                            end
                                            else begin
                                                if (i.base.bits11_10 == 3 && i.base.b12 == 0) begin
                                                    state_out.rd = i.big.rs1;
                                                    state_out.rs1 = i.big.rs1;
                                                    state_out.rs2 = i.big.rs2;
                                                    state_out.alu_op = i.base.bits6_5 == 0 ? Alu_pkg::SUB : (i.base.bits6_5 == 1 ? Alu_pkg::XOR : (i.base.bits6_5 == 2 ? Alu_pkg::OR : Alu_pkg::AND));
                                                    state_out.wb_op = Wb_pkg::ALU;
                                                end
                                            end
                                        end
                                    end
                                end
                                else begin
                                    if (i.base.funct3 == 5) begin
                                        state_out.rd = 0;
                                        state_out.br_op = Br_pkg::JAL;
                                        state_out.imm = (i.base.b12 <<< 11) | (Rv32ic___bit(_this, 8) <<< 10) | (Rv32ic___bits(_this, 10, 9) <<< 8) | (Rv32ic___bit(_this, 6) <<< 7) | (Rv32ic___bit(_this, 7) <<< 6) | (Rv32ic___bit(_this, 2) <<< 5) | (Rv32ic___bit(_this, 11) <<< 4) | (Rv32ic___bits(_this, 5, 3) <<< 1);
                                    end
                                    else begin
                                        if (i.base.funct3 == 6) begin
                                            state_out.rs1 = i.base.rs1_p + 8;
                                            state_out.br_op = Br_pkg::BEQZ;
                                            state_out.alu_op = Alu_pkg::SLTU;
                                            state_out.imm = (i.base.b12 <<< 8) | (Rv32ic___bits(_this, 6, 5) <<< 6) | (Rv32ic___bit(_this, 2) <<< 5) | (Rv32ic___bits(_this, 11, 10) <<< 3) | (Rv32ic___bits(_this, 4, 3) <<< 1);
                                            if (i.base.b12) begin
                                                state_out.imm |= ~511;
                                            end
                                        end
                                        else begin
                                            if (i.base.funct3 == 7) begin
                                                state_out.rs1 = i.base.rs1_p + 8;
                                                state_out.br_op = Br_pkg::BNEZ;
                                                state_out.alu_op = Alu_pkg::SLTU;
                                                state_out.imm = (i.base.b12 <<< 8) | (Rv32ic___bits(_this, 6, 5) <<< 6) | (Rv32ic___bit(_this, 2) <<< 5) | (Rv32ic___bits(_this, 11, 10) <<< 3) | (Rv32ic___bits(_this, 4, 3) <<< 1);
                                                if (i.base.b12) begin
                                                    state_out.imm |= ~511;
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
            else begin
                if (i.base.opcode == 2) begin
                    if (i.base.funct3 == 0) begin
                        state_out.rd = i.big.rs1;
                        state_out.rs1 = i.big.rs1;
                        state_out.imm = (i.base.b12 <<< 5) | Rv32ic___bits(_this, 6, 2);
                        state_out.alu_op = Alu_pkg::SLL;
                        state_out.wb_op = Wb_pkg::ALU;
                    end
                    else begin
                        if (i.base.funct3 == 2) begin
                            state_out.rd = i.big.rs1;
                            state_out.rs1 = 2;
                            state_out.imm = (i.base.b12 <<< 5) | (Rv32ic___bits(_this, 6, 4) <<< 2) | (Rv32ic___bits(_this, 3, 2) <<< 6);
                            state_out.alu_op = Alu_pkg::ADD;
                            state_out.mem_op = Mem_pkg::LOAD;
                            state_out.wb_op = Wb_pkg::MEM;
                        end
                        else begin
                            if (i.base.funct3 == 4) begin
                                if (i.big.rs2 != 0) begin
                                    state_out.rd = i.big.rs1;
                                    state_out.rs1 = i.big.rs1;
                                    state_out.rs2 = i.big.rs2;
                                    state_out.alu_op = i.base.b12 == 0 ? Alu_pkg::PASS : Alu_pkg::ADD;
                                    state_out.wb_op = Wb_pkg::ALU;
                                end
                                else begin
                                    if (i.big.rs2 == 0 && i.base.b12 == 0) begin
                                        state_out.rs1 = i.big.rs1;
                                        state_out.br_op = Br_pkg::JR;
                                        state_out.wb_op = Wb_pkg::PC2;
                                    end
                                    else begin
                                        if (i.big.rs2 == 0 && i.base.b12 == 1) begin
                                            state_out.rs1 = i.big.rs2;
                                            state_out.rd = 1;
                                            state_out.br_op = Br_pkg::JALR;
                                            state_out.wb_op = Wb_pkg::PC2;
                                        end
                                    end
                                end
                            end
                            else begin
                                if (i.base.funct3 == 6) begin
                                    state_out.rs1 = 2;
                                    state_out.rs2 = i.big.rs2;
                                    state_out.imm = (Rv32ic___bits(_this, 8, 7) <<< 6) | (Rv32ic___bits(_this, 12, 9) <<< 2);
                                    state_out.mem_op = Mem_pkg::STORE;
                                    state_out.alu_op = Alu_pkg::ADD;
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    endtask

    task Rv32im___decode (
        input Rv32im _this
,       output State state_out
    );
    begin: Rv32im___decode
        state_out = 0;
        Rv32ic___decode(_this, state_out);
        if (_this._.r.opcode == 51 && _this._.r.funct7 == 1) begin
            state_out.rd = _this._.r.rd;
            state_out.wb_op = Wb_pkg::ALU;
            if (_this._.r.funct3 == 0) begin
                state_out.alu_op = Alu_pkg::MUL;
            end
            if (_this._.r.funct3 == 5) begin
                state_out.alu_op = Alu_pkg::DIV;
            end
            if (_this._.r.funct3 == 3) begin
                state_out.alu_op = Alu_pkg::MULH;
            end
            state_out.funct3 = _this._.r.funct3;
            state_out.rs1 = _this._.r.rs1;
            state_out.rs2 = _this._.r.rs2;
        end
    end
    endtask

    function [63:0] Rv32i___mnemonic (input Rv32i _this);
        logic[31:0] op;
        logic[31:0] f3;
        logic[31:0] f7;
        op = _this._.r.opcode;
        f3 = _this._.r.funct3;
        f7 = _this._.r.funct7;
        case (op)
        51: begin
            if (f3 == 0 && f7 == 0) begin
                return "add   ";
            end
            if (f3 == 0 && f7 == 32) begin
                return "sub   ";
            end
            if (f3 == 0 && f7 == 1) begin
                return "mul   ";
            end
            if (f3 == 7 && f7 == 1) begin
                return "remu  ";
            end
            if (f3 == 7) begin
                return "and   ";
            end
            if (f3 == 6) begin
                return "or    ";
            end
            if (f3 == 4) begin
                return "xor   ";
            end
            if (f3 == 1) begin
                return "sll   ";
            end
            if (f3 == 5 && f7 == 0) begin
                return "srl   ";
            end
            if (f3 == 5 && f7 == 32) begin
                return "sra   ";
            end
            if (f3 == 5 && f7 == 1) begin
                return "divu  ";
            end
            if (f3 == 2) begin
                return "slt   ";
            end
            if (f3 == 3 && f7 == 1) begin
                return "mulhu ";
            end
            if (f3 == 3) begin
                return "sltu  ";
            end
            return "r-type";
        end
        19: begin
            if (f3 == 0) begin
                return "addi  ";
            end
            if (f3 == 7) begin
                return "andi  ";
            end
            if (f3 == 6) begin
                return "ori   ";
            end
            if (f3 == 4) begin
                return "xori  ";
            end
            if (f3 == 1) begin
                return "slli  ";
            end
            if (f3 == 5 && f7 == 0) begin
                return "srli  ";
            end
            if (f3 == 5 && f7 == 32) begin
                return "srai  ";
            end
            if (f3 == 2) begin
                return "slti  ";
            end
            if (f3 == 3) begin
                return "sltiu ";
            end
            return "aluimm";
        end
        3: begin
            return "load  ";
        end
        35: begin
            return "store ";
        end
        99: begin
            return "branch";
        end
        111: begin
            return "jal   ";
        end
        103: begin
            return "jalr  ";
        end
        55: begin
            return "lui   ";
        end
        23: begin
            return "auipc ";
        end
        default: begin
        end
        endcase
        return "unknwn";
    endfunction

    function [63:0] Rv32ic___mnemonic (input Rv32ic _this);
        logic[7:0] op;
        logic[7:0] f3;
        logic[7:0] b12;
        logic[7:0] rs2;
        logic[7:0] bits6_5;
        logic[7:0] bits11_10;
        Rv32ic_rv16 i; i = {unsigned'(16'(_this._.raw))};
        if ((_this._.raw & 3) == 3) begin
            return Rv32i___mnemonic(_this);
        end
        op = i.base.opcode;
        f3 = i.base.funct3;
        b12 = i.base.b12;
        bits6_5 = i.base.bits6_5;
        bits11_10 = i.base.bits11_10;
        rs2 = i.big.rs2;
        case (op)
        0: begin
            case (f3)
            0: begin
                return "addi4s";
            end
            2: begin
                return "lw    ";
            end
            6: begin
                return "sw    ";
            end
            3: begin
                return "ld    ";
            end
            7: begin
                return "sd    ";
            end
            default: begin
            end
            endcase
        end
        1: begin
            case (f3)
            0: begin
                return "addi  ";
            end
            1: begin
                return "jal   ";
            end
            2: begin
                return "li    ";
            end
            3: begin
                return "addisp";
            end
            4: begin
                if (bits11_10 == 0) begin
                    return "srli  ";
                end
                if (bits11_10 == 1) begin
                    return "srai  ";
                end
                if (bits11_10 == 2) begin
                    return "andi  ";
                end
                if (bits11_10 == 3 && b12 == 0 && bits6_5 == 0) begin
                    return "sub   ";
                end
                if (bits11_10 == 3 && b12 == 0 && bits6_5 == 1) begin
                    return "xor   ";
                end
                if (bits11_10 == 3 && b12 == 0 && bits6_5 == 2) begin
                    return "or    ";
                end
                if (bits11_10 == 3 && b12 == 0 && bits6_5 == 3) begin
                    return "and   ";
                end
                return "illgl ";
            end
            5: begin
                return "j     ";
            end
            6: begin
                return "beqz  ";
            end
            7: begin
                return "bnez  ";
            end
            endcase
        end
        2: begin
            case (f3)
            0: begin
                return "slli  ";
            end
            1: begin
                return "fldsp ";
            end
            2: begin
                return "lwsp  ";
            end
            4: begin
                if (rs2 != 0 && b12 == 0) begin
                    return "mv    ";
                end
                if (rs2 != 0 && b12 == 1) begin
                    return "add   ";
                end
                if (rs2 == 0 && b12 == 0) begin
                    return "jr    ";
                end
                if (rs2 == 0 && b12 == 1) begin
                    return "jalr  ";
                end
                return "illgl ";
            end
            6: begin
                return "swsp  ";
            end
            3: begin
                return "ldsp  ";
            end
            7: begin
                return "sdsp  ";
            end
            default: begin
            end
            endcase
        end
        endcase
        return "unknwn";
    endfunction

    function [63:0] Rv32im___mnemonic (input Rv32im _this);
        if (_this._.r.opcode == 51 && _this._.r.funct7 == 1) begin
            if (_this._.r.funct3 == 0) begin
                return "mul   ";
            end
            if (_this._.r.funct3 == 5) begin
                return "divu  ";
            end
            if (_this._.r.funct3 == 3) begin
                return "mulhu ";
            end
        end
        return Rv32ic___mnemonic(_this);
    endfunction

    task debug ();
    begin: debug
        State tmp;
        Rv32im instr; instr = {imem_read_data_in};
        Rv32im___decode(instr, tmp);
        $write("(%d/%d)%x: [%s]%08x  rs%02d/%02d,imm:%08x,rd%02d => (%d)ops:%02d/%x/%x/%x rs%02d/%02d:%08x/%08x,imm:%08x,alu:%09x,rd%02d br(%d)%08x => mem(%d/%d@%08x)%08x/%01x (%d)wop(%x),r(%d)%08x@%02d", logic'(valid), logic'(stall_comb), pc, Rv32im___mnemonic(instr), (instr._.raw & 3) == 3 ? instr._.raw : (instr._.raw | 4294901760), signed'(32'(tmp.rs1)), signed'(32'(tmp.rs2)), tmp.imm, signed'(32'(tmp.rd)), logic'(state_reg[0].valid), unsigned'(8'(state_reg[0].alu_op)), unsigned'(8'(state_reg[0].mem_op)), unsigned'(8'(state_reg[0].br_op)), unsigned'(8'(state_reg[0].wb_op)), signed'(32'(state_reg[0].rs1)), signed'(32'(state_reg[0].rs2)), state_reg[0].rs1_val, state_reg[0].rs2_val, state_reg[0].imm, exe__alu_result_out, signed'(32'(state_reg[0].rd)), logic'(exe__branch_taken_out), exe__branch_target_out, logic'(exe__mem_write_out), logic'(exe__mem_read_out), exe__mem_write_addr_out, exe__mem_write_data_out, exe__mem_write_mask_out, logic'(state_reg[1].valid), unsigned'(8'(state_reg[1].wb_op)), logic'(wb__regs_write_out), wb__regs_data_out, wb__regs_wr_id_out);
        $write("\n");
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (debugen_in) begin
            debug();
        end
        if (dmem_write_addr_out == 287454020 && dmem_write_out) begin
            integer out; out = $fopen("out.txt", "a");
            $fwrite(out, "%c", dmem_write_data_out & 255);
            $fclose(out);
        end
        if (valid && !stall_comb) begin
            pc_next = pc + ((dec__instr_in & 3) == 3 ? 4 : 2);
        end
        if (state_reg[0].valid && exe__branch_taken_out) begin
            pc_next = exe__branch_target_out;
        end
        valid_next = 1;
        state_reg_next[0] = dec__state_out;
        state_reg_next[0].valid = dec__instr_valid_in && !stall_comb;
        forward();
        state_reg_next[1] = state_reg[0];
        alu_result_reg_next = exe__alu_result_out;
        debug_branch_target_reg = exe__branch_target_out;
        debug_branch_taken_reg = exe__branch_taken_out;
        if (reset) begin
            state_reg_next[0].valid = 0;
            state_reg_next[1].valid = 0;
            pc_next = '0;
            valid_next = '0;
        end
    end
    endtask

    task _work_neg (input logic reset);
    begin: _work_neg
    end
    endtask

    generate  // _connect
        assign dec__pc_in = pc;
        assign dec__instr_valid_in = valid;
        assign dec__instr_in = imem_read_data_in;
        assign dec__regs_data0_in = dec__rs1_out == 0 ? 0 : regs__read_data0_out;
        assign dec__regs_data1_in = dec__rs2_out == 0 ? 0 : regs__read_data1_out;
        assign exe__state_in = state_reg[0];
        assign wb__state_in = state_reg[1];
        assign wb__mem_data_in = dmem_read_data_in;
        assign wb__alu_result_in = alu_result_reg;
        assign regs__read_addr0_in = unsigned'(8'(dec__rs1_out));
        assign regs__read_addr1_in = unsigned'(8'(dec__rs2_out));
        assign regs__write_in = wb__regs_write_out;
        assign regs__write_addr_in = wb__regs_wr_id_out;
        assign regs__write_data_in = wb__regs_data_out;
        assign regs__debugen_in = debugen_in;
        assign dmem_write_out = exe__mem_write_out;
        assign dmem_write_addr_out = exe__mem_write_addr_out;
        assign dmem_write_data_out = exe__mem_write_data_out;
        assign dmem_write_mask_out = exe__mem_write_mask_out;
        assign dmem_read_out = exe__mem_read_out;
        assign dmem_read_addr_out = exe__mem_read_addr_out;
    endgenerate

    always @(posedge clk) begin
        _work(reset);

        pc <= pc_next;
        valid <= valid_next;
        alu_result_reg <= alu_result_reg_next;
        state_reg <= state_reg_next;
        debug_alu_a_reg <= debug_alu_a_reg_next;
        debug_alu_b_reg <= debug_alu_b_reg_next;
        debug_branch_target_reg <= debug_branch_target_reg_next;
        debug_branch_taken_reg <= debug_branch_taken_reg_next;
    end

    always @(negedge clk) begin
        _work_neg(reset);
    end

    assign imem_read_addr_out = pc;


endmodule

`default_nettype none

import Predef_pkg::*;
import Alu_pkg::*;
import Br_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import Instr_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_pkg::*;
import Mem_pkg::*;
import MemWBint_int_0_0_State_pkg::*;
import Wb_pkg::*;


module DecodeFetchDecodeFetchint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
    parameter ID
,   parameter LENGTH
 )
 (
    input wire clk
,   input wire reset
,   input logic[31:0] pc_in
,   input wire instr_valid_in
,   input logic[31:0] instr_in
,   input logic[31:0] regs_data0_in
,   input logic[31:0] regs_data1_in
,   output logic[7:0] rs1_out
,   output logic[7:0] rs2_out
,   input logic[31:0] alu_result_in
,   input logic[31:0] mem_data_in
,   output wire stall_out
,   input MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[LENGTH-1:0] state_in
,   output DecodeFetchint_int_0_0_State[LENGTH - ID-1:0] state_out
);

    DecodeFetchint_int_0_0_State state_comb;
    logic[7:0] rs1_out_comb;
    logic[7:0] rs2_out_comb;
    logic stall_comb;
    DecodeFetchint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg;


    DecodeFetchint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg_next;


    generate
    endgenerate
    assign rs1_out = rs1_out_comb;

    assign rs2_out = rs2_out_comb;

    assign stall_out = stall_comb;

    assign state_out = PipelineStage___state_reg;


    function logic signed[31:0] Instr___sext (
        input Instr _this
,       input logic[31:0] val
,       input logic[31:0] bits
    );
        integer m; m = 1 <<< (bits - 1);
        return (val ^ m) - m;
    endfunction

    function logic signed[31:0] Instr___imm_I (input Instr _this);
        return Instr___sext(_this, _this.i.imm11_0, 12);
    endfunction

    function logic signed[31:0] Instr___imm_S (input Instr _this);
        return Instr___sext(_this, _this.s.imm4_0 | (_this.s.imm11_5 <<< 5), 12);
    endfunction

    function logic signed[31:0] Instr___imm_B (input Instr _this);
        return Instr___sext(_this, (_this.b.imm4_1 <<< 1) | (_this.b.imm11 <<< 11) | (_this.b.imm10_5 <<< 5) | (_this.b.imm12 <<< 12), 13);
    endfunction

    function logic signed[31:0] Instr___imm_J (input Instr _this);
        return Instr___sext(_this, (_this.j.imm10_1 <<< 1) | (_this.j.imm11 <<< 11) | (_this.j.imm19_12 <<< 12) | (_this.j.imm20 <<< 20), 21);
    endfunction

    function logic signed[31:0] Instr___imm_U (input Instr _this);
        return signed'(32'(_this.u.imm31_12 <<< 12));
    endfunction

    task Instr___decode (
        input Instr _this
,       output DecodeFetchint_int_0_0_State state_out
    );
    begin: Instr___decode
        state_out = 0;
        if (_this.r.opcode == 3) begin
            state_out.rd = _this.i.rd;
            state_out.imm = Instr___imm_I(_this);
            state_out.mem_op = Mem_pkg::LOAD;
            state_out.alu_op = Alu_pkg::ADD;
            state_out.wb_op = Wb_pkg::MEM;
            state_out.funct3 = _this.i.funct3;
            state_out.rs1 = _this.i.rs1;
        end
        else begin
            if (_this.r.opcode == 35) begin
                state_out.imm = Instr___imm_S(_this);
                state_out.mem_op = Mem_pkg::STORE;
                state_out.alu_op = Alu_pkg::ADD;
                state_out.funct3 = _this.s.funct3;
                state_out.rs1 = _this.s.rs1;
                state_out.rs2 = _this.s.rs2;
            end
            else begin
                if (_this.r.opcode == 19) begin
                    state_out.rd = _this.i.rd;
                    state_out.imm = Instr___imm_I(_this);
                    state_out.wb_op = Wb_pkg::ALU;
                    case (_this.i.funct3)
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
                        state_out.alu_op = (_this.i.imm11_0 >>> 10) & 1 ? Alu_pkg::SRA : Alu_pkg::SRL;
                    end
                    endcase
                    state_out.funct3 = _this.i.funct3;
                    state_out.rs1 = _this.i.rs1;
                end
                else begin
                    if (_this.r.opcode == 51) begin
                        state_out.rd = _this.r.rd;
                        state_out.wb_op = Wb_pkg::ALU;
                        case (_this.r.funct3)
                        0: begin
                            state_out.alu_op = (_this.r.funct7 == 32) ? Alu_pkg::SUB : ((_this.r.funct7 == 1) ? Alu_pkg::MUL : Alu_pkg::ADD);
                        end
                        7: begin
                            state_out.alu_op = (_this.r.funct7 == 1) ? Alu_pkg::REM : Alu_pkg::AND;
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
                            state_out.alu_op = (_this.r.funct7 == 32) ? Alu_pkg::SRA : ((_this.r.funct7 == 1) ? Alu_pkg::DIV : Alu_pkg::SRL);
                        end
                        2: begin
                            state_out.alu_op = Alu_pkg::SLT;
                        end
                        3: begin
                            state_out.alu_op = (_this.r.funct7 == 1) ? Alu_pkg::MULH : Alu_pkg::SLTU;
                        end
                        endcase
                        state_out.funct3 = _this.r.funct3;
                        state_out.rs1 = _this.r.rs1;
                        state_out.rs2 = _this.r.rs2;
                    end
                    else begin
                        if (_this.r.opcode == 99) begin
                            state_out.imm = Instr___imm_B(_this);
                            state_out.br_op = Br_pkg::BNONE;
                            case (_this.b.funct3)
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
                            state_out.funct3 = _this.b.funct3;
                            state_out.rs1 = _this.b.rs1;
                            state_out.rs2 = _this.b.rs2;
                        end
                        else begin
                            if (_this.r.opcode == 111) begin
                                state_out.rd = _this.j.rd;
                                state_out.imm = Instr___imm_J(_this);
                                state_out.br_op = Br_pkg::JAL;
                                state_out.wb_op = Wb_pkg::PC4;
                            end
                            else begin
                                if (_this.r.opcode == 103) begin
                                    state_out.rd = _this.i.rd;
                                    state_out.imm = Instr___imm_I(_this);
                                    state_out.br_op = Br_pkg::JALR;
                                    state_out.wb_op = Wb_pkg::PC4;
                                    state_out.rs1 = _this.i.rs1;
                                end
                                else begin
                                    if (_this.r.opcode == 55) begin
                                        state_out.rd = _this.u.rd;
                                        state_out.imm = Instr___imm_U(_this);
                                        state_out.alu_op = Alu_pkg::PASS;
                                        state_out.wb_op = Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (_this.r.opcode == 23) begin
                                            state_out.rd = _this.u.rd;
                                            state_out.imm = Instr___imm_U(_this);
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

    function logic[31:0] Instr___bits (
        input Instr _this
,       input integer hi
,       input integer lo
    );
        return (_this.raw >>> lo) & ((1 <<< (hi - lo + 1)) - 1);
    endfunction

    function logic[31:0] Instr___bit (
        input Instr _this
,       input integer lo
    );
        return (_this.raw >>> lo) & 1;
    endfunction

    task Instr___decode16 (
        input Instr _this
,       output DecodeFetchint_int_0_0_State state_out
    );
    begin: Instr___decode16
        integer imm_tmp;
        state_out = 0;
        state_out.funct3 = 2;
        if (_this.c.opcode == 0) begin
            if (_this.c.funct3 == 0) begin
                state_out.rd = _this.c.rd_p + 8;
                state_out.rs1 = 2;
                state_out.imm = (Instr___bits(_this, 10, 7) <<< 6) | (Instr___bits(_this, 12, 11) <<< 4) | (Instr___bits(_this, 6, 5) <<< 2);
                state_out.alu_op = Alu_pkg::ADD;
                state_out.wb_op = Wb_pkg::ALU;
            end
            else begin
                if (_this.c.funct3 == 2) begin
                    state_out.rd = _this.c.rd_p + 8;
                    state_out.rs1 = _this.c.rs1_p + 8;
                    state_out.imm = (Instr___bit(_this, 5) <<< 6) | (Instr___bits(_this, 12, 10) <<< 3) | (Instr___bit(_this, 6) <<< 2);
                    state_out.alu_op = Alu_pkg::ADD;
                    state_out.mem_op = Mem_pkg::LOAD;
                    state_out.wb_op = Wb_pkg::MEM;
                end
                else begin
                    if (_this.c.funct3 == 6) begin
                        state_out.rs1 = _this.c.rs1_p + 8;
                        state_out.rs2 = _this.c.rd_p + 8;
                        state_out.imm = (Instr___bit(_this, 5) <<< 6) | (Instr___bits(_this, 12, 10) <<< 3) | (Instr___bit(_this, 6) <<< 2);
                        state_out.alu_op = Alu_pkg::ADD;
                        state_out.mem_op = Mem_pkg::STORE;
                    end
                end
            end
        end
        else begin
            if (_this.c.opcode == 1) begin
                if (_this.c.funct3 == 0) begin
                    state_out.rd = _this.q1.rs1;
                    state_out.rs1 = _this.q1.rs1;
                    imm_tmp = (Instr___bit(_this, 12) <<< 5) | Instr___bits(_this, 6, 2);
                    imm_tmp = (imm_tmp <<< 26) >>> 26;
                    state_out.imm = imm_tmp;
                    state_out.alu_op = Alu_pkg::ADD;
                    state_out.wb_op = Wb_pkg::ALU;
                end
                else begin
                    if (_this.c.funct3 == 1) begin
                        state_out.rd = 1;
                        state_out.wb_op = Wb_pkg::PC2;
                        state_out.br_op = Br_pkg::JAL;
                        state_out.imm = (_this.c.b12 <<< 11) | (Instr___bit(_this, 8) <<< 10) | (Instr___bits(_this, 10, 9) <<< 8) | (Instr___bit(_this, 6) <<< 7) | (Instr___bit(_this, 7) <<< 6) | (Instr___bit(_this, 2) <<< 5) | (Instr___bit(_this, 11) <<< 4) | (Instr___bits(_this, 5, 3) <<< 1);
                    end
                    else begin
                        if (_this.c.funct3 == 2) begin
                            state_out.rd = _this.q1.rs1;
                            imm_tmp = (Instr___bit(_this, 12) <<< 5) | Instr___bits(_this, 6, 2);
                            imm_tmp = (imm_tmp <<< 26) >>> 26;
                            state_out.imm = imm_tmp;
                            state_out.alu_op = Alu_pkg::PASS;
                            state_out.wb_op = Wb_pkg::ALU;
                        end
                        else begin
                            if (_this.c.funct3 == 3) begin
                                state_out.rd = 2;
                                state_out.rs1 = 2;
                                imm_tmp = (Instr___bit(_this, 12) <<< 9) | (Instr___bit(_this, 4) <<< 8) | (Instr___bit(_this, 3) <<< 7) | (Instr___bit(_this, 5) <<< 6) | (Instr___bit(_this, 2) <<< 5) | (Instr___bit(_this, 6) <<< 4);
                                imm_tmp = (imm_tmp <<< 22) >>> 22;
                                state_out.imm = imm_tmp;
                                state_out.alu_op = Alu_pkg::ADD;
                                state_out.wb_op = Wb_pkg::ALU;
                            end
                            else begin
                                if (_this.c.funct3 == 4) begin
                                    if (_this.c.bits11_10 == 0) begin
                                        state_out.rd = _this.c.rs1_p + 8;
                                        state_out.rs1 = _this.c.rs1_p + 8;
                                        state_out.imm = Instr___bits(_this, 6, 2);
                                        state_out.alu_op = Alu_pkg::SRL;
                                        state_out.wb_op = Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (_this.c.bits11_10 == 1) begin
                                            state_out.rd = _this.c.rs1_p + 8;
                                            state_out.rs1 = _this.c.rs1_p + 8;
                                            state_out.imm = Instr___bits(_this, 6, 2);
                                            state_out.alu_op = Alu_pkg::SRA;
                                            state_out.wb_op = Wb_pkg::ALU;
                                        end
                                        else begin
                                            if (_this.c.bits11_10 == 2) begin
                                                state_out.rd = _this.c.rs1_p + 8;
                                                state_out.rs1 = _this.c.rs1_p + 8;
                                                imm_tmp = (Instr___bit(_this, 12) <<< 5) | Instr___bits(_this, 6, 2);
                                                imm_tmp = (imm_tmp <<< 26) >>> 26;
                                                state_out.imm = imm_tmp;
                                                state_out.alu_op = Alu_pkg::AND;
                                                state_out.wb_op = Wb_pkg::ALU;
                                            end
                                            else begin
                                                if (_this.c.bits11_10 == 3 && _this.c.b12 == 0) begin
                                                    state_out.rd = _this.q2.rs1;
                                                    state_out.rs1 = _this.q2.rs1;
                                                    state_out.rs2 = _this.q2.rs2;
                                                    state_out.alu_op = _this.c.bits6_5 == 0 ? Alu_pkg::SUB : (_this.c.bits6_5 == 1 ? Alu_pkg::XOR : (_this.c.bits6_5 == 2 ? Alu_pkg::OR : Alu_pkg::AND));
                                                    state_out.wb_op = Wb_pkg::ALU;
                                                end
                                            end
                                        end
                                    end
                                end
                                else begin
                                    if (_this.c.funct3 == 5) begin
                                        state_out.rd = 0;
                                        state_out.br_op = Br_pkg::JAL;
                                        state_out.imm = (_this.c.b12 <<< 11) | (Instr___bit(_this, 8) <<< 10) | (Instr___bits(_this, 10, 9) <<< 8) | (Instr___bit(_this, 6) <<< 7) | (Instr___bit(_this, 7) <<< 6) | (Instr___bit(_this, 2) <<< 5) | (Instr___bit(_this, 11) <<< 4) | (Instr___bits(_this, 5, 3) <<< 1);
                                    end
                                    else begin
                                        if (_this.c.funct3 == 6) begin
                                            state_out.rs1 = _this.c.rs1_p + 8;
                                            state_out.br_op = Br_pkg::BEQZ;
                                            state_out.alu_op = Alu_pkg::SLTU;
                                            state_out.imm = (_this.c.b12 <<< 8) | (Instr___bits(_this, 6, 5) <<< 6) | (Instr___bit(_this, 2) <<< 5) | (Instr___bits(_this, 11, 10) <<< 3) | (Instr___bits(_this, 4, 3) <<< 1);
                                            if (_this.c.b12) begin
                                                state_out.imm |= ~511;
                                            end
                                        end
                                        else begin
                                            if (_this.c.funct3 == 7) begin
                                                state_out.rs1 = _this.c.rs1_p + 8;
                                                state_out.br_op = Br_pkg::BNEZ;
                                                state_out.alu_op = Alu_pkg::SLTU;
                                                state_out.imm = (_this.c.b12 <<< 8) | (Instr___bits(_this, 6, 5) <<< 6) | (Instr___bit(_this, 2) <<< 5) | (Instr___bits(_this, 11, 10) <<< 3) | (Instr___bits(_this, 4, 3) <<< 1);
                                                if (_this.c.b12) begin
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
                if (_this.c.opcode == 2) begin
                    if (_this.c.funct3 == 0) begin
                        state_out.rd = _this.q2.rs1;
                        state_out.rs1 = _this.q2.rs1;
                        state_out.imm = (_this.c.b12 <<< 5) | Instr___bits(_this, 6, 2);
                        state_out.alu_op = Alu_pkg::SLL;
                        state_out.wb_op = Wb_pkg::ALU;
                    end
                    else begin
                        if (_this.c.funct3 == 2) begin
                            state_out.rd = _this.q2.rs1;
                            state_out.rs1 = 2;
                            state_out.imm = (_this.c.b12 <<< 5) | (Instr___bits(_this, 6, 4) <<< 2) | (Instr___bits(_this, 3, 2) <<< 6);
                            state_out.alu_op = Alu_pkg::ADD;
                            state_out.mem_op = Mem_pkg::LOAD;
                            state_out.wb_op = Wb_pkg::MEM;
                        end
                        else begin
                            if (_this.c.funct3 == 4) begin
                                if (_this.q2.rs2 != 0) begin
                                    state_out.rd = _this.q2.rs1;
                                    state_out.rs1 = _this.q2.rs1;
                                    state_out.rs2 = _this.q2.rs2;
                                    state_out.alu_op = _this.c.b12 == 0 ? Alu_pkg::PASS : Alu_pkg::ADD;
                                    state_out.wb_op = Wb_pkg::ALU;
                                end
                                else begin
                                    if (_this.q2.rs2 == 0 && _this.c.b12 == 0) begin
                                        state_out.rs1 = _this.q2.rs1;
                                        state_out.br_op = Br_pkg::JR;
                                        state_out.wb_op = Wb_pkg::PC2;
                                    end
                                    else begin
                                        if (_this.q2.rs2 == 0 && _this.c.b12 == 1) begin
                                            state_out.rs1 = _this.q2.rs2;
                                            state_out.rd = 1;
                                            state_out.br_op = Br_pkg::JALR;
                                            state_out.wb_op = Wb_pkg::PC2;
                                        end
                                    end
                                end
                            end
                            else begin
                                if (_this.c.funct3 == 6) begin
                                    state_out.rs1 = 2;
                                    state_out.rs2 = _this.q2.rs2;
                                    state_out.imm = (Instr___bits(_this, 8, 7) <<< 6) | (Instr___bits(_this, 12, 9) <<< 2);
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

    always @(*) begin
        logic tmp1;
        logic[31:0] tmp2;
        Instr instr; instr = {instr_in};
        if ((instr.raw & 3) == 3) begin
            Instr___decode(instr, state_comb);
            if (instr.r.opcode == 23) begin
                state_comb.rs1_val = pc_in;
            end
        end
        else begin
            Instr___decode16(instr, state_comb);
        end
        tmp1 = instr_valid_in;
        tmp2 = pc_in;
        state_comb.valid = tmp1;
        state_comb.pc = tmp2;
        rs1_out_comb = state_comb.rs1;
        rs2_out_comb = state_comb.rs2;
    end

    always @(*) begin
        DecodeFetchint_int_0_0_State state_comb_tmp; state_comb_tmp = state_comb;
        stall_comb = 0;
        if (PipelineStage___state_reg[0].valid && PipelineStage___state_reg[0].wb_op == Wb_pkg::MEM && PipelineStage___state_reg[0].rd != 0) begin
            if (PipelineStage___state_reg[0].rd == state_comb_tmp.rs1) begin
                stall_comb = 1;
            end
            if (PipelineStage___state_reg[0].rd == state_comb_tmp.rs2) begin
                stall_comb = 1;
            end
        end
        if ((PipelineStage___state_reg[0].valid && PipelineStage___state_reg[0].br_op != Br_pkg::BNONE)) begin
            stall_comb = 1;
        end
    end

    task do_decode_fetch ();
    begin: do_decode_fetch
        DecodeFetchint_int_0_0_State state_comb_tmp; state_comb_tmp = state_comb;
        if (state_comb_tmp.rs1) begin
            state_comb_tmp.rs1_val = regs_data0_in;
        end
        if (state_comb_tmp.rs2) begin
            state_comb_tmp.rs2_val = regs_data1_in;
        end
        if (PipelineStage___state_reg[1].valid && PipelineStage___state_reg[1].wb_op == Wb_pkg::ALU && PipelineStage___state_reg[1].rd != 0) begin
            if (PipelineStage___state_reg[1].rd == state_comb_tmp.rs1) begin
                state_comb_tmp.rs1_val = state_in[ID + 1].alu_result;
            end
            if (PipelineStage___state_reg[1].rd == state_comb_tmp.rs2) begin
                state_comb_tmp.rs2_val = state_in[ID + 1].alu_result;
            end
        end
        if (PipelineStage___state_reg[0].valid && PipelineStage___state_reg[0].wb_op == Wb_pkg::ALU && PipelineStage___state_reg[0].rd != 0) begin
            if (PipelineStage___state_reg[0].rd == state_comb_tmp.rs1) begin
                state_comb_tmp.rs1_val = alu_result_in;
            end
            if (PipelineStage___state_reg[0].rd == state_comb_tmp.rs2) begin
                state_comb_tmp.rs2_val = alu_result_in;
            end
        end
        if (PipelineStage___state_reg[1].valid && PipelineStage___state_reg[1].wb_op == Wb_pkg::MEM && PipelineStage___state_reg[1].rd != 0) begin
            if (PipelineStage___state_reg[1].rd == state_comb_tmp.rs1) begin
                state_comb_tmp.rs1_val = mem_data_in;
            end
            if (PipelineStage___state_reg[1].rd == state_comb_tmp.rs2) begin
                state_comb_tmp.rs2_val = mem_data_in;
            end
        end
        PipelineStage___state_reg_next[0] = state_comb_tmp;
        PipelineStage___state_reg_next[0].valid = instr_valid_in && !stall_comb;
    end
    endtask

    task PipelineStage____work (input logic reset);
    begin: PipelineStage____work
        logic[63:0] i;
        for (i = 1;i < LENGTH - ID;i=i+1) begin
            PipelineStage___state_reg_next[i] = PipelineStage___state_reg[i - 1];
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            PipelineStage___state_reg_next[0].valid = 0;
            PipelineStage___state_reg_next[1].valid = 0;
            PipelineStage___state_reg_next[2].valid = 0;
        end
        PipelineStage____work(reset);
        do_decode_fetch();
    end
    endtask

    always @(posedge clk) begin
        _work(reset);

        PipelineStage___state_reg <= PipelineStage___state_reg_next;
    end

endmodule

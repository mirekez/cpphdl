`default_nettype none

import Predef_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import Instr_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_pkg::*;
import MemWBint_int_0_0_State_pkg::*;


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
,   input MakeBigStateDecodeFetchint_int_0_0_State[3-1:0] state_in
,   output DecodeFetchint_int_0_0_State[3-1:0] state_out
);

    DecodeFetchint_int_0_0_State state_comb;
    logic[7:0] rs1_out_comb;
    logic[7:0] rs2_out_comb;
    logic stall_comb;
    DecodeFetchint_int_0_0_State[3-1:0] state_reg;
    logic[63:0] i;


    function integer sext (
        input Instr _this
,       input logic[31:0] val
,       input logic[31:0] bits
    );
        integer m; m = 1 << (bits - 1);
        return (val ^ m) - m;
    endfunction

    function integer imm_I (input Instr _this);
        return sext(_this, _this.i.imm11_0, 12);
    endfunction

    function integer imm_S (input Instr _this);
        return sext(_this, _this.s.imm4_0 | (_this.s.imm11_5 << 5), 12);
    endfunction

    function integer imm_B (input Instr _this);
        return sext(_this, (_this.b.imm4_1 << 1) | (_this.b.imm11 << 11) | (_this.b.imm10_5 << 5) | (_this.b.imm12 << 12), 13);
    endfunction

    function integer imm_J (input Instr _this);
        return sext(_this, (_this.j.imm10_1 << 1) | (_this.j.imm11 << 11) | (_this.j.imm19_12 << 12) | (_this.j.imm20 << 20), 21);
    endfunction

    function integer imm_U (input Instr _this);
        return _this.u.imm31_12 << 12;
    endfunction

    task decode (
        input Instr _this
,       input DecodeFetchint_int_0_0_State state
    );
    begin: decode
        state = 0;
        if (_this.r.opcode == 3) begin
            state.rd = _this.i.rd;
            state.imm = imm_I(_this);
            state.mem_op = LOAD;
            state.alu_op = ADD;
            state.wb_op = MEM;
            state.funct3 = _this.i.funct3;
            state.rs1 = _this.i.rs1;
        end
        else begin
            if (_this.r.opcode == 35) begin
                state.imm = imm_S(_this);
                state.mem_op = STORE;
                state.alu_op = ADD;
                state.funct3 = _this.s.funct3;
                state.rs1 = _this.s.rs1;
                state.rs2 = _this.s.rs2;
            end
            else begin
                if (_this.r.opcode == 19) begin
                    state.rd = _this.i.rd;
                    state.imm = imm_I(_this);
                    state.wb_op = ALU;
                    case (_this.i.funct3)
                    0: begin
                        state.alu_op = ADD;
                    end
                    2: begin
                        state.alu_op = SLT;
                    end
                    3: begin
                        state.alu_op = SLTU;
                    end
                    4: begin
                        state.alu_op = XOR;
                    end
                    6: begin
                        state.alu_op = OR;
                    end
                    7: begin
                        state.alu_op = AND;
                    end
                    1: begin
                        state.alu_op = SLL;
                    end
                    5: begin
                        state.alu_op = (_this.i.imm11_0 >> 10) & 1 ? SRA : SRL;
                    end
                    endcase
                    state.funct3 = _this.i.funct3;
                    state.rs1 = _this.i.rs1;
                end
                else begin
                    if (_this.r.opcode == 51) begin
                        state.rd = _this.r.rd;
                        state.wb_op = ALU;
                        case (_this.r.funct3)
                        0: begin
                            state.alu_op = (_this.r.funct7 == 32) ? SUB : ((_this.r.funct7 == 1) ? MUL : ADD);
                        end
                        7: begin
                            state.alu_op = (_this.r.funct7 == 1) ? REM : AND;
                        end
                        6: begin
                            state.alu_op = OR;
                        end
                        4: begin
                            state.alu_op = XOR;
                        end
                        1: begin
                            state.alu_op = SLL;
                        end
                        5: begin
                            state.alu_op = (_this.r.funct7 == 32) ? SRA : ((_this.r.funct7 == 1) ? DIV : SRL);
                        end
                        2: begin
                            state.alu_op = SLT;
                        end
                        3: begin
                            state.alu_op = (_this.r.funct7 == 1) ? MULH : SLTU;
                        end
                        endcase
                        state.funct3 = _this.r.funct3;
                        state.rs1 = _this.r.rs1;
                        state.rs2 = _this.r.rs2;
                    end
                    else begin
                        if (_this.r.opcode == 99) begin
                            state.imm = imm_B(_this);
                            state.br_op = BNONE;
                            case (_this.b.funct3)
                            0: begin
                                state.br_op = BEQ;
                            end
                            1: begin
                                state.br_op = BNE;
                            end
                            4: begin
                                state.br_op = BLT;
                            end
                            5: begin
                                state.br_op = BGE;
                            end
                            6: begin
                                state.br_op = BLTU;
                            end
                            7: begin
                                state.br_op = BGEU;
                            end
                            endcase
                            state.funct3 = _this.b.funct3;
                            state.rs1 = _this.b.rs1;
                            state.rs2 = _this.b.rs2;
                        end
                        else begin
                            if (_this.r.opcode == 111) begin
                                state.rd = _this.j.rd;
                                state.imm = imm_J(_this);
                                state.br_op = JAL;
                                state.wb_op = PC4;
                            end
                            else begin
                                if (_this.r.opcode == 103) begin
                                    state.rd = _this.i.rd;
                                    state.imm = imm_I(_this);
                                    state.br_op = JALR;
                                    state.wb_op = PC4;
                                    state.rs1 = _this.i.rs1;
                                end
                                else begin
                                    if (_this.r.opcode == 55) begin
                                        state.rd = _this.u.rd;
                                        state.imm = imm_U(_this);
                                        state.alu_op = PASS;
                                        state.wb_op = ALU;
                                    end
                                    else begin
                                        if (_this.r.opcode == 23) begin
                                            state.rd = _this.u.rd;
                                            state.imm = imm_U(_this);
                                            state.alu_op = ADD;
                                            state.wb_op = ALU;
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

    function logic[31:0] bits (
        input Instr _this
,       input integer hi
,       input integer lo
    );
        return (_this.raw >> lo) & ((1 << (hi - lo + 1)) - 1);
    endfunction

    function logic[31:0] bit_ (
        input Instr _this
,       input integer lo
    );
        return (_this.raw >> lo) & 1;
    endfunction

    integer imm;
    task decode16 (
        input Instr _this
,       input DecodeFetchint_int_0_0_State state
    );
    begin: decode16
        state = 0;
        state.funct3 = 7;
        state.funct3 = 2;
        if (_this.c.opcode == 0) begin
            if (_this.c.funct3 == 0) begin
                state.rd = _this.c.rd_p + 8;
                state.rs1 = 2;
                state.imm = (_this[7 +:(10 - 7)+1] << 6) | (_this[11 +:(12 - 11)+1] << 4) | (_this[5 +:(6 - 5)+1] << 2);
                state.alu_op = ADD;
                state.wb_op = ALU;
            end
            else begin
                if (_this.c.funct3 == 2) begin
                    state.rd = _this.c.rd_p + 8;
                    state.rs1 = _this.c.rs1_p + 8;
                    state.imm = (bit_(_this, 5) << 6) | (_this[10 +:(12 - 10)+1] << 3) | (bit_(_this, 6) << 2);
                    state.alu_op = ADD;
                    state.mem_op = LOAD;
                    state.wb_op = MEM;
                end
                else begin
                    if (_this.c.funct3 == 6) begin
                        state.rs1 = _this.c.rs1_p + 8;
                        state.rs2 = _this.c.rd_p + 8;
                        state.imm = (bit_(_this, 5) << 6) | (_this[10 +:(12 - 10)+1] << 3) | (bit_(_this, 6) << 2);
                        state.alu_op = ADD;
                        state.mem_op = STORE;
                    end
                end
            end
        end
        else begin
            if (_this.c.opcode == 1) begin
                if (_this.c.funct3 == 0) begin
                    state.rd = _this.q1.rs1;
                    state.rs1 = _this.q1.rs1;
                    imm = (bit_(_this, 12) << 5) | _this[2 +:(6 - 2)+1];
                    imm = (imm << 26) >> 26;
                    state.imm = imm;
                    state.alu_op = ADD;
                    state.wb_op = ALU;
                end
                else begin
                    if (_this.c.funct3 == 1) begin
                        state.rd = 1;
                        state.wb_op = PC2;
                        state.br_op = JAL;
                        state.imm = (_this.c.b12 << 11) | (bit_(_this, 8) << 10) | (_this[9 +:(10 - 9)+1] << 8) | (bit_(_this, 6) << 7) | (bit_(_this, 7) << 6) | (bit_(_this, 2) << 5) | (bit_(_this, 11) << 4) | (_this[3 +:(5 - 3)+1] << 1);
                    end
                    else begin
                        if (_this.c.funct3 == 2) begin
                            state.rd = _this.q1.rs1;
                            imm = (bit_(_this, 12) << 5) | _this[2 +:(6 - 2)+1];
                            imm = (imm << 26) >> 26;
                            state.imm = imm;
                            state.alu_op = PASS;
                            state.wb_op = ALU;
                        end
                        else begin
                            if (_this.c.funct3 == 3) begin
                                state.rd = 2;
                                state.rs1 = 2;
                                imm = (bit_(_this, 12) << 9) | (bit_(_this, 4) << 8) | (bit_(_this, 3) << 7) | (bit_(_this, 5) << 6) | (bit_(_this, 2) << 5) | (bit_(_this, 6) << 4);
                                imm = (imm << 22) >> 22;
                                state.imm = imm;
                                state.alu_op = ADD;
                                state.wb_op = ALU;
                            end
                            else begin
                                if (_this.c.funct3 == 4) begin
                                    if (_this.c.bits11_10 == 0) begin
                                        state.rd = _this.c.rs1_p + 8;
                                        state.rs1 = _this.c.rs1_p + 8;
                                        state.imm = _this[2 +:(6 - 2)+1];
                                        state.alu_op = SRL;
                                        state.wb_op = ALU;
                                    end
                                    else begin
                                        if (_this.c.bits11_10 == 1) begin
                                            state.rd = _this.c.rs1_p + 8;
                                            state.rs1 = _this.c.rs1_p + 8;
                                            state.imm = _this[2 +:(6 - 2)+1];
                                            state.alu_op = SRA;
                                            state.wb_op = ALU;
                                        end
                                        else begin
                                            if (_this.c.bits11_10 == 2) begin
                                                state.rd = _this.c.rs1_p + 8;
                                                state.rs1 = _this.c.rs1_p + 8;
                                                imm = (bit_(_this, 12) << 5) | _this[2 +:(6 - 2)+1];
                                                imm = (imm << 26) >> 26;
                                                state.imm = imm;
                                                state.alu_op = AND;
                                                state.wb_op = ALU;
                                            end
                                            else begin
                                                if (_this.c.bits11_10 == 3 && _this.c.b12 == 0) begin
                                                    state.rd = _this.q2.rs1;
                                                    state.rs1 = _this.q2.rs1;
                                                    state.rs2 = _this.q2.rs2;
                                                    state.alu_op = _this.c.bits6_5 == 0 ? SUB : (_this.c.bits6_5 == 1 ? XOR : (_this.c.bits6_5 == 2 ? OR : AND));
                                                    state.wb_op = ALU;
                                                end
                                            end
                                        end
                                    end
                                end
                                else begin
                                    if (_this.c.funct3 == 5) begin
                                        state.rd = 0;
                                        state.br_op = JAL;
                                        state.imm = (_this.c.b12 << 11) | (bit_(_this, 8) << 10) | (_this[9 +:(10 - 9)+1] << 8) | (bit_(_this, 6) << 7) | (bit_(_this, 7) << 6) | (bit_(_this, 2) << 5) | (bit_(_this, 11) << 4) | (_this[3 +:(5 - 3)+1] << 1);
                                    end
                                    else begin
                                        if (_this.c.funct3 == 6) begin
                                            state.rs1 = _this.c.rs1_p + 8;
                                            state.br_op = BEQZ;
                                            state.alu_op = SLTU;
                                            state.imm = (_this.c.b12 << 8) | (_this[5 +:(6 - 5)+1] << 6) | (bit_(_this, 2) << 5) | (_this[10 +:(11 - 10)+1] << 3) | (_this[3 +:(4 - 3)+1] << 1);
                                            if (_this.c.b12) begin
                                                state.imm |= ~511;
                                            end
                                        end
                                        else begin
                                            if (_this.c.funct3 == 7) begin
                                                state.rs1 = _this.c.rs1_p + 8;
                                                state.br_op = BNEZ;
                                                state.alu_op = SLTU;
                                                state.imm = (_this.c.b12 << 8) | (_this[5 +:(6 - 5)+1] << 6) | (bit_(_this, 2) << 5) | (_this[10 +:(11 - 10)+1] << 3) | (_this[3 +:(4 - 3)+1] << 1);
                                                if (_this.c.b12) begin
                                                    state.imm |= ~511;
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
                        state.rd = _this.q2.rs1;
                        state.rs1 = _this.q2.rs1;
                        state.imm = (_this.c.b12 << 5) | _this[2 +:(6 - 2)+1];
                        state.alu_op = SLL;
                        state.wb_op = ALU;
                    end
                    else begin
                        if (_this.c.funct3 == 2) begin
                            state.rd = _this.q2.rs1;
                            state.rs1 = 2;
                            state.imm = (_this.c.b12 << 5) | (_this[4 +:(6 - 4)+1] << 2) | (_this[2 +:(3 - 2)+1] << 6);
                            state.alu_op = ADD;
                            state.mem_op = LOAD;
                            state.wb_op = MEM;
                        end
                        else begin
                            if (_this.c.funct3 == 4) begin
                                if (_this.q2.rs2 != 0) begin
                                    state.rd = _this.q2.rs1;
                                    state.rs1 = _this.q2.rs1;
                                    state.rs2 = _this.q2.rs2;
                                    state.alu_op = _this.c.b12 == 0 ? PASS : ADD;
                                    state.wb_op = ALU;
                                end
                                else begin
                                    if (_this.q2.rs2 == 0 && _this.c.b12 == 0) begin
                                        state.rs1 = _this.q2.rs1;
                                        state.br_op = JR;
                                        state.wb_op = PC2;
                                    end
                                    else begin
                                        if (_this.q2.rs2 == 0 && _this.c.b12 == 1) begin
                                            state.rs1 = _this.q2.rs2;
                                            state.rd = 1;
                                            state.br_op = JALR;
                                            state.wb_op = PC2;
                                        end
                                    end
                                end
                            end
                            else begin
                                if (_this.c.funct3 == 6) begin
                                    state.rs1 = 2;
                                    state.rs2 = _this.q2.rs2;
                                    state.imm = (_this[7 +:(8 - 7)+1] << 6) | (_this[9 +:(12 - 9)+1] << 2);
                                    state.mem_op = STORE;
                                    state.alu_op = ADD;
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
        Instr instr; instr = instr_in;
        if ((instr.raw & 3) == 3) begin
            decode(instr, state_comb);
            if (instr.r.opcode == 23) begin
                state_comb.rs1_val = pc_in;
            end
        end
        else begin
            decode16(instr, state_comb);
        end
        state_comb.valid = instr_valid_in;
        state_comb.pc = pc_in;
        rs1_out_comb = state_comb.rs1;
        rs2_out_comb = state_comb.rs2;
    end

    always @(*) begin
        DecodeFetchint_int_0_0_State s; s = state_comb;
        stall_comb = 0;
        if (state_reg[0].valid && state_reg[0].wb_op == MEM && state_reg[0].rd != 0) begin
            if (state_reg[0].rd == state_comb.rs1) begin
                stall_comb = 1;
            end
            if (state_reg[0].rd == state_comb.rs2) begin
                stall_comb = 1;
            end
        end
        if ((state_reg[0].valid && state_reg[0].br_op != BNONE)) begin
            stall_comb = 1;
        end
    end

    task do_decode_fetch ();
    begin: do_decode_fetch
        DecodeFetchint_int_0_0_State s; s = state_comb;
        if (state_comb.rs1) begin
            state_comb.rs1_val = regs_data0_in;
        end
        if (state_comb.rs2) begin
            state_comb.rs2_val = regs_data1_in;
        end
        if (state_reg[1].valid && state_reg[1].wb_op == ALU && state_reg[1].rd != 0) begin
            if (state_reg[1].rd == state_comb.rs1) begin
                state_comb.rs1_val = state_in[ID + 1].alu_result;
            end
            if (state_reg[1].rd == state_comb.rs2) begin
                state_comb.rs2_val = state_in[ID + 1].alu_result;
            end
        end
        if (state_reg[0].valid && state_reg[0].wb_op == ALU && state_reg[0].rd != 0) begin
            if (state_reg[0].rd == state_comb.rs1) begin
                state_comb.rs1_val = alu_result_in;
            end
            if (state_reg[0].rd == state_comb.rs2) begin
                state_comb.rs2_val = alu_result_in;
            end
        end
        if (state_reg[1].valid && state_reg[1].wb_op == MEM && state_reg[1].rd != 0) begin
            if (state_reg[1].rd == state_comb.rs1) begin
                state_comb.rs1_val = mem_data_in;
            end
            if (state_reg[1].rd == state_comb.rs2) begin
                state_comb.rs2_val = mem_data_in;
            end
        end
        state_reg[0] = s;
        state_reg[0].valid = instr_valid_in && !stall_comb;
    end
    endtask

    task work (input logic reset);
    begin: work
        if (reset) begin
            state_reg[0].valid = 0;
            state_reg[1].valid = 0;
            state_reg[2].valid = 0;
        end
        do_decode_fetch();
    end
    endtask


    always @(posedge clk) begin
        work(reset);
    end

endmodule

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


module Decode (
    input wire clk
,   input wire reset
,   input logic[31:0] pc_in
,   input wire instr_valid_in
,   input logic[31:0] instr_in
,   input logic[31:0] regs_data0_in
,   input logic[31:0] regs_data1_in
,   output wire[5-1:0] rs1_out
,   output wire[5-1:0] rs2_out
,   output State state_out
);

    State state_comb;
;
    logic[5-1:0] rs1_out_comb;
;
    logic[5-1:0] rs2_out_comb;
;




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

    always @(*) begin  // state_comb_func
        Rv32im instr; instr = {instr_in};
        Rv32im___decode(instr, state_comb);
        if ((instr._.raw & 3) == 3 && instr._.r.opcode == 23) begin
            state_comb.rs1_val = pc_in;
        end
        state_comb.valid = instr_valid_in;
        state_comb.pc = pc_in;
        if (state_comb.rs1) begin
            state_comb.rs1_val = regs_data0_in;
        end
        if (state_comb.rs2) begin
            state_comb.rs2_val = regs_data1_in;
        end
    end

    always @(*) begin  // rs1_out_comb_func
        rs1_out_comb = state_comb.rs1;
    end

    always @(*) begin  // rs2_out_comb_func
        rs2_out_comb = state_comb.rs2;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin
        _work(reset);

    end

    assign rs1_out = rs1_out_comb;

    assign rs2_out = rs2_out_comb;

    assign state_out = state_comb;


endmodule

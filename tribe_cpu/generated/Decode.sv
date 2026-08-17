`default_nettype none

import Predef_pkg::*;
import Zicsr_pkg::*;
import Rv32ia_pkg::*;
import Rv32im_pkg::*;
import Rv32ic_pkg::*;
import Rv32i_pkg::*;
import Mem_pkg::*;
import Alu_pkg::*;
import Wb_pkg::*;
import Br_pkg::*;
import Sys_pkg::*;
import Trap_pkg::*;
import Amo_pkg::*;
import Csr_pkg::*;
import State_pkg::*;


module Decode (
    input wire clk
,   input wire reset
,   input wire[31:0] pc_in
,   input wire instr_valid_in
,   input wire[31:0] instr_in
,   input wire[31:0] regs_data0_in
,   input wire[31:0] regs_data1_in
,   output wire[5-1:0] rs1_out
,   output wire[5-1:0] rs2_out
,   output State state_out
);


    // regs and combs
    State state_comb;
;
    logic[5-1:0] rs1_out_comb;
;
    logic[5-1:0] rs2_out_comb;
;

    // members

    // tmp variables


    function logic signed[31:0] Rv32i___sext (
        input Rv32i _this
,       input logic[31:0] val
,       input logic[31:0] bits
    );
        logic signed[31:0] m; m = 'h1 <<< ((bits - 'h1));
        return ((val ^ m)) - m;
    endfunction

    function logic signed[31:0] Rv32i___imm_I (input Rv32i _this);
        return Rv32i___sext(_this, _this._.i.imm11_0, 'hC);
    endfunction

    function logic signed[31:0] Rv32i___imm_S (input Rv32i _this);
        return Rv32i___sext(_this, _this._.s.imm4_0 | ((_this._.s.imm11_5 <<< 'h5)), 'hC);
    endfunction

    function logic signed[31:0] Rv32i___imm_B (input Rv32i _this);
        return Rv32i___sext(_this, ((((_this._.b.imm4_1 <<< 'h1)) | ((_this._.b.imm11 <<< 'hB))) | ((_this._.b.imm10_5 <<< 'h5))) | ((_this._.b.imm12 <<< 'hC)), 'hD);
    endfunction

    function logic signed[31:0] Rv32i___imm_J (input Rv32i _this);
        return Rv32i___sext(_this, ((((_this._.j.imm10_1 <<< 'h1)) | ((_this._.j.imm11 <<< 'hB))) | ((_this._.j.imm19_12 <<< 'hC))) | ((_this._.j.imm20 <<< 'h14)), 'h15);
    endfunction

    function logic signed[31:0] Rv32i___imm_U (input Rv32i _this);
        return signed'(32'(_this._.u.imm31_12 <<< 'hC));
    endfunction

    task Rv32i___decode (
        input Rv32i _this
,       output State state_out
    );
    begin: Rv32i___decode
        state_out = 0;
        if (_this._.r.opcode == 'h3) begin
            state_out.rd=_this._.i.rd;
            state_out.imm=Rv32i___imm_I(_this);
            state_out.mem_op=Mem_pkg::LOAD;
            state_out.alu_op=Alu_pkg::ADD;
            state_out.wb_op=Wb_pkg::MEM;
            state_out.funct3=_this._.i.funct3;
            state_out.rs1=_this._.i.rs1;
        end
        else begin
            if (_this._.r.opcode == 'h23) begin
                state_out.imm=Rv32i___imm_S(_this);
                state_out.mem_op=Mem_pkg::STORE;
                state_out.alu_op=Alu_pkg::ADD;
                state_out.funct3=_this._.s.funct3;
                state_out.rs1=_this._.s.rs1;
                state_out.rs2=_this._.s.rs2;
            end
            else begin
                if (_this._.r.opcode == 'h13) begin
                    state_out.rd=_this._.i.rd;
                    state_out.imm=Rv32i___imm_I(_this);
                    state_out.wb_op=Wb_pkg::ALU;
                    case (_this._.i.funct3)
                    'h0: begin
                        state_out.alu_op=Alu_pkg::ADD;
                    end
                    'h2: begin
                        state_out.alu_op=Alu_pkg::SLT;
                    end
                    'h3: begin
                        state_out.alu_op=Alu_pkg::SLTU;
                    end
                    'h4: begin
                        state_out.alu_op=Alu_pkg::XOR;
                    end
                    'h6: begin
                        state_out.alu_op=Alu_pkg::OR;
                    end
                    'h7: begin
                        state_out.alu_op=Alu_pkg::AND;
                    end
                    'h1: begin
                        state_out.alu_op=Alu_pkg::SLL;
                    end
                    'h5: begin
                        state_out.alu_op=(((_this._.i.imm11_0 >>> 'hA)) & 'h1) ? (Alu_pkg::SRA) : (Alu_pkg::SRL);
                    end
                    endcase
                    state_out.funct3=_this._.i.funct3;
                    state_out.rs1=_this._.i.rs1;
                end
                else begin
                    if (_this._.r.opcode == 'h33) begin
                        state_out.rd=_this._.r.rd;
                        state_out.wb_op=Wb_pkg::ALU;
                        case (_this._.r.funct3)
                        'h0: begin
                            state_out.alu_op=((_this._.r.funct7 == 'h20)) ? (Alu_pkg::SUB) : (Alu_pkg::ADD);
                        end
                        'h7: begin
                            state_out.alu_op=((_this._.r.funct7 == 'h1)) ? (Alu_pkg::REM) : (Alu_pkg::AND);
                        end
                        'h6: begin
                            state_out.alu_op=Alu_pkg::OR;
                        end
                        'h4: begin
                            state_out.alu_op=Alu_pkg::XOR;
                        end
                        'h1: begin
                            state_out.alu_op=Alu_pkg::SLL;
                        end
                        'h5: begin
                            state_out.alu_op=((_this._.r.funct7 == 'h20)) ? (Alu_pkg::SRA) : (Alu_pkg::SRL);
                        end
                        'h2: begin
                            state_out.alu_op=Alu_pkg::SLT;
                        end
                        'h3: begin
                            state_out.alu_op=Alu_pkg::SLTU;
                        end
                        endcase
                        state_out.funct3=_this._.r.funct3;
                        state_out.rs1=_this._.r.rs1;
                        state_out.rs2=_this._.r.rs2;
                    end
                    else begin
                        if (_this._.r.opcode == 'h63) begin
                            state_out.imm=Rv32i___imm_B(_this);
                            state_out.br_op=Br_pkg::BNONE;
                            case (_this._.b.funct3)
                            'h0: begin
                                state_out.br_op=Br_pkg::BEQ;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h1: begin
                                state_out.br_op=Br_pkg::BNE;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h4: begin
                                state_out.br_op=Br_pkg::BLT;
                                state_out.alu_op=Alu_pkg::SLT;
                            end
                            'h5: begin
                                state_out.br_op=Br_pkg::BGE;
                                state_out.alu_op=Alu_pkg::SLT;
                            end
                            'h6: begin
                                state_out.br_op=Br_pkg::BLTU;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            'h7: begin
                                state_out.br_op=Br_pkg::BGEU;
                                state_out.alu_op=Alu_pkg::SLTU;
                            end
                            endcase
                            state_out.funct3=_this._.b.funct3;
                            state_out.rs1=_this._.b.rs1;
                            state_out.rs2=_this._.b.rs2;
                        end
                        else begin
                            if (_this._.r.opcode == 'h6F) begin
                                state_out.rd=_this._.j.rd;
                                state_out.imm=Rv32i___imm_J(_this);
                                state_out.br_op=Br_pkg::JAL;
                                state_out.wb_op=Wb_pkg::PC4;
                            end
                            else begin
                                if (_this._.r.opcode == 'h67) begin
                                    state_out.rd=_this._.i.rd;
                                    state_out.imm=Rv32i___imm_I(_this);
                                    state_out.br_op=Br_pkg::JALR;
                                    state_out.wb_op=Wb_pkg::PC4;
                                    state_out.rs1=_this._.i.rs1;
                                end
                                else begin
                                    if (_this._.r.opcode == 'h37) begin
                                        state_out.rd=_this._.u.rd;
                                        state_out.imm=Rv32i___imm_U(_this);
                                        state_out.alu_op=Alu_pkg::PASS;
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (_this._.r.opcode == 'h17) begin
                                            state_out.rd=_this._.u.rd;
                                            state_out.imm=Rv32i___imm_U(_this);
                                            state_out.alu_op=Alu_pkg::ADD;
                                            state_out.wb_op=Wb_pkg::ALU;
                                        end
                                        else begin
                                            if (_this._.r.opcode == 'hF) begin
                                                if (_this._.i.funct3 == 'h1) begin
                                                    state_out.sys_op=Sys_pkg::FENCEI;
                                                    state_out.br_op=Br_pkg::BNONE;
                                                end
                                                else begin
                                                    if (_this._.i.funct3 == 'h0) begin
                                                        state_out.sys_op=Sys_pkg::SNONE;
                                                    end
                                                    else begin
                                                        state_out.sys_op=Sys_pkg::TRAP;
                                                        state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                                                        state_out.imm=_this._.raw;
                                                        state_out.br_op=Br_pkg::BNONE;
                                                    end
                                                end
                                            end
                                            else begin
                                                state_out.sys_op=Sys_pkg::TRAP;
                                                state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                                                state_out.imm=_this._.raw;
                                                state_out.br_op=Br_pkg::BNONE;
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
    end
    endtask

    function logic[31:0] Rv32ic___bits (
        input Rv32ic _this
,       input logic signed[31:0] hi
,       input logic signed[31:0] lo
    );
        return ((_this._.raw >>> lo)) & (((('h1 <<< (((hi - lo) + 'h1)))) - 'h1));
    endfunction

    function logic[31:0] Rv32ic___bit (
        input Rv32ic _this
,       input logic signed[31:0] lo
    );
        return ((_this._.raw >>> lo)) & 'h1;
    endfunction

    task Rv32ic___decode (
        input Rv32ic _this
,       output State state_out
    );
    begin: Rv32ic___decode
        logic signed[31:0] imm_tmp;
        logic[31:0] opcode;
        logic[31:0] funct3;
        logic[31:0] rd_p;
        logic[31:0] rs1_p;
        logic[31:0] bits6_5;
        logic[31:0] bits11_10;
        logic[31:0] b12;
        logic[31:0] rd_rs1;
        logic[31:0] rs2;
        state_out = 0;
        if (((_this._.raw & 'h3)) == 'h3) begin
            Rv32i___decode(_this, state_out);
            disable Rv32ic___decode;
        end
        opcode=Rv32ic___bits(_this, 'h1, 'h0);
        funct3=Rv32ic___bits(_this, 'hF, 'hD);
        rd_p=Rv32ic___bits(_this, 'h4, 'h2);
        rs1_p=Rv32ic___bits(_this, 'h9, 'h7);
        bits6_5=Rv32ic___bits(_this, 'h6, 'h5);
        bits11_10=Rv32ic___bits(_this, 'hB, 'hA);
        b12=Rv32ic___bit(_this, 'hC);
        rd_rs1=Rv32ic___bits(_this, 'hB, 'h7);
        rs2=Rv32ic___bits(_this, 'h6, 'h2);
        state_out.funct3='h2;
        if (opcode == 'h0) begin
            if (funct3 == 'h0) begin
                state_out.rd=rd_p + 'h8;
                state_out.rs1='h2;
                state_out.imm=((((Rv32ic___bits(_this, 'hA, 'h7) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hB) <<< 'h4))) | ((Rv32ic___bit(_this, 'h5) <<< 'h3))) | ((Rv32ic___bit(_this, 'h6) <<< 'h2));
                state_out.alu_op=Alu_pkg::ADD;
                state_out.wb_op=Wb_pkg::ALU;
            end
            else begin
                if (funct3 == 'h2) begin
                    state_out.rd=rd_p + 'h8;
                    state_out.rs1=rs1_p + 'h8;
                    state_out.imm=(((Rv32ic___bit(_this, 'h5) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hA) <<< 'h3))) | ((Rv32ic___bit(_this, 'h6) <<< 'h2));
                    state_out.alu_op=Alu_pkg::ADD;
                    state_out.mem_op=Mem_pkg::LOAD;
                    state_out.wb_op=Wb_pkg::MEM;
                end
                else begin
                    if (funct3 == 'h6) begin
                        state_out.rs1=rs1_p + 'h8;
                        state_out.rs2=rd_p + 'h8;
                        state_out.imm=(((Rv32ic___bit(_this, 'h5) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'hA) <<< 'h3))) | ((Rv32ic___bit(_this, 'h6) <<< 'h2));
                        state_out.alu_op=Alu_pkg::ADD;
                        state_out.mem_op=Mem_pkg::STORE;
                    end
                end
            end
        end
        else begin
            if (opcode == 'h1) begin
                if (funct3 == 'h0) begin
                    state_out.rd=rd_rs1;
                    state_out.rs1=rd_rs1;
                    imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                    imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                    state_out.imm=imm_tmp;
                    state_out.alu_op=Alu_pkg::ADD;
                    state_out.wb_op=Wb_pkg::ALU;
                end
                else begin
                    if (funct3 == 'h1) begin
                        state_out.rd='h1;
                        state_out.wb_op=Wb_pkg::PC2;
                        state_out.br_op=Br_pkg::JAL;
                        state_out.imm=Rv32i___sext(_this, ((((((((b12 <<< 'hB)) | ((Rv32ic___bit(_this, 'h8) <<< 'hA))) | ((Rv32ic___bits(_this, 'hA, 'h9) <<< 'h8))) | ((Rv32ic___bit(_this, 'h6) <<< 'h7))) | ((Rv32ic___bit(_this, 'h7) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'hB) <<< 'h4))) | ((Rv32ic___bits(_this, 'h5, 'h3) <<< 'h1)), 'hC);
                    end
                    else begin
                        if (funct3 == 'h2) begin
                            state_out.rd=rd_rs1;
                            imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                            imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                            state_out.imm=imm_tmp;
                            state_out.alu_op=Alu_pkg::PASS;
                            state_out.wb_op=Wb_pkg::ALU;
                        end
                        else begin
                            if (funct3 == 'h3) begin
                                if (rd_rs1 == 'h2) begin
                                    state_out.rd='h2;
                                    state_out.rs1='h2;
                                    imm_tmp=((((((Rv32ic___bit(_this, 'hC) <<< 'h9)) | ((Rv32ic___bit(_this, 'h4) <<< 'h8))) | ((Rv32ic___bit(_this, 'h3) <<< 'h7))) | ((Rv32ic___bit(_this, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'h6) <<< 'h4));
                                    state_out.imm=Rv32i___sext(_this, imm_tmp, 'hA);
                                    state_out.alu_op=Alu_pkg::ADD;
                                    state_out.wb_op=Wb_pkg::ALU;
                                end
                                else begin
                                    state_out.rd=rd_rs1;
                                    imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                                    imm_tmp=((imm_tmp <<< 'h1A)) >>> 'hE;
                                    state_out.imm=imm_tmp;
                                    state_out.alu_op=Alu_pkg::PASS;
                                    state_out.wb_op=Wb_pkg::ALU;
                                end
                            end
                            else begin
                                if (funct3 == 'h4) begin
                                    if (bits11_10 == 'h0) begin
                                        state_out.rd=rs1_p + 'h8;
                                        state_out.rs1=rs1_p + 'h8;
                                        state_out.imm=Rv32ic___bits(_this, 'h6, 'h2);
                                        state_out.alu_op=Alu_pkg::SRL;
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (bits11_10 == 'h1) begin
                                            state_out.rd=rs1_p + 'h8;
                                            state_out.rs1=rs1_p + 'h8;
                                            state_out.imm=Rv32ic___bits(_this, 'h6, 'h2);
                                            state_out.alu_op=Alu_pkg::SRA;
                                            state_out.wb_op=Wb_pkg::ALU;
                                        end
                                        else begin
                                            if (bits11_10 == 'h2) begin
                                                state_out.rd=rs1_p + 'h8;
                                                state_out.rs1=rs1_p + 'h8;
                                                imm_tmp=((Rv32ic___bit(_this, 'hC) <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                                                imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                                                state_out.imm=imm_tmp;
                                                state_out.alu_op=Alu_pkg::AND;
                                                state_out.wb_op=Wb_pkg::ALU;
                                            end
                                            else begin
                                                if ((bits11_10 == 'h3) && (b12 == 'h0)) begin
                                                    state_out.rd=rs1_p + 'h8;
                                                    state_out.rs1=rs1_p + 'h8;
                                                    state_out.rs2=rd_p + 'h8;
                                                    state_out.alu_op=(bits6_5 == 'h0) ? (Alu_pkg::SUB) : (((bits6_5 == 'h1) ? (Alu_pkg::XOR) : (((bits6_5 == 'h2) ? (Alu_pkg::OR) : (Alu_pkg::AND)))));
                                                    state_out.wb_op=Wb_pkg::ALU;
                                                end
                                            end
                                        end
                                    end
                                end
                                else begin
                                    if (funct3 == 'h5) begin
                                        state_out.rd='h0;
                                        state_out.br_op=Br_pkg::JAL;
                                        state_out.imm=Rv32i___sext(_this, ((((((((b12 <<< 'hB)) | ((Rv32ic___bit(_this, 'h8) <<< 'hA))) | ((Rv32ic___bits(_this, 'hA, 'h9) <<< 'h8))) | ((Rv32ic___bit(_this, 'h6) <<< 'h7))) | ((Rv32ic___bit(_this, 'h7) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bit(_this, 'hB) <<< 'h4))) | ((Rv32ic___bits(_this, 'h5, 'h3) <<< 'h1)), 'hC);
                                    end
                                    else begin
                                        if (funct3 == 'h6) begin
                                            state_out.rs1=rs1_p + 'h8;
                                            state_out.br_op=Br_pkg::BEQZ;
                                            state_out.alu_op=Alu_pkg::SLTU;
                                            state_out.imm=(((((b12 <<< 'h8)) | ((Rv32ic___bits(_this, 'h6, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bits(_this, 'hB, 'hA) <<< 'h3))) | ((Rv32ic___bits(_this, 'h4, 'h3) <<< 'h1));
                                            if (b12) begin
                                                state_out.imm|=~'h1FF;
                                            end
                                        end
                                        else begin
                                            if (funct3 == 'h7) begin
                                                state_out.rs1=rs1_p + 'h8;
                                                state_out.br_op=Br_pkg::BNEZ;
                                                state_out.alu_op=Alu_pkg::SLTU;
                                                state_out.imm=(((((b12 <<< 'h8)) | ((Rv32ic___bits(_this, 'h6, 'h5) <<< 'h6))) | ((Rv32ic___bit(_this, 'h2) <<< 'h5))) | ((Rv32ic___bits(_this, 'hB, 'hA) <<< 'h3))) | ((Rv32ic___bits(_this, 'h4, 'h3) <<< 'h1));
                                                if (b12) begin
                                                    state_out.imm|=~'h1FF;
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
                if (opcode == 'h2) begin
                    if (funct3 == 'h0) begin
                        state_out.rd=rd_rs1;
                        state_out.rs1=rd_rs1;
                        state_out.imm=((b12 <<< 'h5)) | Rv32ic___bits(_this, 'h6, 'h2);
                        state_out.alu_op=Alu_pkg::SLL;
                        state_out.wb_op=Wb_pkg::ALU;
                    end
                    else begin
                        if (funct3 == 'h2) begin
                            state_out.rd=rd_rs1;
                            state_out.rs1='h2;
                            state_out.imm=(((b12 <<< 'h5)) | ((Rv32ic___bits(_this, 'h6, 'h4) <<< 'h2))) | ((Rv32ic___bits(_this, 'h3, 'h2) <<< 'h6));
                            state_out.alu_op=Alu_pkg::ADD;
                            state_out.mem_op=Mem_pkg::LOAD;
                            state_out.wb_op=Wb_pkg::MEM;
                        end
                        else begin
                            if (funct3 == 'h4) begin
                                if (rs2 != 'h0) begin
                                    state_out.rd=rd_rs1;
                                    state_out.rs2=rs2;
                                    if (b12 == 'h0) begin
                                        state_out.alu_op=Alu_pkg::PASS;
                                    end
                                    else begin
                                        state_out.rs1=rd_rs1;
                                        state_out.alu_op=Alu_pkg::ADD;
                                    end
                                    state_out.wb_op=Wb_pkg::ALU;
                                end
                                else begin
                                    if ((rs2 == 'h0) && (b12 == 'h0)) begin
                                        state_out.rs1=rd_rs1;
                                        state_out.br_op=Br_pkg::JR;
                                        state_out.wb_op=Wb_pkg::PC2;
                                    end
                                    else begin
                                        if ((rs2 == 'h0) && (b12 == 'h1)) begin
                                            state_out.rs1=rd_rs1;
                                            state_out.rd='h1;
                                            state_out.br_op=Br_pkg::JALR;
                                            state_out.wb_op=Wb_pkg::PC2;
                                        end
                                    end
                                end
                            end
                            else begin
                                if (funct3 == 'h6) begin
                                    state_out.rs1='h2;
                                    state_out.rs2=rs2;
                                    state_out.imm=((Rv32ic___bits(_this, 'h8, 'h7) <<< 'h6)) | ((Rv32ic___bits(_this, 'hC, 'h9) <<< 'h2));
                                    state_out.mem_op=Mem_pkg::STORE;
                                    state_out.alu_op=Alu_pkg::ADD;
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
        if ((_this._.r.opcode == 'h33) && (_this._.r.funct7 == 'h1)) begin
            state_out = 0;
            state_out.rd=_this._.r.rd;
            state_out.wb_op=Wb_pkg::ALU;
            case (_this._.r.funct3)
            'h0: begin
                state_out.alu_op=Alu_pkg::MUL;
            end
            'h1: begin
                state_out.alu_op=Alu_pkg::MULH;
            end
            'h2: begin
                state_out.alu_op=Alu_pkg::MULHSU;
            end
            'h3: begin
                state_out.alu_op=Alu_pkg::MULHU;
            end
            'h4: begin
                state_out.alu_op=Alu_pkg::DIV;
            end
            'h5: begin
                state_out.alu_op=Alu_pkg::DIVU;
            end
            'h6: begin
                state_out.alu_op=Alu_pkg::REM;
            end
            'h7: begin
                state_out.alu_op=Alu_pkg::REMU;
            end
            endcase
            state_out.funct3=_this._.r.funct3;
            state_out.rs1=_this._.r.rs1;
            state_out.rs2=_this._.r.rs2;
        end
    end
    endtask

    function logic[7:0] Rv32ia___funct5 (input Rv32ia _this);
        return unsigned'(8'((_this._.raw >>> 'h1B)));
    endfunction

    task Rv32ia___decode (
        input Rv32ia _this
,       output State state_out
    );
    begin: Rv32ia___decode
        state_out = 0;
        Rv32im___decode(_this, state_out);
        if (((((_this._.raw & 'h3)) == 'h3) && (_this._.r.opcode == 'h2F)) && (_this._.r.funct3 == 'h2)) begin
            state_out = 0;
            state_out.rd=_this._.r.rd;
            state_out.rs1=_this._.r.rs1;
            state_out.rs2=_this._.r.rs2;
            state_out.funct3=_this._.r.funct3;
            state_out.imm='h0;
            state_out.alu_op=Alu_pkg::ADD;
            case (Rv32ia___funct5(_this))
            Rv32ia_pkg::FUNCT5_LR: begin
                if (_this._.r.rs2 == 'h0) begin
                    state_out.amo_op=Amo_pkg::LR_W;
                    state_out.mem_op=Mem_pkg::LOAD;
                    state_out.wb_op=Wb_pkg::MEM;
                end
            end
            Rv32ia_pkg::FUNCT5_SC: begin
                state_out.amo_op=Amo_pkg::SC_W;
                state_out.mem_op=Mem_pkg::STORE;
                state_out.wb_op=Wb_pkg::ALU;
            end
            Rv32ia_pkg::FUNCT5_AMOSWAP: begin
                state_out.amo_op=Amo_pkg::AMOSWAP_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            Rv32ia_pkg::FUNCT5_AMOADD: begin
                state_out.amo_op=Amo_pkg::AMOADD_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            Rv32ia_pkg::FUNCT5_AMOXOR: begin
                state_out.amo_op=Amo_pkg::AMOXOR_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            Rv32ia_pkg::FUNCT5_AMOAND: begin
                state_out.amo_op=Amo_pkg::AMOAND_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            Rv32ia_pkg::FUNCT5_AMOOR: begin
                state_out.amo_op=Amo_pkg::AMOOR_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            Rv32ia_pkg::FUNCT5_AMOMIN: begin
                state_out.amo_op=Amo_pkg::AMOMIN_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            Rv32ia_pkg::FUNCT5_AMOMAX: begin
                state_out.amo_op=Amo_pkg::AMOMAX_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            Rv32ia_pkg::FUNCT5_AMOMINU: begin
                state_out.amo_op=Amo_pkg::AMOMINU_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            Rv32ia_pkg::FUNCT5_AMOMAXU: begin
                state_out.amo_op=Amo_pkg::AMOMAXU_W;
                state_out.mem_op=Mem_pkg::LOAD;
                state_out.wb_op=Wb_pkg::MEM;
            end
            default: begin
                state_out.sys_op=Sys_pkg::TRAP;
                state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                state_out.imm=_this._.raw;
                state_out.br_op=Br_pkg::BNONE;
            end
            endcase
            if (state_out.amo_op == Amo_pkg::AMONONE) begin
                state_out.sys_op=Sys_pkg::TRAP;
                state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                state_out.imm=_this._.raw;
                state_out.br_op=Br_pkg::BNONE;
            end
        end
    end
    endtask

    task Zicsr___decode (
        input Zicsr _this
,       output State state_out
    );
    begin: Zicsr___decode
        state_out = 0;
        Rv32ia___decode(_this, state_out);
        if (((((_this._.raw & 'h3)) == 'h3) && (_this._.r.opcode == 'h73)) && (_this._.i.funct3 == 'h0)) begin
            if (_this._.raw == 'h73) begin
                state_out = 0;
                state_out.sys_op=Sys_pkg::ECALL;
                state_out.imm=_this._.raw;
                state_out.br_op=Br_pkg::BNONE;
            end
            else begin
                if (_this._.raw == 'h100073) begin
                    state_out = 0;
                    state_out.sys_op=Sys_pkg::EBREAK;
                    state_out.trap_op=Trap_pkg::BREAKPOINT;
                    state_out.imm=_this._.raw;
                    state_out.br_op=Br_pkg::BNONE;
                end
                else begin
                    if (_this._.raw == 'h30200073) begin
                        state_out = 0;
                        state_out.sys_op=Sys_pkg::MRET;
                        state_out.imm='h0;
                        state_out.br_op=Br_pkg::BNONE;
                    end
                    else begin
                        if (_this._.raw == 'h10200073) begin
                            state_out = 0;
                            state_out.sys_op=Sys_pkg::SRET;
                            state_out.imm='h0;
                            state_out.br_op=Br_pkg::BNONE;
                        end
                        else begin
                            if (_this._.raw == 'h10500073) begin
                                state_out = 0;
                                state_out.sys_op=Sys_pkg::WFI;
                                state_out.imm=_this._.raw;
                            end
                            else begin
                                if (((_this._.raw & 'hFE007FFF)) == 'h12000073) begin
                                    state_out = 0;
                                    state_out.sys_op=Sys_pkg::SFENCE_VMA;
                                    state_out.rs1=_this._.r.rs1;
                                    state_out.rs2=_this._.r.rs2;
                                    state_out.imm=_this._.raw;
                                end
                                else begin
                                    state_out = 0;
                                    state_out.sys_op=Sys_pkg::TRAP;
                                    state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                                    state_out.imm=_this._.raw;
                                    state_out.br_op=Br_pkg::BNONE;
                                end
                            end
                        end
                    end
                end
            end
        end
        if (((((_this._.raw & 'h3)) == 'h3) && (_this._.r.opcode == 'h73)) && (_this._.i.funct3 != 'h0)) begin
            state_out = 0;
            state_out.rd=_this._.i.rd;
            state_out.funct3=_this._.i.funct3;
            state_out.csr_addr=_this._.i.imm11_0;
            state_out.csr_imm=_this._.i.rs1;
            state_out.imm=_this._.raw;
            case (_this._.i.funct3)
            'h1: begin
                state_out.csr_op=Csr_pkg::CSRRW;
                state_out.rs1=_this._.i.rs1;
                state_out.wb_op=(_this._.i.rd) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h2: begin
                state_out.csr_op=Csr_pkg::CSRRS;
                state_out.rs1=_this._.i.rs1;
                state_out.wb_op=(_this._.i.rd) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h3: begin
                state_out.csr_op=Csr_pkg::CSRRC;
                state_out.rs1=_this._.i.rs1;
                state_out.wb_op=(_this._.i.rd) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h5: begin
                state_out.csr_op=Csr_pkg::CSRRWI;
                state_out.wb_op=(_this._.i.rd) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h6: begin
                state_out.csr_op=Csr_pkg::CSRRSI;
                state_out.wb_op=(_this._.i.rd) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            'h7: begin
                state_out.csr_op=Csr_pkg::CSRRCI;
                state_out.wb_op=(_this._.i.rd) ? (Wb_pkg::ALU) : (Wb_pkg::WNONE);
            end
            default: begin
                state_out.sys_op=Sys_pkg::TRAP;
                state_out.trap_op=Trap_pkg::ILLEGAL_INST;
                state_out.imm=_this._.raw;
                state_out.br_op=Br_pkg::BNONE;
            end
            endcase
        end
    end
    endtask

    always_comb begin : state_comb_func  // state_comb_func
        Zicsr instr; instr = {instr_in};
        Zicsr___decode(instr, state_comb);
        if ((((instr._.raw & 'h3)) == 'h3) && (instr._.r.opcode == 'h17)) begin
            state_comb.rs1_val=pc_in;
        end
        state_comb.valid=instr_valid_in;
        state_comb.pc=pc_in;
        if (state_comb.rs1) begin
            state_comb.rs1_val=regs_data0_in;
        end
        if (state_comb.rs2) begin
            state_comb.rs2_val=regs_data1_in;
        end
    end

    always_comb begin : rs1_out_comb_func  // rs1_out_comb_func
        rs1_out_comb = state_comb.rs1;
    end

    always_comb begin : rs2_out_comb_func  // rs2_out_comb_func
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

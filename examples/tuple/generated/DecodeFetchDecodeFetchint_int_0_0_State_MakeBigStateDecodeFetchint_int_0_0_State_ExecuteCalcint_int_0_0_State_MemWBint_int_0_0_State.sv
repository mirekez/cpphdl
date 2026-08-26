`default_nettype none

import Predef_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import MemWBint_int_0_0_State_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_pkg::*;
import Instr_pkg::*;
import Mem_pkg::*;
import Alu_pkg::*;
import Wb_pkg::*;
import Br_pkg::*;


module DecodeFetchDecodeFetchint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
    parameter ID = 0
,   parameter LENGTH = 3
 )
 (
    input wire clk
,   input wire reset
,   input wire[31:0] pc_in
,   input wire instr_valid_in
,   input wire[31:0] instr_in
,   input wire[31:0] regs_data0_in
,   input wire[31:0] regs_data1_in
,   output wire[7:0] rs1_out
,   output wire[7:0] rs2_out
,   input wire[31:0] alu_result_in
,   input wire[31:0] mem_data_in
,   output wire stall_out
,   input wire MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[LENGTH-1:0] state_in
,   output wire DecodeFetchint_int_0_0_State[LENGTH - ID-1:0] state_out
);

    typedef DecodeFetchint_int_0_0_State STATE;

    // regs and combs
    DecodeFetchint_int_0_0_State state_comb;
;
    logic[7:0] rs1_out_comb;
;
    logic[7:0] rs2_out_comb;
;
    logic stall_comb;
;
    DecodeFetchint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg;

    // members

    // tmp variables
    DecodeFetchint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg_tmp;


    function logic signed[31:0] Instr___sext (
        input Instr _this
,       input logic[31:0] val
,       input logic[31:0] bits
    );
        logic signed[31:0] m;
        m = 'h1 <<< ((bits - 'h1));
        return ((val ^ m)) - m;
    endfunction

    function logic signed[31:0] Instr___imm_I (input Instr _this);
        return Instr___sext(_this, _this.i.imm11_0, 'hC);
    endfunction

    function logic signed[31:0] Instr___imm_S (input Instr _this);
        return Instr___sext(_this, _this.s.imm4_0 | ((_this.s.imm11_5 <<< 'h5)), 'hC);
    endfunction

    function logic signed[31:0] Instr___imm_B (input Instr _this);
        return Instr___sext(_this, ((((_this.b.imm4_1 <<< 'h1)) | ((_this.b.imm11 <<< 'hB))) | ((_this.b.imm10_5 <<< 'h5))) | ((_this.b.imm12 <<< 'hC)), 'hD);
    endfunction

    function logic signed[31:0] Instr___imm_J (input Instr _this);
        return Instr___sext(_this, ((((_this.j.imm10_1 <<< 'h1)) | ((_this.j.imm11 <<< 'hB))) | ((_this.j.imm19_12 <<< 'hC))) | ((_this.j.imm20 <<< 'h14)), 'h15);
    endfunction

    function logic signed[31:0] Instr___imm_U (input Instr _this);
        return signed'(32'(_this.u.imm31_12 <<< 'hC));
    endfunction

    task Instr___decode (
        input Instr _this
,       output DecodeFetchint_int_0_0_State state_out
    );
    begin: Instr___decode
        state_out = 0;
        if (_this.r.opcode == 'h3) begin
            state_out.rd=_this.i.rd;
            state_out.imm=Instr___imm_I(_this);
            state_out.mem_op=Mem_pkg::LOAD;
            state_out.alu_op=Alu_pkg::ADD;
            state_out.wb_op=Wb_pkg::MEM;
            state_out.funct3=_this.i.funct3;
            state_out.rs1=_this.i.rs1;
        end
        else begin
            if (_this.r.opcode == 'h23) begin
                state_out.imm=Instr___imm_S(_this);
                state_out.mem_op=Mem_pkg::STORE;
                state_out.alu_op=Alu_pkg::ADD;
                state_out.funct3=_this.s.funct3;
                state_out.rs1=_this.s.rs1;
                state_out.rs2=_this.s.rs2;
            end
            else begin
                if (_this.r.opcode == 'h13) begin
                    state_out.rd=_this.i.rd;
                    state_out.imm=Instr___imm_I(_this);
                    state_out.wb_op=Wb_pkg::ALU;
                    case (_this.i.funct3)
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
                        state_out.alu_op=(((_this.i.imm11_0 >>> 'hA)) & 'h1) ? (Alu_pkg::SRA) : (Alu_pkg::SRL);
                    end
                    endcase
                    state_out.funct3=_this.i.funct3;
                    state_out.rs1=_this.i.rs1;
                end
                else begin
                    if (_this.r.opcode == 'h33) begin
                        state_out.rd=_this.r.rd;
                        state_out.wb_op=Wb_pkg::ALU;
                        case (_this.r.funct3)
                        'h0: begin
                            state_out.alu_op=((_this.r.funct7 == 'h20)) ? (Alu_pkg::SUB) : ((((_this.r.funct7 == 'h1)) ? (Alu_pkg::MUL) : (Alu_pkg::ADD)));
                        end
                        'h7: begin
                            state_out.alu_op=((_this.r.funct7 == 'h1)) ? (Alu_pkg::REM) : (Alu_pkg::AND);
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
                            state_out.alu_op=((_this.r.funct7 == 'h20)) ? (Alu_pkg::SRA) : ((((_this.r.funct7 == 'h1)) ? (Alu_pkg::DIV) : (Alu_pkg::SRL)));
                        end
                        'h2: begin
                            state_out.alu_op=Alu_pkg::SLT;
                        end
                        'h3: begin
                            state_out.alu_op=((_this.r.funct7 == 'h1)) ? (Alu_pkg::MULH) : (Alu_pkg::SLTU);
                        end
                        endcase
                        state_out.funct3=_this.r.funct3;
                        state_out.rs1=_this.r.rs1;
                        state_out.rs2=_this.r.rs2;
                    end
                    else begin
                        if (_this.r.opcode == 'h63) begin
                            state_out.imm=Instr___imm_B(_this);
                            state_out.br_op=Br_pkg::BNONE;
                            case (_this.b.funct3)
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
                            state_out.funct3=_this.b.funct3;
                            state_out.rs1=_this.b.rs1;
                            state_out.rs2=_this.b.rs2;
                        end
                        else begin
                            if (_this.r.opcode == 'h6F) begin
                                state_out.rd=_this.j.rd;
                                state_out.imm=Instr___imm_J(_this);
                                state_out.br_op=Br_pkg::JAL;
                                state_out.wb_op=Wb_pkg::PC4;
                            end
                            else begin
                                if (_this.r.opcode == 'h67) begin
                                    state_out.rd=_this.i.rd;
                                    state_out.imm=Instr___imm_I(_this);
                                    state_out.br_op=Br_pkg::JALR;
                                    state_out.wb_op=Wb_pkg::PC4;
                                    state_out.rs1=_this.i.rs1;
                                end
                                else begin
                                    if (_this.r.opcode == 'h37) begin
                                        state_out.rd=_this.u.rd;
                                        state_out.imm=Instr___imm_U(_this);
                                        state_out.alu_op=Alu_pkg::PASS;
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (_this.r.opcode == 'h17) begin
                                            state_out.rd=_this.u.rd;
                                            state_out.imm=Instr___imm_U(_this);
                                            state_out.alu_op=Alu_pkg::ADD;
                                            state_out.wb_op=Wb_pkg::ALU;
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
,       input logic signed[31:0] hi
,       input logic signed[31:0] lo
    );
        return ((_this.raw >>> lo)) & (((('h1 <<< (((hi - lo) + 'h1)))) - 'h1));
    endfunction

    function logic[31:0] Instr___bit (
        input Instr _this
,       input logic signed[31:0] lo
    );
        return ((_this.raw >>> lo)) & 'h1;
    endfunction

    task Instr___decode16 (
        input Instr _this
,       output DecodeFetchint_int_0_0_State state_out
    );
    begin: Instr___decode16
        logic signed[31:0] imm_tmp;
        state_out = 0;
        state_out.funct3='h2;
        if (_this.c.opcode == 'h0) begin
            if (_this.c.funct3 == 'h0) begin
                state_out.rd=_this.c.rd_p + 'h8;
                state_out.rs1='h2;
                state_out.imm=(((Instr___bits(_this, 'hA, 'h7) <<< 'h6)) | ((Instr___bits(_this, 'hC, 'hB) <<< 'h4))) | ((Instr___bits(_this, 'h6, 'h5) <<< 'h2));
                state_out.alu_op=Alu_pkg::ADD;
                state_out.wb_op=Wb_pkg::ALU;
            end
            else begin
                if (_this.c.funct3 == 'h2) begin
                    state_out.rd=_this.c.rd_p + 'h8;
                    state_out.rs1=_this.c.rs1_p + 'h8;
                    state_out.imm=(((Instr___bit(_this, 'h5) <<< 'h6)) | ((Instr___bits(_this, 'hC, 'hA) <<< 'h3))) | ((Instr___bit(_this, 'h6) <<< 'h2));
                    state_out.alu_op=Alu_pkg::ADD;
                    state_out.mem_op=Mem_pkg::LOAD;
                    state_out.wb_op=Wb_pkg::MEM;
                end
                else begin
                    if (_this.c.funct3 == 'h6) begin
                        state_out.rs1=_this.c.rs1_p + 'h8;
                        state_out.rs2=_this.c.rd_p + 'h8;
                        state_out.imm=(((Instr___bit(_this, 'h5) <<< 'h6)) | ((Instr___bits(_this, 'hC, 'hA) <<< 'h3))) | ((Instr___bit(_this, 'h6) <<< 'h2));
                        state_out.alu_op=Alu_pkg::ADD;
                        state_out.mem_op=Mem_pkg::STORE;
                    end
                end
            end
        end
        else begin
            if (_this.c.opcode == 'h1) begin
                if (_this.c.funct3 == 'h0) begin
                    state_out.rd=_this.q1.rs1;
                    state_out.rs1=_this.q1.rs1;
                    imm_tmp=((Instr___bit(_this, 'hC) <<< 'h5)) | Instr___bits(_this, 'h6, 'h2);
                    imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                    state_out.imm=imm_tmp;
                    state_out.alu_op=Alu_pkg::ADD;
                    state_out.wb_op=Wb_pkg::ALU;
                end
                else begin
                    if (_this.c.funct3 == 'h1) begin
                        state_out.rd='h1;
                        state_out.wb_op=Wb_pkg::PC2;
                        state_out.br_op=Br_pkg::JAL;
                        state_out.imm=((((((((_this.c.b12 <<< 'hB)) | ((Instr___bit(_this, 'h8) <<< 'hA))) | ((Instr___bits(_this, 'hA, 'h9) <<< 'h8))) | ((Instr___bit(_this, 'h6) <<< 'h7))) | ((Instr___bit(_this, 'h7) <<< 'h6))) | ((Instr___bit(_this, 'h2) <<< 'h5))) | ((Instr___bit(_this, 'hB) <<< 'h4))) | ((Instr___bits(_this, 'h5, 'h3) <<< 'h1));
                    end
                    else begin
                        if (_this.c.funct3 == 'h2) begin
                            state_out.rd=_this.q1.rs1;
                            imm_tmp=((Instr___bit(_this, 'hC) <<< 'h5)) | Instr___bits(_this, 'h6, 'h2);
                            imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                            state_out.imm=imm_tmp;
                            state_out.alu_op=Alu_pkg::PASS;
                            state_out.wb_op=Wb_pkg::ALU;
                        end
                        else begin
                            if (_this.c.funct3 == 'h3) begin
                                state_out.rd='h2;
                                state_out.rs1='h2;
                                imm_tmp=((((((Instr___bit(_this, 'hC) <<< 'h9)) | ((Instr___bit(_this, 'h4) <<< 'h8))) | ((Instr___bit(_this, 'h3) <<< 'h7))) | ((Instr___bit(_this, 'h5) <<< 'h6))) | ((Instr___bit(_this, 'h2) <<< 'h5))) | ((Instr___bit(_this, 'h6) <<< 'h4));
                                imm_tmp=((imm_tmp <<< 'h16)) >>> 'h16;
                                state_out.imm=imm_tmp;
                                state_out.alu_op=Alu_pkg::ADD;
                                state_out.wb_op=Wb_pkg::ALU;
                            end
                            else begin
                                if (_this.c.funct3 == 'h4) begin
                                    if (_this.c.bits11_10 == 'h0) begin
                                        state_out.rd=_this.c.rs1_p + 'h8;
                                        state_out.rs1=_this.c.rs1_p + 'h8;
                                        state_out.imm=Instr___bits(_this, 'h6, 'h2);
                                        state_out.alu_op=Alu_pkg::SRL;
                                        state_out.wb_op=Wb_pkg::ALU;
                                    end
                                    else begin
                                        if (_this.c.bits11_10 == 'h1) begin
                                            state_out.rd=_this.c.rs1_p + 'h8;
                                            state_out.rs1=_this.c.rs1_p + 'h8;
                                            state_out.imm=Instr___bits(_this, 'h6, 'h2);
                                            state_out.alu_op=Alu_pkg::SRA;
                                            state_out.wb_op=Wb_pkg::ALU;
                                        end
                                        else begin
                                            if (_this.c.bits11_10 == 'h2) begin
                                                state_out.rd=_this.c.rs1_p + 'h8;
                                                state_out.rs1=_this.c.rs1_p + 'h8;
                                                imm_tmp=((Instr___bit(_this, 'hC) <<< 'h5)) | Instr___bits(_this, 'h6, 'h2);
                                                imm_tmp=((imm_tmp <<< 'h1A)) >>> 'h1A;
                                                state_out.imm=imm_tmp;
                                                state_out.alu_op=Alu_pkg::AND;
                                                state_out.wb_op=Wb_pkg::ALU;
                                            end
                                            else begin
                                                if ((_this.c.bits11_10 == 'h3) && (_this.c.b12 == 'h0)) begin
                                                    state_out.rd=_this.q2.rs1;
                                                    state_out.rs1=_this.q2.rs1;
                                                    state_out.rs2=_this.q2.rs2;
                                                    state_out.alu_op=(_this.c.bits6_5 == 'h0) ? (Alu_pkg::SUB) : (((_this.c.bits6_5 == 'h1) ? (Alu_pkg::XOR) : (((_this.c.bits6_5 == 'h2) ? (Alu_pkg::OR) : (Alu_pkg::AND)))));
                                                    state_out.wb_op=Wb_pkg::ALU;
                                                end
                                            end
                                        end
                                    end
                                end
                                else begin
                                    if (_this.c.funct3 == 'h5) begin
                                        state_out.rd='h0;
                                        state_out.br_op=Br_pkg::JAL;
                                        state_out.imm=((((((((_this.c.b12 <<< 'hB)) | ((Instr___bit(_this, 'h8) <<< 'hA))) | ((Instr___bits(_this, 'hA, 'h9) <<< 'h8))) | ((Instr___bit(_this, 'h6) <<< 'h7))) | ((Instr___bit(_this, 'h7) <<< 'h6))) | ((Instr___bit(_this, 'h2) <<< 'h5))) | ((Instr___bit(_this, 'hB) <<< 'h4))) | ((Instr___bits(_this, 'h5, 'h3) <<< 'h1));
                                    end
                                    else begin
                                        if (_this.c.funct3 == 'h6) begin
                                            state_out.rs1=_this.c.rs1_p + 'h8;
                                            state_out.br_op=Br_pkg::BEQZ;
                                            state_out.alu_op=Alu_pkg::SLTU;
                                            state_out.imm=(((((_this.c.b12 <<< 'h8)) | ((Instr___bits(_this, 'h6, 'h5) <<< 'h6))) | ((Instr___bit(_this, 'h2) <<< 'h5))) | ((Instr___bits(_this, 'hB, 'hA) <<< 'h3))) | ((Instr___bits(_this, 'h4, 'h3) <<< 'h1));
                                            if (_this.c.b12) begin
                                                state_out.imm|=~'h1FF;
                                            end
                                        end
                                        else begin
                                            if (_this.c.funct3 == 'h7) begin
                                                state_out.rs1=_this.c.rs1_p + 'h8;
                                                state_out.br_op=Br_pkg::BNEZ;
                                                state_out.alu_op=Alu_pkg::SLTU;
                                                state_out.imm=(((((_this.c.b12 <<< 'h8)) | ((Instr___bits(_this, 'h6, 'h5) <<< 'h6))) | ((Instr___bit(_this, 'h2) <<< 'h5))) | ((Instr___bits(_this, 'hB, 'hA) <<< 'h3))) | ((Instr___bits(_this, 'h4, 'h3) <<< 'h1));
                                                if (_this.c.b12) begin
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
                if (_this.c.opcode == 'h2) begin
                    if (_this.c.funct3 == 'h0) begin
                        state_out.rd=_this.q2.rs1;
                        state_out.rs1=_this.q2.rs1;
                        state_out.imm=((_this.c.b12 <<< 'h5)) | Instr___bits(_this, 'h6, 'h2);
                        state_out.alu_op=Alu_pkg::SLL;
                        state_out.wb_op=Wb_pkg::ALU;
                    end
                    else begin
                        if (_this.c.funct3 == 'h2) begin
                            state_out.rd=_this.q2.rs1;
                            state_out.rs1='h2;
                            state_out.imm=(((_this.c.b12 <<< 'h5)) | ((Instr___bits(_this, 'h6, 'h4) <<< 'h2))) | ((Instr___bits(_this, 'h3, 'h2) <<< 'h6));
                            state_out.alu_op=Alu_pkg::ADD;
                            state_out.mem_op=Mem_pkg::LOAD;
                            state_out.wb_op=Wb_pkg::MEM;
                        end
                        else begin
                            if (_this.c.funct3 == 'h4) begin
                                if (_this.q2.rs2 != 'h0) begin
                                    state_out.rd=_this.q2.rs1;
                                    state_out.rs1=_this.q2.rs1;
                                    state_out.rs2=_this.q2.rs2;
                                    state_out.alu_op=(_this.c.b12 == 'h0) ? (Alu_pkg::PASS) : (Alu_pkg::ADD);
                                    state_out.wb_op=Wb_pkg::ALU;
                                end
                                else begin
                                    if ((_this.q2.rs2 == 'h0) && (_this.c.b12 == 'h0)) begin
                                        state_out.rs1=_this.q2.rs1;
                                        state_out.br_op=Br_pkg::JR;
                                        state_out.wb_op=Wb_pkg::PC2;
                                    end
                                    else begin
                                        if ((_this.q2.rs2 == 'h0) && (_this.c.b12 == 'h1)) begin
                                            state_out.rs1=_this.q2.rs2;
                                            state_out.rd='h1;
                                            state_out.br_op=Br_pkg::JALR;
                                            state_out.wb_op=Wb_pkg::PC2;
                                        end
                                    end
                                end
                            end
                            else begin
                                if (_this.c.funct3 == 'h6) begin
                                    state_out.rs1='h2;
                                    state_out.rs2=_this.q2.rs2;
                                    state_out.imm=((Instr___bits(_this, 'h8, 'h7) <<< 'h6)) | ((Instr___bits(_this, 'hC, 'h9) <<< 'h2));
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

    always_comb begin : state_comb_func  // state_comb_func
        Instr instr;
        instr = '{instr_in};
        if (((instr.raw & 'h3)) == 'h3) begin
            Instr___decode(instr, state_comb);
            if (instr.r.opcode == 'h17) begin
                state_comb.rs1_val=pc_in;
            end
        end
        else begin
            Instr___decode16(instr, state_comb);
        end
        state_comb.valid=instr_valid_in;
        state_comb.pc=pc_in;
    end

    always_comb begin : rs1_out_comb_func  // rs1_out_comb_func
        rs1_out_comb=state_comb.rs1;
    end

    always_comb begin : rs2_out_comb_func  // rs2_out_comb_func
        rs2_out_comb=state_comb.rs2;
    end

    always_comb begin : stall_comb_func  // stall_comb_func
        stall_comb=0;
        if ((PipelineStage___state_reg['h0].valid && (PipelineStage___state_reg['h0].wb_op == Wb_pkg::MEM)) && (PipelineStage___state_reg['h0].rd != 'h0)) begin
            if (PipelineStage___state_reg['h0].rd == state_comb.rs1) begin
                stall_comb=1;
            end
            if (PipelineStage___state_reg['h0].rd == state_comb.rs2) begin
                stall_comb=1;
            end
        end
        if ((PipelineStage___state_reg['h0].valid && (PipelineStage___state_reg['h0].br_op != Br_pkg::BNONE))) begin
            stall_comb=1;
        end
    end

    task do_decode_fetch ();
    begin: do_decode_fetch
        if (state_comb.rs1) begin
            state_comb.rs1_val=regs_data0_in;
        end
        if (state_comb.rs2) begin
            state_comb.rs2_val=regs_data1_in;
        end
        if ((PipelineStage___state_reg['h1].valid && (PipelineStage___state_reg['h1].wb_op == Wb_pkg::ALU)) && (PipelineStage___state_reg['h1].rd != 'h0)) begin
            if (PipelineStage___state_reg['h1].rd == state_comb.rs1) begin
                state_comb.rs1_val=state_in[ID + 'h1].alu_result;
            end
            if (PipelineStage___state_reg['h1].rd == state_comb.rs2) begin
                state_comb.rs2_val=state_in[ID + 'h1].alu_result;
            end
        end
        if ((PipelineStage___state_reg['h0].valid && (PipelineStage___state_reg['h0].wb_op == Wb_pkg::ALU)) && (PipelineStage___state_reg['h0].rd != 'h0)) begin
            if (PipelineStage___state_reg['h0].rd == state_comb.rs1) begin
                state_comb.rs1_val=alu_result_in;
            end
            if (PipelineStage___state_reg['h0].rd == state_comb.rs2) begin
                state_comb.rs2_val=alu_result_in;
            end
        end
        if ((PipelineStage___state_reg['h1].valid && (PipelineStage___state_reg['h1].wb_op == Wb_pkg::MEM)) && (PipelineStage___state_reg['h1].rd != 'h0)) begin
            if (PipelineStage___state_reg['h1].rd == state_comb.rs1) begin
                state_comb.rs1_val=mem_data_in;
            end
            if (PipelineStage___state_reg['h1].rd == state_comb.rs2) begin
                state_comb.rs2_val=mem_data_in;
            end
        end
        PipelineStage___state_reg_tmp['h0] = state_comb;
        PipelineStage___state_reg_tmp['h0].valid=instr_valid_in && !stall_comb;
    end
    endtask

    task PipelineStage____work (input logic reset);
    begin: PipelineStage____work
        logic[63:0] i;
        for (i='h1;i < (LENGTH - ID);i=i+1) begin
            PipelineStage___state_reg_tmp[i] = PipelineStage___state_reg[i - 'h1];
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            PipelineStage___state_reg_tmp['h0].valid='h0;
            PipelineStage___state_reg_tmp['h1].valid='h0;
        end
        PipelineStage____work(reset);
        do_decode_fetch();
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        PipelineStage___state_reg_tmp = PipelineStage___state_reg;

        _work(reset);

        PipelineStage___state_reg <= PipelineStage___state_reg_tmp;
    end

    assign rs1_out = rs1_out_comb;

    assign rs2_out = rs2_out_comb;

    assign stall_out = stall_comb;

    assign state_out = PipelineStage___state_reg;


endmodule

`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Alu_pkg::*;
import Mem_pkg::*;
import Br_pkg::*;


module Execute (
    input wire clk
,   input wire reset
,   input State state_in
,   output wire[31:0] alu_result_out
,   output wire[31:0] debug_alu_a_out
,   output wire[31:0] debug_alu_b_out
,   output wire branch_taken_out
,   output wire[31:0] branch_target_out
);


    // regs and combs
    logic[31:0] alu_a_comb;
;
    logic[31:0] alu_b_comb;
;
    logic[63:0] alu_result_comb;
;
    logic branch_taken_comb;
;
    logic[31:0] branch_target_comb;
;

    // members
    genvar gi, gj, gk;

    // tmp variables


    always_comb begin : alu_a_comb_func  // alu_a_comb_func
        alu_a_comb=state_in.rs1_val;
        disable alu_a_comb_func;
    end

    always_comb begin : alu_b_comb_func  // alu_b_comb_func
        alu_b_comb=(((state_in.alu_op == Alu_pkg::ADD) && (state_in.mem_op != Mem_pkg::MNONE))) ? (unsigned'(32'(state_in.imm))) : ((((state_in.br_op != Br_pkg::BNONE) || state_in.rs2)) ? (state_in.rs2_val) : (unsigned'(32'(state_in.imm))));
        disable alu_b_comb_func;
    end

    always_comb begin : alu_result_comb_func  // alu_result_comb_func
        logic[31:0] a;
        logic[31:0] b;
        logic[31:0] alu_op;
        a=alu_a_comb;
        b=alu_b_comb;
        alu_result_comb='h0;
        alu_op=state_in.alu_op;
        case (alu_op)
        Alu_pkg::ADD: begin
            alu_result_comb=a + b;
        end
        Alu_pkg::SUB: begin
            alu_result_comb=a - b;
        end
        Alu_pkg::AND: begin
            alu_result_comb=a & b;
        end
        Alu_pkg::OR: begin
            alu_result_comb=a | b;
        end
        Alu_pkg::XOR: begin
            alu_result_comb=a ^ b;
        end
        Alu_pkg::SLL: begin
            alu_result_comb=a <<< ((b & 'h1F));
        end
        Alu_pkg::SRL: begin
            alu_result_comb=a >>> ((b & 'h1F));
        end
        Alu_pkg::SRA: begin
            alu_result_comb=unsigned'(32'(signed'(32'(a)) >>> ((b & 'h1F))));
        end
        Alu_pkg::SLT: begin
            alu_result_comb=(signed'(32'(a)) < signed'(32'(b)));
        end
        Alu_pkg::SLTU: begin
            alu_result_comb=(a < b);
        end
        Alu_pkg::PASS: begin
            alu_result_comb=b;
        end
        Alu_pkg::MUL: begin
            alu_result_comb=a*b;
        end
        Alu_pkg::MULH: begin
            alu_result_comb=(unsigned'(64'((signed'(64'(signed'(32'(a))))*signed'(64'(signed'(32'(b))))))) >>> 'h20);
        end
        Alu_pkg::MULHSU: begin
            alu_result_comb=(unsigned'(64'((signed'(64'(signed'(32'(a))))*signed'(64'(unsigned'(64'(b))))))) >>> 'h20);
        end
        Alu_pkg::MULHU: begin
            alu_result_comb=((unsigned'(64'(a))*b)) >>> 'h20;
        end
        Alu_pkg::DIV: begin
            alu_result_comb=(b == 'h0) ? (~'h0) : ((((a == 'h80000000) && (b == 'hFFFFFFFF)) ? (a) : (unsigned'(32'(signed'(32'(a))/signed'(32'(b)))))));
        end
        Alu_pkg::DIVU: begin
            alu_result_comb=(b == 'h0) ? (~'h0) : (a/b);
        end
        Alu_pkg::REM: begin
            alu_result_comb=(b == 'h0) ? (a) : ((((a == 'h80000000) && (b == 'hFFFFFFFF)) ? ('h0) : (unsigned'(32'(signed'(32'(a)) % signed'(32'(b)))))));
        end
        Alu_pkg::REMU: begin
            alu_result_comb=(b == 'h0) ? (a) : (a % b);
        end
        Alu_pkg::ANONE: begin
        end
        endcase
        if ((alu_op == Alu_pkg::SLT) || (alu_op == Alu_pkg::SLTU)) begin
            alu_result_comb|=(unsigned'(64'(((a == b))))) <<< 'h20;
        end
        disable alu_result_comb_func;
    end

    always_comb begin : branch_taken_comb_func  // branch_taken_comb_func
        logic[63:0] alu_result;
        alu_result=alu_result_comb;
        branch_taken_comb=0;
        case (state_in.br_op)
        Br_pkg::BEQZ: begin
            branch_taken_comb=alu_result >>> 'h20;
        end
        Br_pkg::BNEZ: begin
            branch_taken_comb=!(alu_result >>> 'h20);
        end
        Br_pkg::BEQ: begin
            branch_taken_comb=alu_result >>> 'h20;
        end
        Br_pkg::BNE: begin
            branch_taken_comb=!(alu_result >>> 'h20);
        end
        Br_pkg::BLT: begin
            branch_taken_comb=alu_result & 'hFFFFFFFF;
        end
        Br_pkg::BGE: begin
            branch_taken_comb=!(alu_result & 'hFFFFFFFF);
        end
        Br_pkg::BLTU: begin
            branch_taken_comb=alu_result & 'hFFFFFFFF;
        end
        Br_pkg::BGEU: begin
            branch_taken_comb=!(alu_result & 'hFFFFFFFF);
        end
        Br_pkg::JAL: begin
            branch_taken_comb=1;
        end
        Br_pkg::JALR: begin
            branch_taken_comb=1;
        end
        Br_pkg::JR: begin
            branch_taken_comb=1;
        end
        Br_pkg::BNONE: begin
        end
        endcase
        branch_taken_comb=branch_taken_comb && state_in.valid;
        disable branch_taken_comb_func;
    end

    always_comb begin : branch_target_comb_func  // branch_target_comb_func
        branch_target_comb='h0;
        if (state_in.br_op != Br_pkg::BNONE) begin
            if (state_in.br_op == Br_pkg::JAL) begin
                branch_target_comb=state_in.pc + state_in.imm;
            end
            else begin
                if ((state_in.br_op == Br_pkg::JALR) || (state_in.br_op == Br_pkg::JR)) begin
                    branch_target_comb=((state_in.rs1_val + state_in.imm)) & ~'h1;
                end
                else begin
                    branch_target_comb=state_in.pc + state_in.imm;
                end
            end
        end
        disable branch_target_comb_func;
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign alu_result_out = unsigned'(32'(alu_result_comb));

    assign debug_alu_a_out = alu_a_comb;

    assign debug_alu_b_out = alu_b_comb;

    assign branch_taken_out = branch_taken_comb;

    assign branch_target_out = branch_target_comb;


endmodule

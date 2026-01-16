`default_nettype none

import Predef_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_pkg::*;
import MemWBint_int_0_0_State_pkg::*;


module ExecuteCalcExecuteCalcint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
    parameter ID
,   parameter LENGTH
 )
 (
    input wire clk
,   input wire reset
,   output wire mem_write_out
,   output logic[31:0] mem_write_addr_out
,   output logic[31:0] mem_write_data_out
,   output logic[7:0] mem_write_mask_out
,   output wire mem_read_out
,   output logic[31:0] mem_read_addr_out
,   output logic[63:0] alu_result_out
,   output wire branch_taken_out
,   output logic[31:0] branch_target_out
,   input MakeBigStateDecodeFetchint_int_0_0_State[3-1:0] state_in
,   output ExecuteCalcint_int_0_0_State[2-1:0] state_out
);

    logic branch_taken_comb;
    logic[31:0] branch_target_comb;
    logic[31:0] alu_a_comb;
    logic[31:0] alu_b_comb;
    logic[63:0] alu_result_comb;
    reg[31:0] mem_addr_reg;
    reg[31:0] mem_data_reg;
    reg[7:0] mem_mask_reg;
    reg mem_write_reg;
    reg mem_read_reg;
    ExecuteCalcint_int_0_0_State[2-1:0] state_reg;
    logic[63:0] i;

    always @(*) begin
        alu_a_comb = state_in[ID - 1].rs1_val;
    end

    always @(*) begin
        alu_b_comb = (state_in[ID - 1].alu_op == ADD && state_in[ID - 1].mem_op != MNONE) ? state_in[ID - 1].imm : (state_in[ID - 1].rs2 || state_in[ID - 1].br_op == BEQZ || state_in[ID - 1].br_op == BNEZ) ? state_in[ID - 1].rs2_val : state_in[ID - 1].imm;
    end

    logic[31:0] a;
    logic[31:0] b;
    logic[31:0] alu_op;
    always @(*) begin
        a = alu_a_comb;
        b = alu_b_comb;
        alu_result_comb = 0;
        alu_op = state_in[ID - 1].alu_op;
        case (alu_op)
        ADD: begin
            alu_result_comb = a + b;
        end
        SUB: begin
            alu_result_comb = a - b;
        end
        AND: begin
            alu_result_comb = a & b;
        end
        OR: begin
            alu_result_comb = a | b;
        end
        XOR: begin
            alu_result_comb = a ^ b;
        end
        SLL: begin
            alu_result_comb = a << (b & 31);
        end
        SRL: begin
            alu_result_comb = a >> (b & 31);
        end
        SRA: begin
            alu_result_comb = a >> (b & 31);
        end
        SLT: begin
            alu_result_comb = (a < b);
        end
        SLTU: begin
            alu_result_comb = (a < b);
        end
        PASS: begin
            alu_result_comb = b;
        end
        MUL: begin
            alu_result_comb = a*b;
        end
        MULH: begin
            alu_result_comb = (a*b) >> 32;
        end
        DIV: begin
            alu_result_comb = a/b;
        end
        REM: begin
            alu_result_comb = a % b;
        end
        ANONE: begin
        end
        endcase
        if (alu_op == SLT || alu_op == SLTU) begin
            alu_result_comb |= ((a == b)) << 32;
        end
    end

    logic[63:0] alu_result;
    always @(*) begin
        alu_result = alu_result_comb;
        branch_taken_comb = 0;
        case (state_in[ID - 1].br_op)
        BEQZ: begin
            branch_taken_comb = alu_result >> 32;
        end
        BNEZ: begin
            branch_taken_comb = !(alu_result >> 32);
        end
        BEQ: begin
            branch_taken_comb = alu_result >> 32;
        end
        BNE: begin
            branch_taken_comb = !(alu_result >> 32);
        end
        BLT: begin
            branch_taken_comb = alu_result & -1;
        end
        BGE: begin
            branch_taken_comb = !(alu_result & -1);
        end
        BLTU: begin
            branch_taken_comb = alu_result & -1;
        end
        BGEU: begin
            branch_taken_comb = !(alu_result & -1);
        end
        JAL: begin
            branch_taken_comb = 1;
        end
        JALR: begin
            branch_taken_comb = 1;
        end
        JR: begin
            branch_taken_comb = 1;
        end
        BNONE: begin
        end
        endcase
        branch_taken_comb &= state_in[ID - 1].valid;
    end

    always @(*) begin
        branch_target_comb = 0;
        if (state_in[ID - 1].br_op != BNONE) begin
            if (state_in[ID - 1].br_op == JAL) begin
                branch_target_comb = state_in[ID - 1].pc + state_in[ID - 1].imm;
            end
            else begin
                if (state_in[ID - 1].br_op == JALR || state_in[ID - 1].br_op == JR) begin
                    branch_target_comb = (state_in[ID - 1].rs1_val + state_in[ID - 1].imm) & ~1;
                end
                else begin
                    branch_target_comb = state_in[ID - 1].pc + state_in[ID - 1].imm;
                end
            end
        end
    end

    task do_execute ();
    begin: do_execute
        state_reg[0].alu_result = alu_result_comb;
        state_reg[0].debug_alu_a = alu_a_comb;
        state_reg[0].debug_alu_b = alu_b_comb;
        state_reg[0].debug_branch_target = branch_target_comb;
        state_reg[0].debug_branch_taken = branch_taken_comb;
    end
    endtask

    task start_memory ();
    begin: start_memory
        mem_addr_reg = alu_result_comb;
        mem_data_reg = state_in[ID - 1].rs2_val;
        mem_write_reg = 0;
        mem_mask_reg = 0;
        if (state_in[ID - 1].mem_op == STORE && state_in[ID - 1].valid) begin
            case (state_in[ID - 1].funct3)
            0: begin
                mem_write_reg = state_in[ID - 1].valid;
            end
            1: begin
                mem_write_reg = state_in[ID - 1].valid;
            end
            2: begin
                mem_write_reg = state_in[ID - 1].valid;
            end
            endcase
        end
        mem_read_reg = 0;
        if (state_in[ID - 1].mem_op == LOAD && state_in[ID - 1].valid) begin
            case (state_in[ID - 1].funct3)
            0: begin
                mem_read_reg = 1;
            end
            1: begin
                mem_read_reg = 1;
            end
            2: begin
                mem_read_reg = 1;
            end
            4: begin
                mem_read_reg = 1;
            end
            5: begin
                mem_read_reg = 1;
            end
            default: begin
            end
            endcase
        end
    end
    endtask

    task work (input logic reset);
    begin: work
        if (reset) begin
            mem_write_reg = '0;
            mem_read_reg = '0;
        end
        state_reg[0] = 0;
        do_execute();
        start_memory();
    end
    endtask


    always @(posedge clk) begin
        work(reset);
    end

endmodule

`default_nettype none

import Predef_pkg::*;
import Alu_pkg::*;
import Br_pkg::*;
import Mem_pkg::*;
import State_pkg::*;


module Execute (
    input wire clk
,   input wire reset
,   input State state_in
,   output wire mem_write_out
,   output logic[31:0] mem_write_addr_out
,   output logic[31:0] mem_write_data_out
,   output logic[7:0] mem_write_mask_out
,   output wire mem_read_out
,   output logic[31:0] mem_read_addr_out
,   output logic[31:0] alu_result_out
,   output logic[31:0] debug_alu_a_out
,   output logic[31:0] debug_alu_b_out
,   output wire branch_taken_out
,   output logic[31:0] branch_target_out
);

    reg[31:0] mem_addr_reg;
    reg[31:0] mem_data_reg;
    reg[7:0] mem_mask_reg;
    reg mem_write_reg;
    reg mem_read_reg;
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


    reg[31:0] mem_addr_reg_next;
    reg[31:0] mem_data_reg_next;
    reg[7:0] mem_mask_reg_next;
    reg mem_write_reg_next;
    reg mem_read_reg_next;


    always @(*) begin  // alu_a_comb_func
        alu_a_comb = state_in.rs1_val;
    end

    always @(*) begin  // alu_b_comb_func
        alu_b_comb = (state_in.alu_op == Alu_pkg::ADD && state_in.mem_op != Mem_pkg::MNONE) ? unsigned'(32'(state_in.imm)) : (state_in.rs2 || state_in.br_op == Br_pkg::BEQZ || state_in.br_op == Br_pkg::BNEZ) ? state_in.rs2_val : unsigned'(32'(state_in.imm));
    end

    always @(*) begin  // alu_result_comb_func
        logic[31:0] a;
        logic[31:0] b;
        logic[31:0] alu_op;
        a = alu_a_comb;
        b = alu_b_comb;
        alu_result_comb = 0;
        alu_op = state_in.alu_op;
        case (alu_op)
        Alu_pkg::ADD: begin
            alu_result_comb = a + b;
        end
        Alu_pkg::SUB: begin
            alu_result_comb = a - b;
        end
        Alu_pkg::AND: begin
            alu_result_comb = a & b;
        end
        Alu_pkg::OR: begin
            alu_result_comb = a | b;
        end
        Alu_pkg::XOR: begin
            alu_result_comb = a ^ b;
        end
        Alu_pkg::SLL: begin
            alu_result_comb = a <<< (b & 31);
        end
        Alu_pkg::SRL: begin
            alu_result_comb = a >>> (b & 31);
        end
        Alu_pkg::SRA: begin
            alu_result_comb = unsigned'(32'(signed'(32'(a)) >>> (b & 31)));
        end
        Alu_pkg::SLT: begin
            alu_result_comb = (signed'(32'(a)) < signed'(32'(b)));
        end
        Alu_pkg::SLTU: begin
            alu_result_comb = (a < b);
        end
        Alu_pkg::PASS: begin
            alu_result_comb = b;
        end
        Alu_pkg::MUL: begin
            alu_result_comb = a*b;
        end
        Alu_pkg::MULH: begin
            alu_result_comb = (unsigned'(64'(a))*b) >>> 32;
        end
        Alu_pkg::DIV: begin
            alu_result_comb = a/b;
        end
        Alu_pkg::REM: begin
            alu_result_comb = a % b;
        end
        Alu_pkg::ANONE: begin
        end
        endcase
        if (alu_op == Alu_pkg::SLT || alu_op == Alu_pkg::SLTU) begin
            alu_result_comb |= (unsigned'(64'((a == b)))) <<< 32;
        end
    end

    always @(*) begin  // branch_taken_comb_func
        logic[63:0] alu_result;
        alu_result = alu_result_comb;
        branch_taken_comb = 0;
        case (state_in.br_op)
        Br_pkg::BEQZ: begin
            branch_taken_comb = alu_result >>> 32;
        end
        Br_pkg::BNEZ: begin
            branch_taken_comb = !(alu_result >>> 32);
        end
        Br_pkg::BEQ: begin
            branch_taken_comb = alu_result >>> 32;
        end
        Br_pkg::BNE: begin
            branch_taken_comb = !(alu_result >>> 32);
        end
        Br_pkg::BLT: begin
            branch_taken_comb = alu_result & 4294967295;
        end
        Br_pkg::BGE: begin
            branch_taken_comb = !(alu_result & 4294967295);
        end
        Br_pkg::BLTU: begin
            branch_taken_comb = alu_result & 4294967295;
        end
        Br_pkg::BGEU: begin
            branch_taken_comb = !(alu_result & 4294967295);
        end
        Br_pkg::JAL: begin
            branch_taken_comb = 1;
        end
        Br_pkg::JALR: begin
            branch_taken_comb = 1;
        end
        Br_pkg::JR: begin
            branch_taken_comb = 1;
        end
        Br_pkg::BNONE: begin
        end
        endcase
        branch_taken_comb = branch_taken_comb && state_in.valid;
    end

    always @(*) begin  // branch_target_comb_func
        branch_target_comb = 0;
        if (state_in.br_op != Br_pkg::BNONE) begin
            if (state_in.br_op == Br_pkg::JAL) begin
                branch_target_comb = state_in.pc + state_in.imm;
            end
            else begin
                if (state_in.br_op == Br_pkg::JALR || state_in.br_op == Br_pkg::JR) begin
                    branch_target_comb = (state_in.rs1_val + state_in.imm) & ~1;
                end
                else begin
                    branch_target_comb = state_in.pc + state_in.imm;
                end
            end
        end
    end

    task do_memory ();
    begin: do_memory
        mem_addr_reg_next = alu_result_comb;
        mem_data_reg_next = state_in.rs2_val;
        mem_write_reg_next = 0;
        mem_mask_reg_next = 0;
        if (state_in.mem_op == Mem_pkg::STORE && state_in.valid) begin
            case (state_in.funct3)
            0: begin
                mem_write_reg_next = state_in.valid;
                mem_mask_reg_next = 1;
            end
            1: begin
                mem_write_reg_next = state_in.valid;
                mem_mask_reg_next = 3;
            end
            2: begin
                mem_write_reg_next = state_in.valid;
                mem_mask_reg_next = 15;
            end
            endcase
        end
        mem_read_reg_next = 0;
        if (state_in.mem_op == Mem_pkg::LOAD && state_in.valid) begin
            case (state_in.funct3)
            0: begin
                mem_read_reg_next = 1;
            end
            1: begin
                mem_read_reg_next = 1;
            end
            2: begin
                mem_read_reg_next = 1;
            end
            4: begin
                mem_read_reg_next = 1;
            end
            5: begin
                mem_read_reg_next = 1;
            end
            default: begin
            end
            endcase
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        do_memory();
        if (reset) begin
            mem_write_reg_next = '0;
            mem_read_reg_next = '0;
        end
    end
    endtask

    generate  // _connect
    endgenerate

    always @(posedge clk) begin
        _work(reset);

        mem_addr_reg <= mem_addr_reg_next;
        mem_data_reg <= mem_data_reg_next;
        mem_mask_reg <= mem_mask_reg_next;
        mem_write_reg <= mem_write_reg_next;
        mem_read_reg <= mem_read_reg_next;
    end

    assign mem_write_out = mem_write_reg;

    assign mem_write_addr_out = mem_addr_reg;

    assign mem_write_data_out = mem_data_reg;

    assign mem_write_mask_out = mem_mask_reg;

    assign mem_read_out = mem_read_reg;

    assign mem_read_addr_out = mem_addr_reg;

    assign alu_result_out = unsigned'(32'(alu_result_comb));

    assign debug_alu_a_out = alu_a_comb;

    assign debug_alu_b_out = alu_b_comb;

    assign branch_taken_out = branch_taken_comb;

    assign branch_target_out = branch_target_comb;


endmodule

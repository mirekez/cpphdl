`default_nettype none

import Predef_pkg::*;
import State_pkg::*;
import Alu_pkg::*;
import Mem_pkg::*;
import Br_pkg::*;


module Execute (
    input wire clk
,   input wire l2_clock
,   input wire reset
,   input wire State state_in
,   input wire State multicycle_state_in
,   output wire[31:0] alu_result_out
,   output wire[31:0] mem_addr_out
,   output wire[31:0] debug_alu_a_out
,   output wire[31:0] debug_alu_b_out
,   output wire branch_taken_out
,   output wire[31:0] branch_target_out
,   output wire multicycle_wait_out
);


    // regs and combs
    reg div_busy_reg;
    reg div_done_reg;
    reg[6-1:0] div_count_reg;
    reg[32-1:0] div_dividend_reg;
    reg[32-1:0] div_divisor_reg;
    reg[32-1:0] div_quotient_reg;
    reg[33-1:0] div_remainder_reg;
    reg div_quotient_neg_reg;
    reg div_remainder_neg_reg;
    reg[32-1:0] div_result_reg;
    reg[32-1:0] div_pc_reg;
    reg[32-1:0] div_a_reg;
    reg[32-1:0] div_b_reg;
    reg[8-1:0] div_op_reg;
    reg mul_busy_reg;
    reg mul_done_reg;
    reg[6-1:0] mul_count_reg;
    reg[64-1:0] mul_multiplicand_reg;
    reg[32-1:0] mul_multiplier_reg;
    reg[64-1:0] mul_accumulator_reg;
    reg mul_negate_reg;
    reg mul_high_reg;
    reg[32-1:0] mul_result_reg;
    reg[32-1:0] mul_pc_reg;
    reg[32-1:0] mul_a_reg;
    reg[32-1:0] mul_b_reg;
    reg[8-1:0] mul_op_reg;
    logic div_instruction_comb;
    logic div_result_match_comb;
    logic mul_instruction_comb;
    logic mul_result_match_comb;
    logic multicycle_wait_comb;
    logic[31:0] alu_a_comb;
    logic[31:0] alu_b_comb;
    logic[31:0] mem_addr_comb;
    logic[63:0] alu_result_comb;
    logic branch_taken_comb;
    logic[31:0] branch_target_comb;

    // members

    // tmp variables
    logic div_busy_reg_tmp;
    logic div_done_reg_tmp;
    logic[6-1:0] div_count_reg_tmp;
    logic[32-1:0] div_dividend_reg_tmp;
    logic[32-1:0] div_divisor_reg_tmp;
    logic[32-1:0] div_quotient_reg_tmp;
    logic[33-1:0] div_remainder_reg_tmp;
    logic div_quotient_neg_reg_tmp;
    logic div_remainder_neg_reg_tmp;
    logic[32-1:0] div_result_reg_tmp;
    logic[32-1:0] div_pc_reg_tmp;
    logic[32-1:0] div_a_reg_tmp;
    logic[32-1:0] div_b_reg_tmp;
    logic[8-1:0] div_op_reg_tmp;
    logic mul_busy_reg_tmp;
    logic mul_done_reg_tmp;
    logic[6-1:0] mul_count_reg_tmp;
    logic[64-1:0] mul_multiplicand_reg_tmp;
    logic[32-1:0] mul_multiplier_reg_tmp;
    logic[64-1:0] mul_accumulator_reg_tmp;
    logic mul_negate_reg_tmp;
    logic mul_high_reg_tmp;
    logic[32-1:0] mul_result_reg_tmp;
    logic[32-1:0] mul_pc_reg_tmp;
    logic[32-1:0] mul_a_reg_tmp;
    logic[32-1:0] mul_b_reg_tmp;
    logic[8-1:0] mul_op_reg_tmp;


    always_comb begin : alu_a_comb_func  // alu_a_comb_func
        alu_a_comb=state_in.rs1_val;
    end

    always_comb begin : alu_b_comb_func  // alu_b_comb_func
        alu_b_comb=(((signed'(32'(state_in.alu_op)) == signed'(32'(Alu_pkg::ADD))) && (signed'(32'(state_in.mem_op)) != signed'(32'(Mem_pkg::MNONE))))) ? (state_in.imm) : ((((state_in.rs2) != '0)) ? (state_in.rs2_val) : (state_in.imm));
    end

    always_comb begin : alu_result_comb_func  // alu_result_comb_func
        logic[31:0] a;
        logic[31:0] b;
        logic[31:0] alu_op;
        a=alu_a_comb;
        b=alu_b_comb;
        alu_result_comb=64'h0;
        alu_op=state_in.alu_op;
        if (signed'(32'(state_in.br_op)) == signed'(32'(Br_pkg::BNONE))) begin
            case (alu_op)
            unsigned'(32'(Alu_pkg::ADD)): begin
                alu_result_comb=32'(a + b);
            end
            unsigned'(32'(Alu_pkg::SUB)): begin
                alu_result_comb=32'(a - b);
            end
            unsigned'(32'(Alu_pkg::AND)): begin
                alu_result_comb=32'(a & b);
            end
            unsigned'(32'(Alu_pkg::OR)): begin
                alu_result_comb=32'(a | b);
            end
            unsigned'(32'(Alu_pkg::XOR)): begin
                alu_result_comb=32'(a ^ b);
            end
            unsigned'(32'(Alu_pkg::SLL)): begin
                alu_result_comb=32'(a <<< ((b & 32'h1F)));
            end
            unsigned'(32'(Alu_pkg::SRL)): begin
                alu_result_comb=32'(a >>> ((b & 32'h1F)));
            end
            unsigned'(32'(Alu_pkg::SRA)): begin
                alu_result_comb=unsigned'(32'(signed'(32'(a)) >>> ((b & 32'h1F))));
            end
            unsigned'(32'(Alu_pkg::SLT)): begin
                alu_result_comb=(signed'(32'(a)) < signed'(32'(b)));
            end
            unsigned'(32'(Alu_pkg::SLTU)): begin
                alu_result_comb=(a < b);
            end
            unsigned'(32'(Alu_pkg::PASS)): begin
                alu_result_comb=b;
            end
            unsigned'(32'(Alu_pkg::MUL)): begin
                alu_result_comb=mul_result_reg;
            end
            unsigned'(32'(Alu_pkg::MULH)): begin
                alu_result_comb=mul_result_reg;
            end
            unsigned'(32'(Alu_pkg::MULHSU)): begin
                alu_result_comb=mul_result_reg;
            end
            unsigned'(32'(Alu_pkg::MULHU)): begin
                alu_result_comb=mul_result_reg;
            end
            unsigned'(32'(Alu_pkg::DIV)): begin
                alu_result_comb=div_result_reg;
            end
            unsigned'(32'(Alu_pkg::DIVU)): begin
                alu_result_comb=div_result_reg;
            end
            unsigned'(32'(Alu_pkg::REM)): begin
                alu_result_comb=div_result_reg;
            end
            unsigned'(32'(Alu_pkg::REMU)): begin
                alu_result_comb=div_result_reg;
            end
            unsigned'(32'(Alu_pkg::ANONE)): begin
            end
            endcase
        end
    end

    always_comb begin : mem_addr_comb_func  // mem_addr_comb_func
        mem_addr_comb=state_in.rs1_val + state_in.imm;
    end

    always_comb begin : branch_taken_comb_func  // branch_taken_comb_func
        logic[31:0] a;
        logic[31:0] b;
        logic signed_less;
        a=state_in.rs1_val;
        b=state_in.rs2_val;
        signed_less=(((((a ^ b)) >>> 32'sh1F)) != 32'h0) ? ((((a >>> 32'sh1F)) != 32'h0)) : (a < b);
        branch_taken_comb=0;
        case (signed'(32'(state_in.br_op)))
        signed'(32'(Br_pkg::BEQZ)): begin
            branch_taken_comb=a == 32'h0;
        end
        signed'(32'(Br_pkg::BNEZ)): begin
            branch_taken_comb=a != 32'h0;
        end
        signed'(32'(Br_pkg::BEQ)): begin
            branch_taken_comb=a == b;
        end
        signed'(32'(Br_pkg::BNE)): begin
            branch_taken_comb=a != b;
        end
        signed'(32'(Br_pkg::BLT)): begin
            branch_taken_comb=signed_less;
        end
        signed'(32'(Br_pkg::BGE)): begin
            branch_taken_comb=!signed_less;
        end
        signed'(32'(Br_pkg::BLTU)): begin
            branch_taken_comb=a < b;
        end
        signed'(32'(Br_pkg::BGEU)): begin
            branch_taken_comb=a>=b;
        end
        signed'(32'(Br_pkg::JAL)): begin
            branch_taken_comb=1;
        end
        signed'(32'(Br_pkg::JALR)): begin
            branch_taken_comb=1;
        end
        signed'(32'(Br_pkg::JR)): begin
            branch_taken_comb=1;
        end
        signed'(32'(Br_pkg::BNONE)): begin
        end
        endcase
        branch_taken_comb=branch_taken_comb && ((state_in.valid) != '0);
    end

    always_comb begin : branch_target_comb_func  // branch_target_comb_func
        branch_target_comb=32'h0;
        if (signed'(32'(state_in.br_op)) != signed'(32'(Br_pkg::BNONE))) begin
            if (signed'(32'(state_in.br_op)) == signed'(32'(Br_pkg::JAL))) begin
                branch_target_comb=state_in.pc + state_in.imm;
            end
            else begin
                if ((signed'(32'(state_in.br_op)) == signed'(32'(Br_pkg::JALR))) || (signed'(32'(state_in.br_op)) == signed'(32'(Br_pkg::JR)))) begin
                    branch_target_comb=((state_in.rs1_val + state_in.imm)) & ~'h1;
                end
                else begin
                    branch_target_comb=state_in.pc + state_in.imm;
                end
            end
        end
    end

    always_comb begin : div_instruction_comb_func  // div_instruction_comb_func
        logic[31:0] op;
        op=multicycle_state_in.alu_op;
        div_instruction_comb=((multicycle_state_in.valid) != '0) && (((((op == unsigned'(32'(Alu_pkg::DIV))) || (op == unsigned'(32'(Alu_pkg::DIVU)))) || (op == unsigned'(32'(Alu_pkg::REM)))) || (op == unsigned'(32'(Alu_pkg::REMU)))));
    end

    always_comb begin : div_result_match_comb_func  // div_result_match_comb_func
        div_result_match_comb=((((((div_done_reg) != '0) && div_instruction_comb) && (div_pc_reg == 64'(multicycle_state_in.pc))) && (div_a_reg == 64'(multicycle_state_in.rs1_val))) && (div_b_reg == 64'(multicycle_state_in.rs2_val))) && (div_op_reg == 64'(multicycle_state_in.alu_op));
    end

    always_comb begin : mul_instruction_comb_func  // mul_instruction_comb_func
        logic[31:0] op;
        op=multicycle_state_in.alu_op;
        mul_instruction_comb=((multicycle_state_in.valid) != '0) && (((((op == unsigned'(32'(Alu_pkg::MUL))) || (op == unsigned'(32'(Alu_pkg::MULH)))) || (op == unsigned'(32'(Alu_pkg::MULHSU)))) || (op == unsigned'(32'(Alu_pkg::MULHU)))));
    end

    always_comb begin : mul_result_match_comb_func  // mul_result_match_comb_func
        mul_result_match_comb=((((((mul_done_reg) != '0) && mul_instruction_comb) && (mul_pc_reg == 64'(multicycle_state_in.pc))) && (mul_a_reg == 64'(multicycle_state_in.rs1_val))) && (mul_b_reg == 64'(multicycle_state_in.rs2_val))) && (mul_op_reg == 64'(multicycle_state_in.alu_op));
    end

    always_comb begin : multicycle_wait_comb_func  // multicycle_wait_comb_func
        multicycle_wait_comb=((div_instruction_comb && !div_result_match_comb)) || ((mul_instruction_comb && !mul_result_match_comb));
    end

    task _work (input logic reset);
    begin: _work
        logic[31:0] a;
        logic[31:0] b;
        logic[31:0] op;
        logic[31:0] abs_a;
        logic[31:0] abs_b;
        logic[63:0] shifted_remainder;
        logic[31:0] shifted_quotient;
        logic[31:0] final_result;
        logic signed_op;
        logic quotient_op;
        logic a_negative;
        logic b_negative;
        logic mul_signed_a;
        logic mul_signed_b;
        logic mul_a_negative;
        logic mul_b_negative;
        logic[31:0] mul_abs_a;
        logic[31:0] mul_abs_b;
        logic[63:0] mul_sum;
        logic[63:0] mul_product;
        a=multicycle_state_in.rs1_val;
        b=multicycle_state_in.rs2_val;
        op=multicycle_state_in.alu_op;
        signed_op=(op == unsigned'(32'(Alu_pkg::DIV))) || (op == unsigned'(32'(Alu_pkg::REM)));
        quotient_op=(op == unsigned'(32'(Alu_pkg::DIV))) || (op == unsigned'(32'(Alu_pkg::DIVU)));
        a_negative=signed_op && ((((a >>> 32'sh1F)) != 32'h0));
        b_negative=signed_op && ((((b >>> 32'sh1F)) != 32'h0));
        abs_a=(a_negative) ? ((~a + 'h1)) : (a);
        abs_b=(b_negative) ? ((~b + 'h1)) : (b);
        mul_signed_a=(op == unsigned'(32'(Alu_pkg::MULH))) || (op == unsigned'(32'(Alu_pkg::MULHSU)));
        mul_signed_b=op == unsigned'(32'(Alu_pkg::MULH));
        mul_a_negative=mul_signed_a && ((((a >>> 32'sh1F)) != 32'h0));
        mul_b_negative=mul_signed_b && ((((b >>> 32'sh1F)) != 32'h0));
        mul_abs_a=(mul_a_negative) ? ((~a + 'h1)) : (a);
        mul_abs_b=(mul_b_negative) ? ((~b + 'h1)) : (b);
        if (((div_busy_reg) != '0)) begin
            if (div_count_reg == 64'h20) begin
                final_result=(((div_op_reg == unsigned'(64'(Alu_pkg::DIV))) || (div_op_reg == unsigned'(64'(Alu_pkg::DIVU))))) ? (unsigned'(32'(div_quotient_reg))) : (unsigned'(32'(div_remainder_reg)));
                if ((((((div_op_reg == unsigned'(64'(Alu_pkg::DIV))) || (div_op_reg == unsigned'(64'(Alu_pkg::DIVU))))) && ((div_quotient_neg_reg) != '0))) || (((((div_op_reg == unsigned'(64'(Alu_pkg::REM))) || (div_op_reg == unsigned'(64'(Alu_pkg::REMU))))) && ((div_remainder_neg_reg) != '0)))) begin
                    final_result=~final_result + 'h1;
                end
                div_result_reg_tmp = final_result;
                div_busy_reg_tmp = 0;
                div_done_reg_tmp = 1;
            end
            else begin
                shifted_remainder=((unsigned'(64'(div_remainder_reg)) <<< 'h1)) | 64'(32'(((unsigned'(32'(div_dividend_reg)) >>> 32'sh1F))));
                shifted_quotient=unsigned'(32'(div_quotient_reg)) <<< 'h1;
                if (shifted_remainder>=64'(unsigned'(32'(div_divisor_reg)))) begin
                    shifted_remainder-=64'(unsigned'(32'(div_divisor_reg)));
                    shifted_quotient|='h1;
                end
                div_dividend_reg_tmp = 32'(unsigned'(32'(div_dividend_reg)) <<< 'h1);
                div_remainder_reg_tmp = shifted_remainder;
                div_quotient_reg_tmp = shifted_quotient;
                div_count_reg_tmp = div_count_reg + 64'h1;
            end
        end
        else begin
            if (div_instruction_comb && !div_result_match_comb) begin
                div_pc_reg_tmp = multicycle_state_in.pc;
                div_a_reg_tmp = a;
                div_b_reg_tmp = b;
                div_op_reg_tmp = op;
                div_count_reg_tmp = 64'h0;
                div_dividend_reg_tmp = abs_a;
                div_divisor_reg_tmp = abs_b;
                div_quotient_reg_tmp = 32'h0;
                div_remainder_reg_tmp = 64'h0;
                div_quotient_neg_reg_tmp = signed'(32'(a_negative)) != signed'(32'(b_negative));
                div_remainder_neg_reg_tmp = a_negative;
                if (b == 32'h0) begin
                    div_result_reg_tmp = unsigned'(32'((quotient_op) ? (~'h0) : (a)));
                    div_busy_reg_tmp = 0;
                    div_done_reg_tmp = 1;
                end
                else begin
                    div_busy_reg_tmp = 1;
                    div_done_reg_tmp = 0;
                end
            end
            else begin
                if (!div_instruction_comb) begin
                    div_done_reg_tmp = 0;
                end
            end
        end
        if (((mul_busy_reg) != '0)) begin
            mul_sum=mul_accumulator_reg;
            if (((unsigned'(32'(mul_multiplier_reg)) & 'h1)) != 32'h0) begin
                mul_sum+=unsigned'(64'(mul_multiplicand_reg));
            end
            mul_accumulator_reg_tmp = mul_sum;
            mul_multiplicand_reg_tmp = unsigned'(64'(unsigned'(64'(mul_multiplicand_reg)) <<< 'h1));
            mul_multiplier_reg_tmp = 32'(unsigned'(32'(mul_multiplier_reg)) >>> 32'sh1);
            if (mul_count_reg == 64'h1F) begin
                mul_product=(((mul_negate_reg) != '0)) ? ((~mul_sum + 64'h1)) : (mul_sum);
                mul_result_reg_tmp = unsigned'(32'((((mul_high_reg) != '0)) ? (32'((mul_product >>> 32'sh20))) : (32'(mul_product))));
                mul_busy_reg_tmp = 0;
                mul_done_reg_tmp = 1;
            end
            else begin
                mul_count_reg_tmp = mul_count_reg + 64'h1;
            end
        end
        else begin
            if (mul_instruction_comb && !mul_result_match_comb) begin
                mul_pc_reg_tmp = multicycle_state_in.pc;
                mul_a_reg_tmp = a;
                mul_b_reg_tmp = b;
                mul_op_reg_tmp = op;
                mul_count_reg_tmp = 64'h0;
                mul_multiplicand_reg_tmp = mul_abs_a;
                mul_multiplier_reg_tmp = mul_abs_b;
                mul_accumulator_reg_tmp = 64'h0;
                mul_negate_reg_tmp = signed'(32'(mul_a_negative)) != signed'(32'(mul_b_negative));
                mul_high_reg_tmp = op != unsigned'(32'(Alu_pkg::MUL));
                mul_busy_reg_tmp = 1;
                mul_done_reg_tmp = 0;
            end
            else begin
                if (!mul_instruction_comb) begin
                    mul_done_reg_tmp = 0;
                end
            end
        end
        if (reset) begin
            div_busy_reg_tmp = '0;
            div_done_reg_tmp = '0;
            div_count_reg_tmp = '0;
            div_dividend_reg_tmp = '0;
            div_divisor_reg_tmp = '0;
            div_quotient_reg_tmp = '0;
            div_remainder_reg_tmp = '0;
            div_quotient_neg_reg_tmp = '0;
            div_remainder_neg_reg_tmp = '0;
            div_result_reg_tmp = '0;
            div_pc_reg_tmp = '0;
            div_a_reg_tmp = '0;
            div_b_reg_tmp = '0;
            div_op_reg_tmp = '0;
            mul_busy_reg_tmp = '0;
            mul_done_reg_tmp = '0;
            mul_count_reg_tmp = '0;
            mul_multiplicand_reg_tmp = '0;
            mul_multiplier_reg_tmp = '0;
            mul_accumulator_reg_tmp = '0;
            mul_negate_reg_tmp = '0;
            mul_high_reg_tmp = '0;
            mul_result_reg_tmp = '0;
            mul_pc_reg_tmp = '0;
            mul_a_reg_tmp = '0;
            mul_b_reg_tmp = '0;
            mul_op_reg_tmp = '0;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    task _work_l2_clock (input logic reset);
    begin: _work_l2_clock
    end
    endtask

    always_ff @(posedge clk) begin
        div_busy_reg_tmp = div_busy_reg;
        div_done_reg_tmp = div_done_reg;
        div_count_reg_tmp = div_count_reg;
        div_dividend_reg_tmp = div_dividend_reg;
        div_divisor_reg_tmp = div_divisor_reg;
        div_quotient_reg_tmp = div_quotient_reg;
        div_remainder_reg_tmp = div_remainder_reg;
        div_quotient_neg_reg_tmp = div_quotient_neg_reg;
        div_remainder_neg_reg_tmp = div_remainder_neg_reg;
        div_result_reg_tmp = div_result_reg;
        div_pc_reg_tmp = div_pc_reg;
        div_a_reg_tmp = div_a_reg;
        div_b_reg_tmp = div_b_reg;
        div_op_reg_tmp = div_op_reg;
        mul_busy_reg_tmp = mul_busy_reg;
        mul_done_reg_tmp = mul_done_reg;
        mul_count_reg_tmp = mul_count_reg;
        mul_multiplicand_reg_tmp = mul_multiplicand_reg;
        mul_multiplier_reg_tmp = mul_multiplier_reg;
        mul_accumulator_reg_tmp = mul_accumulator_reg;
        mul_negate_reg_tmp = mul_negate_reg;
        mul_high_reg_tmp = mul_high_reg;
        mul_result_reg_tmp = mul_result_reg;
        mul_pc_reg_tmp = mul_pc_reg;
        mul_a_reg_tmp = mul_a_reg;
        mul_b_reg_tmp = mul_b_reg;
        mul_op_reg_tmp = mul_op_reg;

            _work(reset);

        div_busy_reg <= div_busy_reg_tmp;
        div_done_reg <= div_done_reg_tmp;
        div_count_reg <= div_count_reg_tmp;
        div_dividend_reg <= div_dividend_reg_tmp;
        div_divisor_reg <= div_divisor_reg_tmp;
        div_quotient_reg <= div_quotient_reg_tmp;
        div_remainder_reg <= div_remainder_reg_tmp;
        div_quotient_neg_reg <= div_quotient_neg_reg_tmp;
        div_remainder_neg_reg <= div_remainder_neg_reg_tmp;
        div_result_reg <= div_result_reg_tmp;
        div_pc_reg <= div_pc_reg_tmp;
        div_a_reg <= div_a_reg_tmp;
        div_b_reg <= div_b_reg_tmp;
        div_op_reg <= div_op_reg_tmp;
        mul_busy_reg <= mul_busy_reg_tmp;
        mul_done_reg <= mul_done_reg_tmp;
        mul_count_reg <= mul_count_reg_tmp;
        mul_multiplicand_reg <= mul_multiplicand_reg_tmp;
        mul_multiplier_reg <= mul_multiplier_reg_tmp;
        mul_accumulator_reg <= mul_accumulator_reg_tmp;
        mul_negate_reg <= mul_negate_reg_tmp;
        mul_high_reg <= mul_high_reg_tmp;
        mul_result_reg <= mul_result_reg_tmp;
        mul_pc_reg <= mul_pc_reg_tmp;
        mul_a_reg <= mul_a_reg_tmp;
        mul_b_reg <= mul_b_reg_tmp;
        mul_op_reg <= mul_op_reg_tmp;
    end

    always_ff @(posedge l2_clock) begin

            _work_l2_clock(reset);

    end

    assign alu_result_out = unsigned'(32'(alu_result_comb));

    assign mem_addr_out = mem_addr_comb;

    assign debug_alu_a_out = alu_a_comb;

    assign debug_alu_b_out = alu_b_comb;

    assign branch_taken_out = branch_taken_comb;

    assign branch_target_out = branch_target_comb;

    assign multicycle_wait_out = multicycle_wait_comb;


endmodule

`default_nettype none

import Predef_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import MemWBint_int_0_0_State_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_pkg::*;
import Alu_pkg::*;
import Mem_pkg::*;
import Br_pkg::*;


module ExecuteCalcExecuteCalcint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
    parameter ID = 1
,   parameter LENGTH = 3
 )
 (
    input wire clk
,   input wire reset
,   output wire mem_write_out
,   output wire[31:0] mem_write_addr_out
,   output wire[31:0] mem_write_data_out
,   output wire[7:0] mem_write_mask_out
,   output wire mem_read_out
,   output wire[31:0] mem_read_addr_out
,   output wire[31:0] alu_result_out
,   output wire branch_taken_out
,   output wire[31:0] branch_target_out
,   input wire MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[LENGTH-1:0] state_in
,   output wire ExecuteCalcint_int_0_0_State[LENGTH - ID-1:0] state_out
);

    typedef ExecuteCalcint_int_0_0_State STATE;

    // regs and combs
    reg[32-1:0] mem_addr_reg;
    reg[32-1:0] mem_data_reg;
    reg[8-1:0] mem_mask_reg;
    reg mem_write_reg;
    reg mem_read_reg;
    logic[31:0] alu_a_comb;
    logic[31:0] alu_b_comb;
    logic[63:0] alu_result_comb;
    logic branch_taken_comb;
    logic[31:0] branch_target_comb;
    ExecuteCalcint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg;

    // members

    // tmp variables
    logic[8-1:0] mem_mask_reg_tmp;
    logic mem_write_reg_tmp;
    logic mem_read_reg_tmp;
    ExecuteCalcint_int_0_0_State[LENGTH - ID-1:0] PipelineStage___state_reg_tmp;


    always_comb begin : alu_a_comb_func  // alu_a_comb_func
        alu_a_comb=state_in[ID - 64'h1].rs1_val;
    end

    always_comb begin : alu_b_comb_func  // alu_b_comb_func
        alu_b_comb=(((signed'(32'(state_in[(ID - 64'h1)].alu_op)) == signed'(32'(Alu_pkg::ADD))) && (signed'(32'(state_in[(ID - 64'h1)].mem_op)) != signed'(32'(Mem_pkg::MNONE))))) ? (state_in[ID - 64'h1].imm) : ((((((state_in[(ID - 64'h1)].rs2) != '0) || (signed'(32'(state_in[(ID - 64'h1)].br_op)) == signed'(32'(Br_pkg::BEQZ)))) || (signed'(32'(state_in[(ID - 64'h1)].br_op)) == signed'(32'(Br_pkg::BNEZ))))) ? (state_in[ID - 64'h1].rs2_val) : (state_in[ID - 64'h1].imm));
    end

    always_comb begin : alu_result_comb_func  // alu_result_comb_func
        logic[31:0] a;
        logic[31:0] b;
        logic[31:0] alu_op;
        a=alu_a_comb;
        b=alu_b_comb;
        alu_result_comb=64'h0;
        alu_op=state_in[ID - 64'h1].alu_op;
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
            alu_result_comb=32'(a*b);
        end
        unsigned'(32'(Alu_pkg::MULH)): begin
            alu_result_comb=((128'(64'(a))*128'(64'(b)))) >>> 32'sh20;
        end
        unsigned'(32'(Alu_pkg::DIV)): begin
            alu_result_comb=unsigned'(32'((b == 32'h0) ? (32'hFFFFFFFF) : (a/b)));
        end
        unsigned'(32'(Alu_pkg::REM)): begin
            alu_result_comb=unsigned'(32'((b == 32'h0) ? (a) : (a % b)));
        end
        unsigned'(32'(Alu_pkg::ANONE)): begin
        end
        endcase
        if ((alu_op == unsigned'(32'(Alu_pkg::SLT))) || (alu_op == unsigned'(32'(Alu_pkg::SLTU)))) begin
            alu_result_comb|=(64'(((a == b)))) <<< 'h20;
        end
    end

    always_comb begin : branch_taken_comb_func  // branch_taken_comb_func
        logic[63:0] alu_result;
        alu_result=alu_result_comb;
        branch_taken_comb=0;
        case (signed'(32'(state_in[ID - 64'h1].br_op)))
        signed'(32'(Br_pkg::BEQZ)): begin
            branch_taken_comb=((alu_result >>> 32'sh20) != '0);
        end
        signed'(32'(Br_pkg::BNEZ)): begin
            branch_taken_comb=!(((alu_result >>> 32'sh20)) != '0);
        end
        signed'(32'(Br_pkg::BEQ)): begin
            branch_taken_comb=((alu_result >>> 32'sh20) != '0);
        end
        signed'(32'(Br_pkg::BNE)): begin
            branch_taken_comb=!(((alu_result >>> 32'sh20)) != '0);
        end
        signed'(32'(Br_pkg::BLT)): begin
            branch_taken_comb=((alu_result & 64'hFFFFFFFF) != '0);
        end
        signed'(32'(Br_pkg::BGE)): begin
            branch_taken_comb=!(((alu_result & 64'hFFFFFFFF)) != '0);
        end
        signed'(32'(Br_pkg::BLTU)): begin
            branch_taken_comb=((alu_result & 64'hFFFFFFFF) != '0);
        end
        signed'(32'(Br_pkg::BGEU)): begin
            branch_taken_comb=!(((alu_result & 64'hFFFFFFFF)) != '0);
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
        branch_taken_comb=branch_taken_comb && ((state_in[(ID - 64'h1)].valid) != '0);
    end

    always_comb begin : branch_target_comb_func  // branch_target_comb_func
        branch_target_comb=32'h0;
        if (signed'(32'(state_in[(ID - 64'h1)].br_op)) != signed'(32'(Br_pkg::BNONE))) begin
            if (signed'(32'(state_in[(ID - 64'h1)].br_op)) == signed'(32'(Br_pkg::JAL))) begin
                branch_target_comb=state_in[(ID - 64'h1)].pc + state_in[(ID - 64'h1)].imm;
            end
            else begin
                if ((signed'(32'(state_in[(ID - 64'h1)].br_op)) == signed'(32'(Br_pkg::JALR))) || (signed'(32'(state_in[(ID - 64'h1)].br_op)) == signed'(32'(Br_pkg::JR)))) begin
                    branch_target_comb=((state_in[(ID - 64'h1)].rs1_val + state_in[(ID - 64'h1)].imm)) & ~'h1;
                end
                else begin
                    branch_target_comb=state_in[(ID - 64'h1)].pc + state_in[(ID - 64'h1)].imm;
                end
            end
        end
    end

    task do_execute ();
    begin: do_execute
        PipelineStage___state_reg_tmp[64'h0].alu_result=alu_result_comb;
        PipelineStage___state_reg_tmp[64'h0].debug_alu_a=alu_a_comb;
        PipelineStage___state_reg_tmp[64'h0].debug_alu_b=alu_b_comb;
        PipelineStage___state_reg_tmp[64'h0].debug_branch_target=branch_target_comb;
        PipelineStage___state_reg_tmp[64'h0].debug_branch_taken=branch_taken_comb;
    end
    endtask

    task start_memory ();
    begin: start_memory
        mem_addr_reg <= alu_result_comb;
        mem_data_reg <= state_in[ID - 64'h1].rs2_val;
        mem_write_reg_tmp = (('h0) != '0);
        mem_mask_reg_tmp = 8'h0;
        if ((signed'(32'(state_in[(ID - 64'h1)].mem_op)) == signed'(32'(Mem_pkg::STORE))) && ((state_in[(ID - 64'h1)].valid) != '0)) begin
            case (signed'(32'(state_in[ID - 64'h1].funct3)))
            'h0: begin
                mem_write_reg_tmp = ((state_in[ID - 64'h1].valid) != '0);
                mem_mask_reg_tmp = 8'h1;
            end
            'h1: begin
                mem_write_reg_tmp = ((state_in[ID - 64'h1].valid) != '0);
                mem_mask_reg_tmp = 8'h3;
            end
            'h2: begin
                mem_write_reg_tmp = ((state_in[ID - 64'h1].valid) != '0);
                mem_mask_reg_tmp = 8'hF;
            end
            endcase
        end
        mem_read_reg_tmp = (('h0) != '0);
        if ((signed'(32'(state_in[(ID - 64'h1)].mem_op)) == signed'(32'(Mem_pkg::LOAD))) && ((state_in[(ID - 64'h1)].valid) != '0)) begin
            case (signed'(32'(state_in[ID - 64'h1].funct3)))
            'h0: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            'h1: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            'h2: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            'h4: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            'h5: begin
                mem_read_reg_tmp = (('h1) != '0);
            end
            default: begin
            end
            endcase
        end
    end
    endtask

    task PipelineStage____work (input logic reset);
    begin: PipelineStage____work
        logic[63:0] i;
        for (i=64'h1;i < (LENGTH - ID);i=i+1) begin
            PipelineStage___state_reg_tmp[i] = PipelineStage___state_reg[i - 64'h1];
        end
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            mem_write_reg_tmp = '0;
            mem_read_reg_tmp = '0;
        end
        PipelineStage____work(reset);
        PipelineStage___state_reg_tmp[64'h0] = 0;
        do_execute();
        start_memory();
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        mem_mask_reg_tmp = mem_mask_reg;
        mem_write_reg_tmp = mem_write_reg;
        mem_read_reg_tmp = mem_read_reg;
        PipelineStage___state_reg_tmp = PipelineStage___state_reg;

            _work(reset);

        mem_mask_reg <= mem_mask_reg_tmp;
        mem_write_reg <= mem_write_reg_tmp;
        mem_read_reg <= mem_read_reg_tmp;
        PipelineStage___state_reg <= PipelineStage___state_reg_tmp;
    end

    assign mem_write_out = mem_write_reg;

    assign mem_write_addr_out = mem_addr_reg;

    assign mem_write_data_out = mem_data_reg;

    assign mem_write_mask_out = mem_mask_reg;

    assign mem_read_out = mem_read_reg;

    assign mem_read_addr_out = mem_addr_reg;

    assign alu_result_out = unsigned'(32'(alu_result_comb));

    assign branch_taken_out = branch_taken_comb;

    assign branch_target_out = branch_target_comb;

    assign state_out = PipelineStage___state_reg;


endmodule

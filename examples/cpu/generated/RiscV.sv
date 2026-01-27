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


module RiscV (
    input wire clk
,   input wire reset
,   output wire dmem_write_out
,   output logic[31:0] dmem_write_addr_out
,   output logic[31:0] dmem_write_data_out
,   output logic[31:0] dmem_write_mask_out
,   output wire dmem_read_out
,   output logic[31:0] dmem_read_addr_out
,   input logic[31:0] dmem_read_data_in
,   output logic[31:0] imem_read_addr_out
,   input logic[31:0] imem_read_data_in
,   input wire debugen_in
);
    parameter     LENGTH = 3;

    reg[31:0] pc;
    reg valid;
    MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[3-1:0] Pipeline___states_comb;

      logic[31:0] regs__write_addr_in;
      wire regs__write_in;
      logic[31:0] regs__write_data_in;
      logic[31:0] regs__read_addr0_in;
      logic[31:0] regs__read_addr1_in;
      wire regs__read_in;
      logic[31:0] regs__read_data0_out;
      logic[31:0] regs__read_data1_out;
      wire regs__debugen_in;
    File #(
        32
,       32
    ) regs (
        .clk(clk)
,       .reset(reset)
,       .write_addr_in(regs__write_addr_in)
,       .write_in(regs__write_in)
,       .write_data_in(regs__write_data_in)
,       .read_addr0_in(regs__read_addr0_in)
,       .read_addr1_in(regs__read_addr1_in)
,       .read_in(regs__read_in)
,       .read_data0_out(regs__read_data0_out)
,       .read_data1_out(regs__read_data1_out)
,       .debugen_in(regs__debugen_in)
    );
      logic[31:0] Pipeline___members_tuple_0__pc_in;
      wire Pipeline___members_tuple_0__instr_valid_in;
      logic[31:0] Pipeline___members_tuple_0__instr_in;
      logic[31:0] Pipeline___members_tuple_0__regs_data0_in;
      logic[31:0] Pipeline___members_tuple_0__regs_data1_in;
      logic[31:0] Pipeline___members_tuple_0__rs1_out;
      logic[31:0] Pipeline___members_tuple_0__rs2_out;
      logic[31:0] Pipeline___members_tuple_0__alu_result_in;
      logic[31:0] Pipeline___members_tuple_0__mem_data_in;
      wire Pipeline___members_tuple_0__stall_out;
      MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[(3)-1:0] Pipeline___members_tuple_0__state_in;
      DecodeFetchint_int_0_0_State[(3) - (0)-1:0] Pipeline___members_tuple_0__state_out;
    DecodeFetchDecodeFetchint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
        0
,       3
    ) Pipeline___members_tuple_0 (
        .clk(clk)
,       .reset(reset)
,       .pc_in(Pipeline___members_tuple_0__pc_in)
,       .instr_valid_in(Pipeline___members_tuple_0__instr_valid_in)
,       .instr_in(Pipeline___members_tuple_0__instr_in)
,       .regs_data0_in(Pipeline___members_tuple_0__regs_data0_in)
,       .regs_data1_in(Pipeline___members_tuple_0__regs_data1_in)
,       .rs1_out(Pipeline___members_tuple_0__rs1_out)
,       .rs2_out(Pipeline___members_tuple_0__rs2_out)
,       .alu_result_in(Pipeline___members_tuple_0__alu_result_in)
,       .mem_data_in(Pipeline___members_tuple_0__mem_data_in)
,       .stall_out(Pipeline___members_tuple_0__stall_out)
,       .state_in(Pipeline___members_tuple_0__state_in)
,       .state_out(Pipeline___members_tuple_0__state_out)
    );
      wire Pipeline___members_tuple_1__mem_write_out;
      logic[31:0] Pipeline___members_tuple_1__mem_write_addr_out;
      logic[31:0] Pipeline___members_tuple_1__mem_write_data_out;
      logic[31:0] Pipeline___members_tuple_1__mem_write_mask_out;
      wire Pipeline___members_tuple_1__mem_read_out;
      logic[31:0] Pipeline___members_tuple_1__mem_read_addr_out;
      logic[31:0] Pipeline___members_tuple_1__alu_result_out;
      wire Pipeline___members_tuple_1__branch_taken_out;
      logic[31:0] Pipeline___members_tuple_1__branch_target_out;
      MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[(3)-1:0] Pipeline___members_tuple_1__state_in;
      ExecuteCalcint_int_0_0_State[(3) - (1)-1:0] Pipeline___members_tuple_1__state_out;
    ExecuteCalcExecuteCalcint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
        1
,       3
    ) Pipeline___members_tuple_1 (
        .clk(clk)
,       .reset(reset)
,       .mem_write_out(Pipeline___members_tuple_1__mem_write_out)
,       .mem_write_addr_out(Pipeline___members_tuple_1__mem_write_addr_out)
,       .mem_write_data_out(Pipeline___members_tuple_1__mem_write_data_out)
,       .mem_write_mask_out(Pipeline___members_tuple_1__mem_write_mask_out)
,       .mem_read_out(Pipeline___members_tuple_1__mem_read_out)
,       .mem_read_addr_out(Pipeline___members_tuple_1__mem_read_addr_out)
,       .alu_result_out(Pipeline___members_tuple_1__alu_result_out)
,       .branch_taken_out(Pipeline___members_tuple_1__branch_taken_out)
,       .branch_target_out(Pipeline___members_tuple_1__branch_target_out)
,       .state_in(Pipeline___members_tuple_1__state_in)
,       .state_out(Pipeline___members_tuple_1__state_out)
    );
      logic[31:0] Pipeline___members_tuple_2__mem_data_in;
      logic[31:0] Pipeline___members_tuple_2__regs_data_out;
      logic[31:0] Pipeline___members_tuple_2__regs_wr_id_out;
      wire Pipeline___members_tuple_2__regs_write_out;
      MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[(3)-1:0] Pipeline___members_tuple_2__state_in;
      MemWBint_int_0_0_State[(3) - (2)-1:0] Pipeline___members_tuple_2__state_out;
    MemWBMemWBint_int_0_0_State_MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State #(
        2
,       3
    ) Pipeline___members_tuple_2 (
        .clk(clk)
,       .reset(reset)
,       .mem_data_in(Pipeline___members_tuple_2__mem_data_in)
,       .regs_data_out(Pipeline___members_tuple_2__regs_data_out)
,       .regs_wr_id_out(Pipeline___members_tuple_2__regs_wr_id_out)
,       .regs_write_out(Pipeline___members_tuple_2__regs_write_out)
,       .state_in(Pipeline___members_tuple_2__state_in)
,       .state_out(Pipeline___members_tuple_2__state_out)
    );

    always @(*) begin
        logic[31:0] y;
        logic[31:0] x;
        logic[31:0] offset;
        for (y = 0;y < LENGTH;y=y+1) begin
            x = 0;
            offset = 0;
            Pipeline___states_comb[y] = 0;
            if (x <= y) begin
                Pipeline___states_comb[y][offset+: $bits(DecodeFetchint_int_0_0_State_pkg::DecodeFetchint_int_0_0_State)/8] = Pipeline___members_tuple_0__state_out[y - x];
            end
            x=x+1;
            offset += ($bits(DecodeFetchint_int_0_0_State_pkg::DecodeFetchint_int_0_0_State)/8);
            if (x <= y) begin
                Pipeline___states_comb[y][offset+: $bits(DecodeFetchint_int_0_0_State_pkg::DecodeFetchint_int_0_0_State)/8] = Pipeline___members_tuple_1__state_out[y - x];
            end
            x=x+1;
            offset += ($bits(DecodeFetchint_int_0_0_State_pkg::DecodeFetchint_int_0_0_State)/8);
            if (x <= y) begin
                Pipeline___states_comb[y][offset+: $bits(DecodeFetchint_int_0_0_State_pkg::DecodeFetchint_int_0_0_State)/8] = Pipeline___members_tuple_2__state_out[y - x];
            end
            x=x+1;
            offset += ($bits(DecodeFetchint_int_0_0_State_pkg::DecodeFetchint_int_0_0_State)/8);
        end
    end

    generate
        assign Pipeline___members_tuple_0__state_in = Pipeline___states_comb;
        assign Pipeline___members_tuple_1__state_in = Pipeline___states_comb;
        assign Pipeline___members_tuple_2__state_in = Pipeline___states_comb;
    endgenerate
    assign dmem_write_out = Pipeline___members_tuple_1__mem_write_out;

    assign dmem_write_addr_out = Pipeline___members_tuple_1__mem_write_addr_out;

    assign dmem_write_data_out = Pipeline___members_tuple_1__mem_write_data_out;

    assign dmem_write_mask_out = Pipeline___members_tuple_1__mem_write_mask_out;

    assign dmem_read_out = Pipeline___members_tuple_1__mem_read_out;

    assign dmem_read_addr_out = Pipeline___members_tuple_1__mem_read_addr_out;

    assign imem_read_addr_out = pc;


    generate
        assign Pipeline___members_tuple_0__pc_in = pc;
        assign Pipeline___members_tuple_0__instr_valid_in = valid;
        assign Pipeline___members_tuple_0__instr_in = imem_read_data_in;
        assign Pipeline___members_tuple_0__regs_data0_in = Pipeline___members_tuple_0__rs1_out == 0 ? 0 : regs__read_data0_out;
        assign Pipeline___members_tuple_0__regs_data1_in = Pipeline___members_tuple_0__rs2_out == 0 ? 0 : regs__read_data1_out;
        assign Pipeline___members_tuple_0__alu_result_in = Pipeline___members_tuple_1__alu_result_out;
        assign Pipeline___members_tuple_0__mem_data_in = dmem_read_data_in;
        assign Pipeline___members_tuple_2__mem_data_in = dmem_read_data_in;
        assign regs__read_addr0_in = Pipeline___members_tuple_0__rs1_out;
        assign regs__read_addr1_in = Pipeline___members_tuple_0__rs2_out;
        assign regs__write_in = Pipeline___members_tuple_2__regs_write_out;
        assign regs__write_addr_in = Pipeline___members_tuple_2__regs_wr_id_out;
        assign regs__write_data_in = Pipeline___members_tuple_2__regs_data_out;
    endgenerate
    assign dmem_write_out = Pipeline___members_tuple_1__mem_write_out;

    assign dmem_write_addr_out = Pipeline___members_tuple_1__mem_write_addr_out;

    assign dmem_write_data_out = Pipeline___members_tuple_1__mem_write_data_out;

    assign dmem_write_mask_out = Pipeline___members_tuple_1__mem_write_mask_out;

    assign dmem_read_out = Pipeline___members_tuple_1__mem_read_out;

    assign dmem_read_addr_out = Pipeline___members_tuple_1__mem_read_addr_out;

    assign imem_read_addr_out = pc;


    function logic signed[31:0] Instr___sext (
        input Instr _this
,       input logic[31:0] val
,       input logic[31:0] bits
    );
        integer m; m = 1 << (bits - 1);
        return (val ^ m) - m;
    endfunction

    function logic signed[31:0] Instr___imm_I (input Instr _this);
        return Instr___sext(_this, _this.i.imm11_0, 12);
    endfunction

    function logic signed[31:0] Instr___imm_S (input Instr _this);
        return Instr___sext(_this, _this.s.imm4_0 | (_this.s.imm11_5 << 5), 12);
    endfunction

    function logic signed[31:0] Instr___imm_B (input Instr _this);
        return Instr___sext(_this, (_this.b.imm4_1 << 1) | (_this.b.imm11 << 11) | (_this.b.imm10_5 << 5) | (_this.b.imm12 << 12), 13);
    endfunction

    function logic signed[31:0] Instr___imm_J (input Instr _this);
        return Instr___sext(_this, (_this.j.imm10_1 << 1) | (_this.j.imm11 << 11) | (_this.j.imm19_12 << 12) | (_this.j.imm20 << 20), 21);
    endfunction

    function logic signed[31:0] Instr___imm_U (input Instr _this);
        return _this.u.imm31_12 << 12;
    endfunction

    task Instr___decode (
        input Instr _this
,       output MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State state_out
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
                        state_out.alu_op = (_this.i.imm11_0 >> 10) & 1 ? Alu_pkg::SRA : Alu_pkg::SRL;
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
                            end
                            1: begin
                                state_out.br_op = Br_pkg::BNE;
                            end
                            4: begin
                                state_out.br_op = Br_pkg::BLT;
                            end
                            5: begin
                                state_out.br_op = Br_pkg::BGE;
                            end
                            6: begin
                                state_out.br_op = Br_pkg::BLTU;
                            end
                            7: begin
                                state_out.br_op = Br_pkg::BGEU;
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
        return (_this.raw >> lo) & ((1 << (hi - lo + 1)) - 1);
    endfunction

    function logic[31:0] Instr___bit (
        input Instr _this
,       input integer lo
    );
        return (_this.raw >> lo) & 1;
    endfunction

    task Instr___decode16 (
        input Instr _this
,       output MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State state_out
    );
    begin: Instr___decode16
        integer imm_tmp;
        state_out = 0;
        state_out.funct3 = 7;
        state_out.funct3 = 2;
        if (_this.c.opcode == 0) begin
            if (_this.c.funct3 == 0) begin
                state_out.rd = _this.c.rd_p + 8;
                state_out.rs1 = 2;
                state_out.imm = (Instr___bits(_this, 10, 7) << 6) | (Instr___bits(_this, 12, 11) << 4) | (Instr___bits(_this, 6, 5) << 2);
                state_out.alu_op = Alu_pkg::ADD;
                state_out.wb_op = Wb_pkg::ALU;
            end
            else begin
                if (_this.c.funct3 == 2) begin
                    state_out.rd = _this.c.rd_p + 8;
                    state_out.rs1 = _this.c.rs1_p + 8;
                    state_out.imm = (Instr___bit(_this, 5) << 6) | (Instr___bits(_this, 12, 10) << 3) | (Instr___bit(_this, 6) << 2);
                    state_out.alu_op = Alu_pkg::ADD;
                    state_out.mem_op = Mem_pkg::LOAD;
                    state_out.wb_op = Wb_pkg::MEM;
                end
                else begin
                    if (_this.c.funct3 == 6) begin
                        state_out.rs1 = _this.c.rs1_p + 8;
                        state_out.rs2 = _this.c.rd_p + 8;
                        state_out.imm = (Instr___bit(_this, 5) << 6) | (Instr___bits(_this, 12, 10) << 3) | (Instr___bit(_this, 6) << 2);
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
                    imm_tmp = (Instr___bit(_this, 12) << 5) | Instr___bits(_this, 6, 2);
                    imm_tmp = (imm_tmp << 26) >> 26;
                    state_out.imm = imm_tmp;
                    state_out.alu_op = Alu_pkg::ADD;
                    state_out.wb_op = Wb_pkg::ALU;
                end
                else begin
                    if (_this.c.funct3 == 1) begin
                        state_out.rd = 1;
                        state_out.wb_op = Wb_pkg::PC2;
                        state_out.br_op = Br_pkg::JAL;
                        state_out.imm = (_this.c.b12 << 11) | (Instr___bit(_this, 8) << 10) | (Instr___bits(_this, 10, 9) << 8) | (Instr___bit(_this, 6) << 7) | (Instr___bit(_this, 7) << 6) | (Instr___bit(_this, 2) << 5) | (Instr___bit(_this, 11) << 4) | (Instr___bits(_this, 5, 3) << 1);
                    end
                    else begin
                        if (_this.c.funct3 == 2) begin
                            state_out.rd = _this.q1.rs1;
                            imm_tmp = (Instr___bit(_this, 12) << 5) | Instr___bits(_this, 6, 2);
                            imm_tmp = (imm_tmp << 26) >> 26;
                            state_out.imm = imm_tmp;
                            state_out.alu_op = Alu_pkg::PASS;
                            state_out.wb_op = Wb_pkg::ALU;
                        end
                        else begin
                            if (_this.c.funct3 == 3) begin
                                state_out.rd = 2;
                                state_out.rs1 = 2;
                                imm_tmp = (Instr___bit(_this, 12) << 9) | (Instr___bit(_this, 4) << 8) | (Instr___bit(_this, 3) << 7) | (Instr___bit(_this, 5) << 6) | (Instr___bit(_this, 2) << 5) | (Instr___bit(_this, 6) << 4);
                                imm_tmp = (imm_tmp << 22) >> 22;
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
                                                imm_tmp = (Instr___bit(_this, 12) << 5) | Instr___bits(_this, 6, 2);
                                                imm_tmp = (imm_tmp << 26) >> 26;
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
                                        state_out.imm = (_this.c.b12 << 11) | (Instr___bit(_this, 8) << 10) | (Instr___bits(_this, 10, 9) << 8) | (Instr___bit(_this, 6) << 7) | (Instr___bit(_this, 7) << 6) | (Instr___bit(_this, 2) << 5) | (Instr___bit(_this, 11) << 4) | (Instr___bits(_this, 5, 3) << 1);
                                    end
                                    else begin
                                        if (_this.c.funct3 == 6) begin
                                            state_out.rs1 = _this.c.rs1_p + 8;
                                            state_out.br_op = Br_pkg::BEQZ;
                                            state_out.alu_op = Alu_pkg::SLTU;
                                            state_out.imm = (_this.c.b12 << 8) | (Instr___bits(_this, 6, 5) << 6) | (Instr___bit(_this, 2) << 5) | (Instr___bits(_this, 11, 10) << 3) | (Instr___bits(_this, 4, 3) << 1);
                                            if (_this.c.b12) begin
                                                state_out.imm |= ~511;
                                            end
                                        end
                                        else begin
                                            if (_this.c.funct3 == 7) begin
                                                state_out.rs1 = _this.c.rs1_p + 8;
                                                state_out.br_op = Br_pkg::BNEZ;
                                                state_out.alu_op = Alu_pkg::SLTU;
                                                state_out.imm = (_this.c.b12 << 8) | (Instr___bits(_this, 6, 5) << 6) | (Instr___bit(_this, 2) << 5) | (Instr___bits(_this, 11, 10) << 3) | (Instr___bits(_this, 4, 3) << 1);
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
                        state_out.imm = (_this.c.b12 << 5) | Instr___bits(_this, 6, 2);
                        state_out.alu_op = Alu_pkg::SLL;
                        state_out.wb_op = Wb_pkg::ALU;
                    end
                    else begin
                        if (_this.c.funct3 == 2) begin
                            state_out.rd = _this.q2.rs1;
                            state_out.rs1 = 2;
                            state_out.imm = (_this.c.b12 << 5) | (Instr___bits(_this, 6, 4) << 2) | (Instr___bits(_this, 3, 2) << 6);
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
                                    state_out.imm = (Instr___bits(_this, 8, 7) << 6) | (Instr___bits(_this, 12, 9) << 2);
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

    function [63:0] Instr___mnemonic (input Instr _this);
        logic[31:0] op;
        logic[31:0] f3;
        logic[31:0] f7;
        logic[31:0] b12;
        logic[31:0] rs2;
        logic[31:0] bits6_5;
        logic[31:0] quadrant;
        if ((_this.raw & 3) == 3) begin
            op = _this.r.opcode;
            f3 = _this.r.funct3;
            f7 = _this.r.funct7;
            case (op)
            51: begin
                if (f3 == 0 && f7 == 0) begin
                    return "add   ";
                end
            end
            19: begin
                if (f3 == 0) begin
                    return "addi  ";
                end
            end
            3: begin
                return "load  ";
            end
            35: begin
                return "store ";
            end
            99: begin
                return "branch";
            end
            111: begin
                return "jal   ";
            end
            103: begin
                return "jalr  ";
            end
            55: begin
                return "lui   ";
            end
            23: begin
                return "auipc ";
            end
            default: begin
                return "unknwn";
            end
            endcase
        end
        else begin
            op = _this.r.opcode;
            f3 = _this.c.funct3;
            b12 = _this.c.b12;
            rs2 = _this.q2.rs2;
            bits6_5 = _this.c.bits6_5;
            quadrant = op & 3;
            case (quadrant)
            0: begin
                case (f3)
                0: begin
                    return "addi4s";
                end
                2: begin
                    return "lw    ";
                end
                6: begin
                    return "sw    ";
                end
                3: begin
                    return "ld    ";
                end
                7: begin
                    return "sd    ";
                end
                default: begin
                    return "rsrvd ";
                end
                endcase
            end
            1: begin
                case (f3)
                0: begin
                    return "addi  ";
                end
                1: begin
                    return "jal   ";
                end
                2: begin
                    return "li    ";
                end
                3: begin
                    return "addisp";
                end
                4: begin
                    if (_this.c.bits11_10 == 0) begin
                        return "srli  ";
                    end
                    if (_this.c.bits11_10 == 1) begin
                        return "srai  ";
                    end
                    if (_this.c.bits11_10 == 2) begin
                        return "andi  ";
                    end
                    if (_this.c.bits11_10 == 3 && b12 == 0 && bits6_5 == 0) begin
                        return "sub   ";
                    end
                    if (_this.c.bits11_10 == 3 && b12 == 0 && bits6_5 == 1) begin
                        return "xor   ";
                    end
                    if (_this.c.bits11_10 == 3 && b12 == 0 && bits6_5 == 2) begin
                        return "or    ";
                    end
                    if (_this.c.bits11_10 == 3 && b12 == 0 && bits6_5 == 3) begin
                        return "and   ";
                    end
                    return "illgl ";
                end
                5: begin
                    return "j     ";
                end
                6: begin
                    return "beqz  ";
                end
                7: begin
                    return "bnez  ";
                end
                endcase
            end
            2: begin
                case (f3)
                0: begin
                    return "slli  ";
                end
                1: begin
                    return "fldsp ";
                end
                2: begin
                    return "lwsp  ";
                end
                4: begin
                    if (rs2 != 0 && b12 == 0) begin
                        return "mv    ";
                    end
                    if (rs2 != 0 && b12 == 1) begin
                        return "add   ";
                    end
                    if (rs2 == 0 && b12 == 0) begin
                        return "jr    ";
                    end
                    if (rs2 == 0 && b12 == 1) begin
                        return "jalr  ";
                    end
                    return "illgl ";
                end
                6: begin
                    return "swsp  ";
                end
                3: begin
                    return "ldsp  ";
                end
                7: begin
                    return "sdsp  ";
                end
                default: begin
                    return "rsrvd ";
                end
                endcase
            end
            endcase
        end
        return "unknwn";
    endfunction

    task Pipeline____work (input logic reset);
    begin: Pipeline____work
    end
    endtask

    task _work (input logic reset);
    begin: _work
        MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State tmp;
        Instr instr;
        MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[3-1:0] state_comb_tmp; state_comb_tmp = Pipeline___states_comb;
        if (reset) begin
            pc = '0;
            valid = '0;
            disable _work;
        end
        instr = {Pipeline___members_tuple_0__instr_in};
        if ((instr.raw & 3) == 3) begin
            Instr___decode(instr, tmp);
        end
        else begin
            Instr___decode16(instr, tmp);
        end
        if (debugen_in) begin
            $write("(%x/%x)%x: %s rs%02d/%02d,imm:%08x,rd%02d => (%x)ops:%02d/%x/%x/%x rs%02d/%02d:%08x/%08x,imm:%08x,alu:%09x,rd%02d br(%x)%08x => mem(%x/%x@%08x)%08x/%01x (%x)wop(%x),r(%x)%08x@%02d", valid, Pipeline___members_tuple_0__stall_out, pc, Instr___mnemonic(instr), tmp.rs1, tmp.rs2, tmp.imm, tmp.rd, state_comb_tmp[0].valid, state_comb_tmp[0].alu_op, state_comb_tmp[0].mem_op, state_comb_tmp[0].br_op, state_comb_tmp[0].wb_op, state_comb_tmp[0].rs1, state_comb_tmp[0].rs2, state_comb_tmp[0].rs1_val, state_comb_tmp[0].rs2_val, state_comb_tmp[0].imm, Pipeline___members_tuple_1__alu_result_out, state_comb_tmp[0].rd, Pipeline___members_tuple_1__branch_taken_out, Pipeline___members_tuple_1__branch_target_out, Pipeline___members_tuple_1__mem_write_out, Pipeline___members_tuple_1__mem_read_out, Pipeline___members_tuple_1__mem_write_addr_out, Pipeline___members_tuple_1__mem_write_data_out, Pipeline___members_tuple_1__mem_write_mask_out, state_comb_tmp[1].valid, state_comb_tmp[1].wb_op, Pipeline___members_tuple_2__regs_write_out, Pipeline___members_tuple_2__regs_data_out, Pipeline___members_tuple_2__regs_wr_id_out);
            $write("\n");
        end
        if (dmem_write_addr_out == 287454020 && dmem_write_out) begin
            integer out; out = $fopen("out.txt", "a");
            $fwrite("%c", dmem_write_data_out & 255);
            $fclose(out);
        end
        Pipeline____work(reset);
        if (valid && !Pipeline___members_tuple_0__stall_out) begin
            pc = pc + ((Pipeline___members_tuple_0__instr_in & 3) == 3 ? 4 : 2);
        end
        if (state_comb_tmp[0].valid && Pipeline___members_tuple_1__branch_taken_out) begin
            pc = Pipeline___members_tuple_1__branch_target_out;
        end
        valid = 1;
    end
    endtask

    always @(posedge clk) begin
        _work(reset);
    end

endmodule

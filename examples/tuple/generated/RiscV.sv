`default_nettype none

import Predef_pkg::*;
import Instr_pkg::*;
import Wb_pkg::*;
import Br_pkg::*;
import Alu_pkg::*;
import Mem_pkg::*;
import DecodeFetchint_int_0_0_State_pkg::*;
import ExecuteCalcint_int_0_0_State_pkg::*;
import MemWBint_int_0_0_State_pkg::*;
import MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_pkg::*;
import MakeStagesTupleImplMakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State_3_std_integer_sequencelogic63_0_0_1_2_pkg::*;


module RiscV (
    input wire clk
,   input wire reset
,   output wire dmem_write_out
,   output wire[31:0] dmem_write_addr_out
,   output wire[31:0] dmem_write_data_out
,   output wire[7:0] dmem_write_mask_out
,   output wire dmem_read_out
,   output wire[31:0] dmem_read_addr_out
,   input wire[31:0] dmem_read_data_in
,   output wire[31:0] imem_read_addr_out
,   input wire[31:0] imem_read_data_in
,   input wire debugen_in
);
    localparam  LENGTH = 64'h3;

    typedef MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State BIG_STATE;

    // regs and combs
    reg[32-1:0] pc;
    reg valid;
    MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[3-1:0] Pipeline___states_comb;
;

    // members
    wire[7:0] regs__write_addr_in;
    wire regs__write_in;
    wire[31:0] regs__write_data_in;
    wire[7:0] regs__read_addr0_in;
    wire[7:0] regs__read_addr1_in;
    wire regs__read_in;
    wire[31:0] regs__read_data0_out;
    wire[31:0] regs__read_data1_out;
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
    wire[31:0] Pipeline___members_tuple_0__pc_in;
    wire Pipeline___members_tuple_0__instr_valid_in;
    wire[31:0] Pipeline___members_tuple_0__instr_in;
    wire[31:0] Pipeline___members_tuple_0__regs_data0_in;
    wire[31:0] Pipeline___members_tuple_0__regs_data1_in;
    wire[7:0] Pipeline___members_tuple_0__rs1_out;
    wire[7:0] Pipeline___members_tuple_0__rs2_out;
    wire[31:0] Pipeline___members_tuple_0__alu_result_in;
    wire[31:0] Pipeline___members_tuple_0__mem_data_in;
    wire Pipeline___members_tuple_0__stall_out;
    wire MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[3-1:0] Pipeline___members_tuple_0__state_in;
    wire DecodeFetchint_int_0_0_State[3 - 0-1:0] Pipeline___members_tuple_0__state_out;
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
    wire[31:0] Pipeline___members_tuple_1__mem_write_addr_out;
    wire[31:0] Pipeline___members_tuple_1__mem_write_data_out;
    wire[7:0] Pipeline___members_tuple_1__mem_write_mask_out;
    wire Pipeline___members_tuple_1__mem_read_out;
    wire[31:0] Pipeline___members_tuple_1__mem_read_addr_out;
    wire[31:0] Pipeline___members_tuple_1__alu_result_out;
    wire Pipeline___members_tuple_1__branch_taken_out;
    wire[31:0] Pipeline___members_tuple_1__branch_target_out;
    wire MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[3-1:0] Pipeline___members_tuple_1__state_in;
    wire ExecuteCalcint_int_0_0_State[3 - 1-1:0] Pipeline___members_tuple_1__state_out;
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
    wire[31:0] Pipeline___members_tuple_2__mem_data_in;
    wire[31:0] Pipeline___members_tuple_2__regs_data_out;
    wire[7:0] Pipeline___members_tuple_2__regs_wr_id_out;
    wire Pipeline___members_tuple_2__regs_write_out;
    wire MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[3-1:0] Pipeline___members_tuple_2__state_in;
    wire MemWBint_int_0_0_State[3 - 2-1:0] Pipeline___members_tuple_2__state_out;
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

    // tmp variables
    logic[32-1:0] pc_tmp;
    logic valid_tmp;


    always_comb begin : Pipeline___states_comb_func  // Pipeline___states_comb_func
        logic[7:0] y;
        logic[7:0] x;
        logic[7:0] offset;
        for (y='h0;y < LENGTH;y=y+1) begin
            x='h0;
            offset='h0;
            Pipeline___states_comb[y] = 0;
            if (x<=y) begin
                Pipeline___states_comb[y][((unsigned'(32'(0)) + offset))*8 +: ($bits(DecodeFetchint_int_0_0_State_pkg::DecodeFetchint_int_0_0_State))]=Pipeline___members_tuple_0__state_out[y - x];
            end
            x=x+1;
            offset+=($bits(DecodeFetchint_int_0_0_State_pkg::DecodeFetchint_int_0_0_State)/8);
            if (x<=y) begin
                Pipeline___states_comb[y][((unsigned'(32'(0)) + offset))*8 +: ($bits(ExecuteCalcint_int_0_0_State_pkg::ExecuteCalcint_int_0_0_State))]=Pipeline___members_tuple_1__state_out[y - x];
            end
            x=x+1;
            offset+=($bits(ExecuteCalcint_int_0_0_State_pkg::ExecuteCalcint_int_0_0_State)/8);
            if (x<=y) begin
                Pipeline___states_comb[y][((unsigned'(32'(0)) + offset))*8 +: ($bits(MemWBint_int_0_0_State_pkg::MemWBint_int_0_0_State))]=Pipeline___members_tuple_2__state_out[y - x];
            end
            x=x+1;
            offset+=($bits(MemWBint_int_0_0_State_pkg::MemWBint_int_0_0_State)/8);
        end
    end

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
,       output MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State state_out
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
,       output MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State state_out
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

    function string Instr___mnemonic (input Instr _this);
        logic[31:0] op;
        logic[31:0] f3;
        logic[31:0] f7;
        logic[31:0] b12;
        logic[31:0] rs2;
        logic[31:0] bits6_5;
        logic[31:0] quadrant;
        if (((_this.raw & 'h3)) == 'h3) begin
            op=_this.r.opcode;
            f3=_this.r.funct3;
            f7=_this.r.funct7;
            case (op)
            'h33: begin
                if ((f3 == 'h0) && (f7 == 'h0)) begin
                    return "add   ";
                end
                if ((f3 == 'h0) && (f7 == 'h20)) begin
                    return "sub   ";
                end
                if ((f3 == 'h0) && (f7 == 'h1)) begin
                    return "mul   ";
                end
                if ((f3 == 'h7) && (f7 == 'h1)) begin
                    return "remu  ";
                end
                if (f3 == 'h7) begin
                    return "and   ";
                end
                if (f3 == 'h6) begin
                    return "or    ";
                end
                if (f3 == 'h4) begin
                    return "xor   ";
                end
                if (f3 == 'h1) begin
                    return "sll   ";
                end
                if ((f3 == 'h5) && (f7 == 'h0)) begin
                    return "srl   ";
                end
                if ((f3 == 'h5) && (f7 == 'h20)) begin
                    return "sra   ";
                end
                if ((f3 == 'h5) && (f7 == 'h1)) begin
                    return "divu  ";
                end
                if (f3 == 'h2) begin
                    return "slt   ";
                end
                if ((f3 == 'h3) && (f7 == 'h1)) begin
                    return "mulhu ";
                end
                if (f3 == 'h3) begin
                    return "sltu  ";
                end
                return "r-type";
            end
            'h13: begin
                if (f3 == 'h0) begin
                    return "addi  ";
                end
                if (f3 == 'h7) begin
                    return "andi  ";
                end
                if (f3 == 'h6) begin
                    return "ori   ";
                end
                if (f3 == 'h4) begin
                    return "xori  ";
                end
                if (f3 == 'h1) begin
                    return "slli  ";
                end
                if ((f3 == 'h5) && (f7 == 'h0)) begin
                    return "srli  ";
                end
                if ((f3 == 'h5) && (f7 == 'h20)) begin
                    return "srai  ";
                end
                if (f3 == 'h2) begin
                    return "slti  ";
                end
                if (f3 == 'h3) begin
                    return "sltiu ";
                end
                return "aluimm";
            end
            'h3: begin
                return "load  ";
            end
            'h23: begin
                return "store ";
            end
            'h63: begin
                return "branch";
            end
            'h6F: begin
                return "jal   ";
            end
            'h67: begin
                return "jalr  ";
            end
            'h37: begin
                return "lui   ";
            end
            'h17: begin
                return "auipc ";
            end
            default: begin
                return "unknwn";
            end
            endcase
        end
        else begin
            op=_this.r.opcode;
            f3=_this.c.funct3;
            b12=_this.c.b12;
            rs2=_this.q2.rs2;
            bits6_5=_this.c.bits6_5;
            quadrant=op & 'h3;
            case (quadrant)
            'h0: begin
                case (f3)
                'h0: begin
                    return "addi4s";
                end
                'h2: begin
                    return "lw    ";
                end
                'h6: begin
                    return "sw    ";
                end
                'h3: begin
                    return "ld    ";
                end
                'h7: begin
                    return "sd    ";
                end
                default: begin
                    return "rsrvd ";
                end
                endcase
            end
            'h1: begin
                case (f3)
                'h0: begin
                    return "addi  ";
                end
                'h1: begin
                    return "jal   ";
                end
                'h2: begin
                    return "li    ";
                end
                'h3: begin
                    return "addisp";
                end
                'h4: begin
                    if (_this.c.bits11_10 == 'h0) begin
                        return "srli  ";
                    end
                    if (_this.c.bits11_10 == 'h1) begin
                        return "srai  ";
                    end
                    if (_this.c.bits11_10 == 'h2) begin
                        return "andi  ";
                    end
                    if (((_this.c.bits11_10 == 'h3) && (b12 == 'h0)) && (bits6_5 == 'h0)) begin
                        return "sub   ";
                    end
                    if (((_this.c.bits11_10 == 'h3) && (b12 == 'h0)) && (bits6_5 == 'h1)) begin
                        return "xor   ";
                    end
                    if (((_this.c.bits11_10 == 'h3) && (b12 == 'h0)) && (bits6_5 == 'h2)) begin
                        return "or    ";
                    end
                    if (((_this.c.bits11_10 == 'h3) && (b12 == 'h0)) && (bits6_5 == 'h3)) begin
                        return "and   ";
                    end
                    return "illgl ";
                end
                'h5: begin
                    return "j     ";
                end
                'h6: begin
                    return "beqz  ";
                end
                'h7: begin
                    return "bnez  ";
                end
                default: begin
                    return "rsrvd ";
                end
                endcase
            end
            'h2: begin
                case (f3)
                'h0: begin
                    return "slli  ";
                end
                'h1: begin
                    return "fldsp ";
                end
                'h2: begin
                    return "lwsp  ";
                end
                'h4: begin
                    if ((rs2 != 'h0) && (b12 == 'h0)) begin
                        return "mv    ";
                    end
                    if ((rs2 != 'h0) && (b12 == 'h1)) begin
                        return "add   ";
                    end
                    if ((rs2 == 'h0) && (b12 == 'h0)) begin
                        return "jr    ";
                    end
                    if ((rs2 == 'h0) && (b12 == 'h1)) begin
                        return "jalr  ";
                    end
                    return "illgl ";
                end
                'h6: begin
                    return "swsp  ";
                end
                'h3: begin
                    return "ldsp  ";
                end
                'h7: begin
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

    task debug ();
    begin: debug
        MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State tmp;
        Instr instr;
        MakeBigStateDecodeFetchint_int_0_0_State_ExecuteCalcint_int_0_0_State_MemWBint_int_0_0_State[3-1:0] state_comb_tmp;
        state_comb_tmp = Pipeline___states_comb;
        instr = '{imem_read_data_in};
        if (((instr.raw & 'h3)) == 'h3) begin
            Instr___decode(instr, tmp);
        end
        else begin
            Instr___decode16(instr, tmp);
        end
        $write("(%d/%d)%x: %s rs%02d/%02d,imm:%08x,rd%02d => (%d)ops:%02d/%x/%x/%x rs%02d/%02d:%08x/%08x,imm:%08x,alu:%09x,rd%02d br(%d)%08x => mem(%d/%d@%08x)%08x/%01x (%d)wop(%x),r(%d)%08x@%02d", valid, Pipeline___members_tuple_0__stall_out, pc, Instr___mnemonic(instr), signed'(32'(tmp.rs1)), signed'(32'(tmp.rs2)), tmp.imm, signed'(32'(tmp.rd)), state_comb_tmp['h0].valid, unsigned'(8'(state_comb_tmp['h0].alu_op)), unsigned'(8'(state_comb_tmp['h0].mem_op)), unsigned'(8'(state_comb_tmp['h0].br_op)), unsigned'(8'(state_comb_tmp['h0].wb_op)), signed'(32'(state_comb_tmp['h0].rs1)), signed'(32'(state_comb_tmp['h0].rs2)), state_comb_tmp['h0].rs1_val, state_comb_tmp['h0].rs2_val, state_comb_tmp['h0].imm, Pipeline___members_tuple_1__alu_result_out, signed'(32'(state_comb_tmp['h0].rd)), Pipeline___members_tuple_1__branch_taken_out, Pipeline___members_tuple_1__branch_target_out, Pipeline___members_tuple_1__mem_write_out, Pipeline___members_tuple_1__mem_read_out, Pipeline___members_tuple_1__mem_write_addr_out, Pipeline___members_tuple_1__mem_write_data_out, Pipeline___members_tuple_1__mem_write_mask_out, state_comb_tmp['h1].valid, unsigned'(8'(state_comb_tmp['h1].wb_op)), Pipeline___members_tuple_2__regs_write_out, Pipeline___members_tuple_2__regs_data_out, Pipeline___members_tuple_2__regs_wr_id_out);
        $write("\n");
    end
    endtask

    task Pipeline____work (input logic reset);
    begin: Pipeline____work
    end
    endtask

    task _work (input logic reset);
    begin: _work
        if (debugen_in) begin
            debug();
        end
        if ((dmem_write_addr_out == 'h11223344) && dmem_write_out) begin
            logic signed[31:0] out; out = $fopen("out.txt", "a");
            $fwrite(out, "%c", dmem_write_data_out & 'hFF);
            $fclose(out);
        end
        if (valid && !Pipeline___members_tuple_0__stall_out) begin
            pc_tmp = unsigned'(32'(pc + (((((Pipeline___members_tuple_0__instr_in & 'h3)) == 'h3)) ? ('h4) : ('h2))));
        end
        if (Pipeline___states_comb['h0].valid && Pipeline___members_tuple_1__branch_taken_out) begin
            pc_tmp = unsigned'(32'(Pipeline___members_tuple_1__branch_target_out));
        end
        valid_tmp = unsigned'(1'(1));
        Pipeline____work(reset);
        if (reset) begin
            pc_tmp = '0;
            valid_tmp = '0;
        end
    end
    endtask

    generate  // Pipeline____assign
        assign Pipeline___members_tuple_0__state_in=Pipeline___states_comb;
        assign Pipeline___members_tuple_1__state_in=Pipeline___states_comb;
        assign Pipeline___members_tuple_2__state_in=Pipeline___states_comb;
    endgenerate

    generate  // _assign
        assign Pipeline___members_tuple_0__pc_in = pc;
        assign Pipeline___members_tuple_0__instr_valid_in = valid;
        assign Pipeline___members_tuple_0__instr_in = imem_read_data_in;
        assign Pipeline___members_tuple_0__regs_data0_in = (Pipeline___members_tuple_0__rs1_out == 'h0) ? ('h0) : (regs__read_data0_out);
        assign Pipeline___members_tuple_0__regs_data1_in = (Pipeline___members_tuple_0__rs2_out == 'h0) ? ('h0) : (regs__read_data1_out);
        assign Pipeline___members_tuple_0__alu_result_in = Pipeline___members_tuple_1__alu_result_out;
        assign Pipeline___members_tuple_0__mem_data_in = dmem_read_data_in;
        assign dmem_write_out = Pipeline___members_tuple_1__mem_write_out;
        assign dmem_write_addr_out = Pipeline___members_tuple_1__mem_write_addr_out;
        assign dmem_write_data_out = Pipeline___members_tuple_1__mem_write_data_out;
        assign dmem_write_mask_out = Pipeline___members_tuple_1__mem_write_mask_out;
        assign dmem_read_out = Pipeline___members_tuple_1__mem_read_out;
        assign dmem_read_addr_out = Pipeline___members_tuple_1__mem_read_addr_out;
        assign Pipeline___members_tuple_2__mem_data_in = dmem_read_data_in;
        assign regs__read_addr0_in = Pipeline___members_tuple_0__rs1_out;
        assign regs__read_addr1_in = Pipeline___members_tuple_0__rs2_out;
        assign regs__write_in = Pipeline___members_tuple_2__regs_write_out;
        assign regs__write_addr_in = Pipeline___members_tuple_2__regs_wr_id_out;
        assign regs__write_data_in = Pipeline___members_tuple_2__regs_data_out;
        assign regs__debugen_in=debugen_in;
    endgenerate

    always @(posedge clk) begin
        pc_tmp = pc;
        valid_tmp = valid;

        _work(reset);

        pc <= pc_tmp;
        valid <= valid_tmp;
    end

    assign imem_read_addr_out = pc;


endmodule

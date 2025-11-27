#include "pipeline.h"

using namespace cpphdl;

union Instr
{
    uint32_t raw;

    struct
    {
        uint32_t opcode : 7;
        uint32_t rd     : 5;
        uint32_t funct3 : 3;
        uint32_t rs1    : 5;
        uint32_t rs2    : 5;
        uint32_t funct7 : 7;
    } r;

    struct
    {
        uint32_t opcode : 7;
        uint32_t rd     : 5;
        uint32_t funct3 : 3;
        uint32_t rs1    : 5;
        uint32_t imm11_0: 12;
    } i;

    struct
    {
        uint32_t opcode  : 7;
        uint32_t imm4_0  : 5;
        uint32_t funct3  : 3;
        uint32_t rs1     : 5;
        uint32_t rs2     : 5;
        uint32_t imm11_5 : 7;
    } s;

    struct
    {
        uint32_t opcode    : 7;
        uint32_t imm11     : 1;
        uint32_t imm4_1    : 4;
        uint32_t funct3    : 3;
        uint32_t rs1       : 5;
        uint32_t rs2       : 5;
        uint32_t imm10_5   : 6;
        uint32_t imm12     : 1;
    } b;

    struct
    {
        uint32_t opcode    : 7;
        uint32_t rd        : 5;
        uint32_t imm31_12  : 20;
    } u;

    struct
    {
        uint32_t opcode     : 7;
        uint32_t rd         : 5;
        uint32_t imm19_12   : 8;
        uint32_t imm11      : 1;
        uint32_t imm10_1    : 10;
        uint32_t imm20      : 1;
    } j;

    int32_t sext(uint32_t val, unsigned bits)
    {
        int32_t m = 1u << (bits - 1);
        return (val ^ m) - m; 
    }

    int32_t imm_I()
    {
        return sext(i.imm11_0, 12);
    }

    int32_t imm_S()
    {
        return sext(s.imm4_0 | (s.imm11_5 << 5), 12);
    }

    int32_t imm_B()
    {
        return sext((b.imm4_1 << 1) | (b.imm11  << 11) | (b.imm10_5 << 5) | (b.imm12  << 12), 13);
    }

    int32_t imm_U()
    {
        return int32_t(u.imm31_12 << 12);
    }

    int32_t imm_J()
    {
        return sext((j.imm10_1 << 1) | (j.imm11   << 11) | (j.imm19_12 << 12) | (j.imm20   << 20), 21);
    }
};

enum Alu
{
    ANONE, ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU, PASS_RS1,
};

enum Mem
{
    MNONE, LOAD, STORE
};

enum Wb
{
    WNONE, ALU, MEMORY, PC,
};

enum Br
{
    BNONE, BEQ, BNE, BLT, BGE, BLTU, BGEU, JAL, JALR
};

template<typename STATE, size_t ID, size_t LENGTH>
class DecodeFetch: public PipelineStage
{
public:
    struct State
    {
        uint32_t pc;
        uint32_t rs1_val;
        uint32_t rs2_val;

        int32_t imm;

        uint8_t valid:1;
        uint8_t alu_op:4;
        uint8_t mem_op:2;
        uint8_t wb_op:2;
        uint8_t br_op:4;
        uint8_t rsv:3;

        uint8_t rd:5;
        uint8_t rs1:5;
        uint8_t rs2:5;
        uint8_t rsv1:1;
    };//__PACKED;
    array<State,LENGTH-ID> state_reg;

    array<uint8_t,2> regs_rdid_comb;

    bool stall_comb;

public:
    uint32_t                  *pc_in       = nullptr;
    bool                      *instr_valid = nullptr;
    Instr                     *instr_in    = nullptr;
    array<STATE,LENGTH>       *state_in    = nullptr;
    array<State,LENGTH-ID>    *state_out   = &state_reg;
    array<uint32_t,2>         *regs_in     = nullptr;  // this input works from combinationsl path of another module from regs_rdid_comb
    array<uint8_t,2>          *regs_rdid_out = &regs_rdid_comb;
    uint32_t                  *alu_result_in = nullptr;  // forwarding from Ex
    uint32_t                  *mem_in        = nullptr;  // forwarding from Mem
    bool                      *stall_out     = &stall_comb;  // comb !ready

    void connect()
    {
        std::print("DecodeFetch: {} of {}\n", ID, LENGTH);
    }

    array<uint8_t,2> regs_rdid_comb_func()
    {
        regs_rdid_comb = {};
        switch (instr_in->r.opcode)
        {
            case 0b0000011:  // LOAD  // LB, LH, LW, LBU, LHU
                regs_rdid_comb[0] = instr_in->i.rs1;
                break;
            case 0b0100011:  // STORE  // SB, SH, SW
                regs_rdid_comb[0] = instr_in->s.rs1;
                regs_rdid_comb[1] = instr_in->s.rs2;
                break;
            case 0b0010011:  // OP-IMM (immediate ALU)
                regs_rdid_comb[0] = instr_in->i.rs1;
                break;
            case 0b0110011:  // OP (register ALU)
                regs_rdid_comb[0] = instr_in->r.rs1;
                regs_rdid_comb[1] = instr_in->r.rs2;
                break;
            case 0b1100011:  // BRANCH
                regs_rdid_comb[0] = instr_in->b.rs1;
                regs_rdid_comb[1] = instr_in->b.rs2;
                break;
            case 0b1100111:  // JALR
                regs_rdid_comb[0] = instr_in->i.rs1;
                break;
        }
        return regs_rdid_comb;
    }

    bool stall_comb_func()
    {
        // hazard
        stall_comb = 0;
        if (state_reg[0].wb_op == Wb::MEMORY && state_reg[0].rd != 0) {  // Ex
            if (state_reg[0].rd == state_reg.next[0].rs1) {
                stall_comb = 1;
            }
            if (state_reg[0].rd == state_reg.next[0].rs2) {
                stall_comb = 1;
            }
        }
        if (state_reg[0].wb_op == Wb::PC) {  // Ex
            stall_comb = 1;
        }
        return stall_comb;
    }

    void do_decode_fetch()
    {
        state_reg.next[0] = {};

        state_reg.next[0].rs1 = regs_rdid_comb_func()[0];
        state_reg.next[0].rs2 = regs_rdid_comb_func()[1];

        switch (instr_in->r.opcode)
        {
            case 0b0000011:  // LOAD  // LB, LH, LW, LBU, LHU
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = instr_in->imm_I();
                state_reg.next[0].mem_op = Mem::LOAD;
                state_reg.next[0].alu_op = Alu::ADD;    // address = rs1 + imm
                state_reg.next[0].wb_op = Wb::MEMORY;
                break;

            case 0b0100011:  // STORE  // SB, SH, SW
                state_reg.next[0].imm = instr_in->imm_S();
                state_reg.next[0].mem_op = Mem::STORE;
                state_reg.next[0].alu_op = Alu::ADD;    // base + offset
                break;

            case 0b0010011:  // OP-IMM (immediate ALU)
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = instr_in->imm_I();
                state_reg.next[0].wb_op = Wb::ALU;

                switch (instr_in->i.funct3)
                {
                    case 0b000: state_reg.next[0].alu_op = Alu::ADD; break;     // ADDI
                    case 0b010: state_reg.next[0].alu_op = Alu::SLT; break;     // SLTI
                    case 0b011: state_reg.next[0].alu_op = Alu::SLTU; break;    // SLTIU
                    case 0b100: state_reg.next[0].alu_op = Alu::XOR; break;
                    case 0b110: state_reg.next[0].alu_op = Alu::OR; break;
                    case 0b111: state_reg.next[0].alu_op = Alu::AND; break;
                    case 0b001: state_reg.next[0].alu_op = Alu::SLL; break;
                    case 0b101:
                        state_reg.next[0].alu_op = (instr_in->i.imm11_0 >> 10) & 1 ? Alu::SRA : Alu::SRL;
                        break;
                }
                break;

            case 0b0110011:  // OP (register ALU)
                state_reg.next[0].rd = instr_in->r.rd;
                state_reg.next[0].wb_op = Wb::ALU;

                switch (instr_in->r.funct3)
                {
                    case 0b000:
                        state_reg.next[0].alu_op = (instr_in->r.funct7 == 0b0100000) ? Alu::SUB : Alu::ADD;
                        break;
                    case 0b111: state_reg.next[0].alu_op = Alu::AND; break;
                    case 0b110: state_reg.next[0].alu_op = Alu::OR;  break;
                    case 0b100: state_reg.next[0].alu_op = Alu::XOR; break;
                    case 0b001: state_reg.next[0].alu_op = Alu::SLL; break;
                    case 0b101: state_reg.next[0].alu_op = (instr_in->r.funct7 == 0b0100000) ? Alu::SRA : Alu::SRL; break;
                    case 0b010: state_reg.next[0].alu_op = Alu::SLT;  break;
                    case 0b011: state_reg.next[0].alu_op = Alu::SLTU; break;
                }
                break;

            case 0b1100011:  // BRANCH
                state_reg.next[0].imm = instr_in->imm_B();
                state_reg.next[0].branch = Br::BNONE;

                switch (instr_in->b.funct3)
                {
                    case 0b000: state_reg.next[0].branch = Br::BEQ; break;
                    case 0b001: state_reg.next[0].branch = Br::BNE; break;
                    case 0b100: state_reg.next[0].branch = Br::BLT; break;
                    case 0b101: state_reg.next[0].branch = Br::BGE; break;
                    case 0b110: state_reg.next[0].branch = Br::BLTU; break;
                    case 0b111: state_reg.next[0].branch = Br::BGEU; break;
                }
                break;

            case 0b1101111:  // JAL
                state_reg.next[0].rd  = instr_in->j.rd;
                state_reg.next[0].imm = instr_in->imm_J();
                state_reg.next[0].branch = Br::JAL;
                state_reg.next[0].wb_op = Wb::PC;
                break;

            case 0b1100111:  // JALR
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = instr_in->imm_I();
                state_reg.next[0].branch = Br::JALR;
                state_reg.next[0].wb_op = Wb::PC;
                break;

            case 0b0110111:  // LUI
                state_reg.next[0].rd  = instr_in->u.rd;
                state_reg.next[0].imm = instr_in->imm_U();
                state_reg.next[0].alu_op = Alu::PASS_RS1;   // or NONE, since result = imm
                state_reg.next[0].wb_op = Wb::ALU;
                break;

            case 0b0010111:  // AUIPC
                state_reg.next[0].rd  = instr_in->u.rd;
                state_reg.next[0].imm = instr_in->imm_U();
                state_reg.next[0].alu_op = Alu::ADD;  // PC + imm
                state_reg.next[0].wb_op = Wb::ALU;
                break;
        }

        // fetch

        state_reg.next[0].pc = *pc_in;
        state_reg.next[0].rs1_val = (*regs_in)[0];
        state_reg.next[0].rs2_val = (*regs_in)[1];

        // forwarding
        if (state_reg[0].wb_op == Wb::ALU && state_reg[0].rd != 0) {  // Ex
            if (state_reg[0].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = *alu_result_in;
            }
            if (state_reg[0].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = *alu_result_in;
            }
        }

        if (state_reg[1].wb_op == Wb::ALU && state_reg[1].rd != 0) {  // Mem
            if (state_reg[1].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = (*state_in)[ID+1].result_in;
            }
            if (state_reg[1].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = (*state_in)[ID+1].result_in;
            }
        }

        if (state_reg[2].wb_op == Wb::ALU && state_reg[2].rd != 0) {  // Wb
            if (state_reg[2].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = (*state_in)[ID+2].result_in;
            }
            if (state_reg[2].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = (*state_in)[ID+2].result_in;
            }
        }

        if (state_reg[1].wb_op == Wb::MEMORY && state_reg[1].rd != 0) {  // Mem
            if (state_reg[0].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = *mem_in;
            }
            if (state_reg[0].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = *mem_in;
            }
        }

        state_reg.next[0].valid = *instr_valid && stall_comb_func();
    }

};

template<typename STATE, size_t ID, size_t LENGTH>
class ExecuteCalc
{
public:
    struct State
    {
        uint32_t alu_result;
    };

    bool     branch_taken_comb;
    uint32_t branch_target_comb;

    array<State,LENGTH-ID> state_reg;

public:
    array<STATE,LENGTH>       *state_in  = nullptr;
    array<State,LENGTH-ID>    *state_out = &state_reg;
    uint32_t                  *alu_result_out = (uint32_t*)&state_reg;
    bool                      *branch_taken_out = &branch_taken_comb;
    uint32_t                  *branch_target_out = &branch_target_comb;

    void connect()
    {
        std::print("ExecuteCalc: {} of {}\n", ID, LENGTH);
    }

    bool branch_taken_comb_func()
    {
        branch_taken_comb = false;
        switch ((*state_in)[ID-1].branch)
        {
            case Br::BEQ: state_reg.next[0].branch_taken_comb = ((*state_in)[ID-1].rs1_val == (*state_in)[ID-1].rs2_val); break;
            case Br::BNE: state_reg.next[0].branch_taken_comb = ((*state_in)[ID-1].rs1_val != (*state_in)[ID-1].rs2_val); break;
            case Br::BLT: state_reg.next[0].branch_taken_comb = (int32_t((*state_in)[ID-1].rs1_val) < int32_t((*state_in)[ID-1].rs2_val)); break;
            case Br::BGE: state_reg.next[0].branch_taken_comb = (int32_t((*state_in)[ID-1].rs1_val) >= int32_t((*state_in)[ID-1].rs2_val)); break;
            case Br::BLTU: state_reg.next[0].branch_taken_comb = ((*state_in)[ID-1].rs1_val < (*state_in)[ID-1].rs2_val); break;
            case Br::BGEU: state_reg.next[0].branch_taken_comb = ((*state_in)[ID-1].rs1_val >= (*state_in)[ID-1].rs2_val); break;
            case Br::JAL: state_reg.next[0].branch_taken_comb = true; break;
            case Br::JALR: state_reg.next[0].branch_taken_comb = true; break;
            case Br::BNONE: break;
        }
        return branch_taken_comb && (*state_in)[ID-1].valid;
    }

    bool branch_target_comb_func()
    {
        branch_target_comb = 0;
        if ((*state_in)[ID-1].branch != Br::BNONE)
        {
            if ((*state_in)[ID-1].branch == Br::JAL)
                state_reg.next[0].branch_target_comb = (*state_in)[ID-1].pc + (*state_in)[ID-1].imm;
            else if ((*state_in)[ID-1].branch == Br::JALR)
                state_reg.next[0].branch_target_comb = ((*state_in)[ID-1].rs1_val + (*state_in)[ID-1].imm) & ~1u;
            else     // conditional branch
                state_reg.next[0].branch_target_comb = (*state_in)[ID-1].pc + (*state_in)[ID-1].imm;
        }
        return branch_target_comb;
    }

    void do_execute()
    {
        state_reg.next[0] = {};

        uint32_t a = (*state_in)[ID-1].rs1_val;
        uint32_t b = ((*state_in)[ID-1].alu_op == Alu::ADD && (*state_in)[ID-1].mem_op != Mem::MNONE)
                        ? uint32_t((*state_in)[ID-1].imm)      // load/store address calc uses imm
                        : (*state_in)[ID-1].rs2 ? (*state_in)[ID-1].rs2_val : uint32_t((*state_in)[ID-1].imm);

        switch ((*state_in)[ID-1].alu_op)
        {
            case Alu::ADD:  state_reg.next[0].alu_result = a + b; break;
            case Alu::SUB:  state_reg.next[0].alu_result = a - b; break;
            case Alu::AND:  state_reg.next[0].alu_result = a & b; break;
            case Alu::OR:   state_reg.next[0].alu_result = a | b; break;
            case Alu::XOR:  state_reg.next[0].alu_result = a ^ b; break;
            case Alu::SLL:  state_reg.next[0].alu_result = a << (b & 0x1F); break;
            case Alu::SRL:  state_reg.next[0].alu_result = a >> (b & 0x1F); break;
            case Alu::SRA:  state_reg.next[0].alu_result = uint32_t(int32_t(a) >> (b & 0x1F)); break;
            case Alu::SLT:  state_reg.next[0].alu_result = (int32_t(a) < int32_t(b)); break;
            case Alu::SLTU: state_reg.next[0].alu_result = (a < b); break;
            case Alu::PASS_RS1: state_reg.next[0].alu_result = a; break;
            case Alu::ANONE: break;
        }


    }



};

template<typename STATE, size_t ID, size_t LENGTH>
class MemoryAccess: public PipelineStage
{
public:
    struct State
    {
        uint32_t load_data = 0;   // valid only if Mem::LOAD
    };
    array<State,LENGTH-ID> state_reg;

    reg<u32> mem_addr_reg;
    reg<u32> mem_out_reg;
    reg<u8> mem_mask_reg;
    reg<u1> mem_write_reg;
    reg<u1> mem_read_reg;

public:
    array<STATE,LENGTH>       *state_in     = nullptr;
    array<State,LENGTH-ID>    *state_out    = &state_reg;
    uint32_t                  *mem_addr_out = &mem_addr_reg;
    uint32_t                  *mem_out      = &mem_out_reg;
    uint8_t                   *mem_mask_out = &mem_mask_reg;
    bool                      *mem_write_out = &mem_write_reg;
    bool                      *mem_read_out = &mem_read_reg;

    void connect()
    {
        std::print("MemoryAccess: {} of {}\n", ID, LENGTH);
    }

    void do_memory()
    {
        state_reg.next[0] = {};

        mem_addr_reg.next = (*state_in)[ID-1].alu_result;
        mem_out_reg.next = (*state_in)[ID-1].rs2_val;


        if ((*state_in)[ID-1].mem_op == Mem::MNONE) {
            return;
        }

        mem_write_reg.next = 0;
        if ((*state_in)[ID-1].mem_op == Mem::STORE)  // parallel case, full case
        {
            switch ((*state_in)[ID-1].funct3)
            {
                case 0b000: // LB
                    mem_write_reg.next = (*state_in)[ID-1].valid;
                    mem_mask_reg.next = 0x1;
                    break;
                case 0b001: // LH
                    mem_write_reg.next = (*state_in)[ID-1].valid;
                    mem_mask_reg.next = 0x3;
                    break;
                case 0b010: // LW
                    mem_write_reg.next = (*state_in)[ID-1].valid;
                    mem_mask_reg.next = 0xF;
                    break;
                default:
                    break;
            }
        }

        mem_read_reg.next = 0;
        if ((*state_in)[ID-1].mem_op == Mem::LOAD)
        {
            switch ((*state_in)[ID-1].funct3)
            {
                case 0b000: mem_read_reg.next = 1; break;
                case 0b001: mem_read_reg.next = 1; break;
                case 0b010: mem_read_reg.next = 1; break;
                case 0b100: mem_read_reg.next = 1; break;
                case 0b101: mem_read_reg.next = 1; break;
                default: break;
            }
        }

        if ((*state_in)[ID-1].branch_taken) {
            state_reg.next[0].next_pc = (*state_in)[ID-1].branch_target;
        }
        else {
            state_reg.next[0].next_pc = (*state_in)[ID-1].pc + 4;
        }
    }

};

template<typename STATE, size_t ID, size_t LENGTH>
class WriteBack: public PipelineStage
{
public:
    struct State
    {
        Wb op;
        uint64_t data;
        int write_to_reg;
    };
    array<State,LENGTH-ID> state_reg;

    reg<u32> regs_out_reg;
    reg<u8> regs_wrid_reg;
    reg<u1> regs_write_reg;

public:
    array<STATE,LENGTH>       *state_in      = nullptr;
    array<State,LENGTH-ID>    *state_out     = &state_reg;
    uint32_t                  *mem_in        = nullptr;
    uint32_t                  *regs_out      = &regs_out_reg;
    uint8_t                   *regs_wrid_out = &regs_wrid_reg;
    bool                      *regs_write_out = &regs_write_reg;

    void connect()
    {
        std::print("WriteBack: {} of {}\n", ID, LENGTH);
    }

    void do_writeback()
    {
        // NOTE! reg0 is ZERO, never write it
        regs_wrid_reg.next = (*state_in)[ID-1].rd;

        regs_write_reg.next = 0;
        switch ((*state_in)[ID-1].wb_op) {
            case Wb::ALU:
                regs_out_reg.next = (*state_in)[ID-1].alu_result;
                regs_write_reg.next = (*state_in)[ID-1].valid;
            break;
            case Wb::MEMORY:
                switch ((*state_in)[ID-1].funct3) {
                    case 0b000: regs_out_reg.next = int8_t(*mem_in); break;
                    case 0b001: regs_out_reg.next = int16_t(*mem_in); break;
                    case 0b010: regs_out_reg.next = int32_t(*mem_in); break;
                    case 0b100: regs_out_reg.next = uint8_t(*mem_in); break;
                    case 0b101: regs_out_reg.next = uint16_t(*mem_in); break;
                    default: regs_write_reg.next = 0; break;
                }
                regs_write_reg.next = (*state_in)[ID-1].valid;
            break;
        }
    }
};



template<size_t WIDTH>
class RiscV: public Pipeline<PipelineStages<DecodeFetch,ExecuteCalc,MemoryAccess,WriteBack>>
{



public:


    bool             debugen_in;

    void connect()
    {
        Pipeline::connect();
    }

    void work(bool clk, bool reset)
    {
        Pipeline::work(clk, reset);
    }

    void strobe()
    {
        Pipeline::strobe();
    }

    void comb()
    {
        Pipeline::comb();
    }

};

// C++HDL INLINE TEST ///////////////////////////////////////////////////

template class RiscV<32>;
template class RiscV<64>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

template<size_t WIDTH>
class TestRiscV : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL riscv;
#else
    RiscV<WIDTH> riscv;
#endif

    bool error;

    size_t i;

public:
//    array<DTYPE,LENGTH>    *data_in  = nullptr;
//    array<STYPE,LENGTH>    *data_out = &out_reg;

    bool             debugen_in;

    TestRiscV(bool debug)
    {
        debugen_in = debug;
    }

    ~TestRiscV()
    {
    }

    void connect()
    {
#ifndef VERILATOR
//        riscv.__inst_name = __inst_name + "/riscv";

//        riscv.data_in      = data_out;
        riscv.debugen_in   = debugen_in;
        riscv.connect();

//        data_in           = riscv.data_out;
#endif
    }

    void work(bool clk, bool reset)
    {
#ifndef VERILATOR
        riscv.work(clk, reset);
#else
        memcpy(&riscv.data_in.m_storage, data_out, sizeof(riscv.data_in.m_storage));
        riscv.debugen_in    = debugen_in;

        data_in           = (array<DTYPE,LENGTH>*) &riscv.data_out.m_storage;

        riscv.clk = clk;
        riscv.reset = reset;
        riscv.eval();  // eval of verilator should be in the end
#endif

        if (reset) {
            error = false;
            return;
        }

        if (!clk) {  // all checks on negedge edge
//            for (i=0; i < LENGTH; ++i) {
//                if (!reset && ((!USE_REG && can_check1 && !(*data_in)[i].cmp(was_refs1[i], 0.1))
//                             || (USE_REG && can_check2 && !(*data_in)[i].cmp(was_refs2[i], 0.1))) ) {
//                    std::print("{:s} ERROR: {}({}) was read instead of {}\n",
//                        __inst_name,
//                        (*data_in)[i].to_double(),
//                        (*data_in)[i],
//                        USE_REG?was_refs2[i]:was_refs1[i]);
//                    error = true;
//                }
//            }
            return;
        }

//        for (i=0; i < LENGTH; ++i) {
//            refs[i] = ((double)random() - RAND_MAX/2) / (RAND_MAX/2);
//            out_reg.next[i].from_double(refs[i]);
//        }
//        was_refs1.next = refs;
//        was_refs2.next = was_refs1;
//        can_check1.next = 1;
//        can_check2.next = can_check1;
    }

    void strobe()
    {
#ifndef VERILATOR
        riscv.strobe();
#endif

    }

    void comb()
    {
#ifndef VERILATOR
        riscv.comb();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestRiscV, WIDTH: {}...", WiDTH);
#else
        std::print("C++HDL TestRiscV, WIDTH: {}...", WIDTH);
#endif
        if (debugen_in) {
            std::print("\n");
        }

        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "riscv_test";
        connect();
        work(0, 1);
        work(1, 1);
        int cycles = 100000;
        int clk = 0;
        while (--cycles && !error) {
            comb();
            work(clk, 0);
            strobe();
            clk = !clk;
        }
        std::print(" {} ({} microseconds)\n", !error?"PASSED":"FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main (int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        debug = true;
    }
    if (argc > 2 && strcmp(argv[2], "--noveril") == 0) {
        noveril = true;
    }
    int only = -1;
    if (argc > 1 && argv[argc-1][0] != '-') {
        only = atoi(argv[argc-1]);
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
/*    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        ok &= VerilatorCompile("RiscV.cpp", "RiscV", {}, 32);
        ok &= VerilatorCompile("RiscV.cpp", "RiscV", {}, 64);
        std::cout << "Executing tests... ===========================================================================\n";
        std::system((std::string("RiscV_32/obj_dir/VRiscV") + (debug?" --debug":"") + " 0").c_str());
        std::system((std::string("RiscV_64/obj_dir/VRiscV") + (debug?" --debug":"") + " 1").c_str());
    }*/
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
    && ((only != -1 && only != 0) || TestRiscV<32>(debug).run())
//    && ((only != -1 && only != 1) || TestRiscV<64>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

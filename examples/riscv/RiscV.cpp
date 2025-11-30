#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "Pipeline.h"
#include "File.h"
#include "../basic/Memory.cpp"

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

#ifndef SYNTHESIS
    std::string format()
    {
        auto decode_mnemonic = [&](uint32_t op, uint32_t f3, uint32_t f7) -> std::string {
            switch (op) {
            case 0b0110011: // R-type
                if (f3 == 0 && f7 == 0b0000000) return "add";
                if (f3 == 0 && f7 == 0b0100000) return "sub";
                if (f3 == 7) return "and";
                if (f3 == 6) return "or";
                if (f3 == 4) return "xor";
                if (f3 == 1) return "sll";
                if (f3 == 5 && f7 == 0b0000000) return "srl";
                if (f3 == 5 && f7 == 0b0100000) return "sra";
                if (f3 == 2) return "slt";
                if (f3 == 3) return "sltu";
                return "r-type";
            case 0b0010011: // I-type ALU
                if (f3 == 0) return "addi";
                if (f3 == 7) return "andi";
                if (f3 == 6) return "ori";
                if (f3 == 4) return "xori";
                if (f3 == 1) return "slli";
                if (f3 == 5 && f7 == 0) return "srli";
                if (f3 == 5 && f7 == 0b0100000) return "srai";
                if (f3 == 2) return "slti";
                if (f3 == 3) return "sltiu";
                return "alu-imm";
            case 0b0000011: return "load";
            case 0b0100011: return "store";
            case 0b1100011: return "branch";
            case 0b1101111: return "jal";
            case 0b1100111: return "jalr";
            case 0b0110111: return "lui";
            case 0b0010111: return "auipc";
            default:        return "unknown";
            }
        };

        auto m = decode_mnemonic(r.opcode, r.funct3, r.funct7);
        return std::format("[{:08X}]({}){:#04x},rd:{},rs1:{},rs2:{},f3:{:#03x},f7:{:#04x},imm:{:x}/{:x}/{:x}/{:x}/{:x}\n",
            raw, m, (uint32_t)r.opcode, (uint8_t)r.rd, (uint8_t)r.rs1, (uint8_t)r.rs2, (uint8_t)r.funct3, (uint8_t)r.funct7,
            imm_I(), imm_S(), imm_B(), imm_U(), imm_J());
    }
#endif
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
        uint8_t funct3:3;

        uint8_t rd:5;
        uint8_t rs1:5;
        uint8_t rs2:5;
        uint8_t rsv1:1;
    };//__PACKED;
    reg<array<State,LENGTH-ID>> state_reg;

    array<uint8_t,2> regs_rd_id_comb;

    bool stall_comb;

public:
    uint32_t                  *pc_in       = nullptr;
    bool                      *instr_valid_in = nullptr;
    Instr                     *instr_in    = nullptr;
    array<STATE,LENGTH>       *state_in    = nullptr;
    array<State,LENGTH-ID>    *state_out   = &state_reg;
    uint32_t                  *regs_data0_in  = nullptr;  // this input works from combinationsl path of another module from regs_rd_id_comb
    uint32_t                  *regs_data1_in  = nullptr;  // this input works from combinationsl path of another module from regs_rd_id_comb
    array<uint8_t,2>          *regs_rd_id_out = &regs_rd_id_comb;
    uint32_t                  *alu_result_in = nullptr;  // forwarding from Ex
    uint32_t                  *mem_data_in   = nullptr;  // forwarding from Mem
    bool                      *stall_out     = &stall_comb;  // comb !ready

    void connect()
    {
        std::print("DecodeFetch: {} of {}\n", ID, LENGTH);
    }

    array<uint8_t,2> regs_rd_id_comb_func()
    {
        regs_rd_id_comb = {};
        switch (instr_in->r.opcode)
        {
            case 0b0000011:  // LOAD  // LB, LH, LW, LBU, LHU
                regs_rd_id_comb[0] = instr_in->i.rs1;
                break;
            case 0b0100011:  // STORE  // SB, SH, SW
                regs_rd_id_comb[0] = instr_in->s.rs1;
                regs_rd_id_comb[1] = instr_in->s.rs2;
                break;
            case 0b0010011:  // OP-IMM (immediate ALU)
                regs_rd_id_comb[0] = instr_in->i.rs1;
                break;
            case 0b0110011:  // OP (register ALU)
                regs_rd_id_comb[0] = instr_in->r.rs1;
                regs_rd_id_comb[1] = instr_in->r.rs2;
                break;
            case 0b1100011:  // BRANCH
                regs_rd_id_comb[0] = instr_in->b.rs1;
                regs_rd_id_comb[1] = instr_in->b.rs2;
                break;
            case 0b1100111:  // JALR
                regs_rd_id_comb[0] = instr_in->i.rs1;
                break;
        }
        return regs_rd_id_comb;
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
#ifndef SYNTHESIS
        std::print("{}\n", instr_in->format());
#endif

        state_reg.next[0] = {};

        state_reg.next[0].rs1 = regs_rd_id_comb_func()[0];
        state_reg.next[0].rs2 = regs_rd_id_comb_func()[1];

        state_reg.next[0].funct3 = 7;
        switch (instr_in->r.opcode)
        {
            case 0b0000011:  // LOAD  // LB, LH, LW, LBU, LHU
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = instr_in->imm_I();
                state_reg.next[0].mem_op = Mem::LOAD;
                state_reg.next[0].alu_op = Alu::ADD;    // address = rs1 + imm
                state_reg.next[0].wb_op = Wb::MEMORY;
                state_reg.next[0].funct3 = instr_in->i.funct3;
                break;

            case 0b0100011:  // STORE  // SB, SH, SW
                state_reg.next[0].imm = instr_in->imm_S();
                state_reg.next[0].mem_op = Mem::STORE;
                state_reg.next[0].alu_op = Alu::ADD;    // base + offset
                state_reg.next[0].funct3 = instr_in->i.funct3;
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
                state_reg.next[0].funct3 = instr_in->i.funct3;
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
                state_reg.next[0].funct3 = instr_in->r.funct3;
                break;

            case 0b1100011:  // BRANCH
                state_reg.next[0].imm = instr_in->imm_B();
                state_reg.next[0].br_op = Br::BNONE;

                switch (instr_in->b.funct3)
                {
                    case 0b000: state_reg.next[0].br_op = Br::BEQ; break;
                    case 0b001: state_reg.next[0].br_op = Br::BNE; break;
                    case 0b100: state_reg.next[0].br_op = Br::BLT; break;
                    case 0b101: state_reg.next[0].br_op = Br::BGE; break;
                    case 0b110: state_reg.next[0].br_op = Br::BLTU; break;
                    case 0b111: state_reg.next[0].br_op = Br::BGEU; break;
                }
                state_reg.next[0].funct3 = instr_in->b.funct3;
                break;

            case 0b1101111:  // JAL
                state_reg.next[0].rd  = instr_in->j.rd;
                state_reg.next[0].imm = instr_in->imm_J();
                state_reg.next[0].br_op = Br::JAL;
                state_reg.next[0].wb_op = Wb::PC;
                break;

            case 0b1100111:  // JALR
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = instr_in->imm_I();
                state_reg.next[0].br_op = Br::JALR;
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
        state_reg.next[0].rs1_val = *regs_data0_in;
        state_reg.next[0].rs2_val = *regs_data1_in;

        // forwarding
        if (state_reg[2].valid && state_reg[2].wb_op == Wb::ALU && state_reg[2].rd != 0) {  // Wb
            if (state_reg[2].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = (*state_in)[ID+2].alu_result;
            }
            if (state_reg[2].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = (*state_in)[ID+2].alu_result;
            }
        }

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::ALU && state_reg[1].rd != 0) {  // Mem
            if (state_reg[1].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = (*state_in)[ID+1].alu_result;
            }
            if (state_reg[1].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = (*state_in)[ID+1].alu_result;
            }
        }

        if (state_reg[0].valid && state_reg[0].wb_op == Wb::ALU && state_reg[0].rd != 0) {  // Ex
            if (state_reg[0].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = *alu_result_in;
            }
            if (state_reg[0].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = *alu_result_in;
            }
        }

        if (state_reg[1].valid && state_reg[1].wb_op == Wb::MEMORY && state_reg[1].rd != 0) {  // Mem
            if (state_reg[0].rd == state_reg.next[0].rs1) {
                state_reg.next[0].rs1_val = *mem_data_in;
            }
            if (state_reg[0].rd == state_reg.next[0].rs2) {
                state_reg.next[0].rs2_val = *mem_data_in;
            }
        }

        state_reg.next[0].valid = *instr_valid_in && stall_comb_func();
    }

    void work(bool clk, bool reset)
    {
        do_decode_fetch();
    }

    void strobe()
    {
        state_reg.strobe();
    }
};

template<typename STATE, size_t ID, size_t LENGTH>
class ExecuteCalc: public PipelineStage
{
public:
    struct State
    {
        uint32_t alu_result;
    };
    reg<array<State,LENGTH-ID>> state_reg;

    bool     branch_taken_comb;
    uint32_t branch_target_comb;

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

    void work(bool clk, bool reset)
    {
        do_execute();
    }

    void strobe()
    {
        state_reg.strobe();
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
    reg<array<State,LENGTH-ID>> state_reg;

    reg<u32> mem_addr_reg;
    reg<u32> mem_data_reg;
    reg<u8> mem_mask_reg;
    reg<u1> mem_write_reg;
    reg<u1> mem_read_reg;

public:
    array<STATE,LENGTH>       *state_in     = nullptr;
    array<State,LENGTH-ID>    *state_out    = &state_reg;
    uint32_t                  *mem_addr_out = &mem_addr_reg;
    uint32_t                  *mem_data_out = &mem_data_reg;
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
        mem_data_reg.next = (*state_in)[ID-1].rs2_val;


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
    }

    void work(bool clk, bool reset)
    {
        do_memory();
    }

    void strobe()
    {
        state_reg.strobe();
        mem_addr_reg.strobe();
        mem_data_reg.strobe();
        mem_mask_reg.strobe();
        mem_write_reg.strobe();
        mem_read_reg.strobe();
    }
};

template<typename STATE, size_t ID, size_t LENGTH>
class WriteBack: public PipelineStage
{
public:
    struct State
    {
    };
    reg<array<State,LENGTH-ID>> state_reg;

    reg<u32> regs_out_reg;
    reg<u8> regs_wr_id_reg;
    reg<u1> regs_write_reg;

public:
    array<STATE,LENGTH>       *state_in      = nullptr;
    array<State,LENGTH-ID>    *state_out     = &state_reg;
    uint32_t                  *mem_data_in   = nullptr;
    uint32_t                  *regs_data_out = &regs_out_reg;
    uint8_t                   *regs_wr_id_out = &regs_wr_id_reg;
    bool                      *regs_write_out = &regs_write_reg;

    void connect()
    {
        std::print("WriteBack: {} of {}\n", ID, LENGTH);
    }

    void do_writeback()
    {
        // NOTE! reg0 is ZERO, never write it
        regs_wr_id_reg.next = (*state_in)[ID-1].rd;

        regs_write_reg.next = 0;
        switch ((*state_in)[ID-1].wb_op) {
            case Wb::ALU:
                regs_out_reg.next = (*state_in)[ID-1].alu_result;
                regs_write_reg.next = (*state_in)[ID-1].valid;
            break;
            case Wb::MEMORY:
                switch ((*state_in)[ID-1].funct3) {
                    case 0b000: regs_out_reg.next = int8_t(*mem_data_in); break;
                    case 0b001: regs_out_reg.next = int16_t(*mem_data_in); break;
                    case 0b010: regs_out_reg.next = int32_t(*mem_data_in); break;
                    case 0b100: regs_out_reg.next = uint8_t(*mem_data_in); break;
                    case 0b101: regs_out_reg.next = uint16_t(*mem_data_in); break;
                    default: regs_write_reg.next = 0; break;
                }
                regs_write_reg.next = (*state_in)[ID-1].valid;
            break;
        }
    }

    void work(bool clk, bool reset)
    {
        do_writeback();
    }

    void strobe()
    {
        state_reg.strobe();
        regs_out_reg.strobe();
        regs_wr_id_reg.strobe();
        regs_write_reg.strobe();
    }
};



template<size_t WIDTH>
class RiscV: public Pipeline<PipelineStages<DecodeFetch,ExecuteCalc,MemoryAccess,WriteBack>>
{
    File<32/8,32>       regs;
    reg<u32>            pc;
    reg<u1>             valid;

public:

    bool     *dmem_read_out;
    bool     *dmem_write_out;
    uint8_t  *dmem_write_mask_out;
    uint32_t *dmem_write_addr_out;
    uint32_t *dmem_write_data_out;
    uint32_t *dmem_read_data_in;
    uint32_t *imem_read_addr_out;
    uint32_t *imem_read_data_in;
    bool      debugen_in;

    void connect()
    {
        std::get<0>(members).__inst_name = Pipeline::__inst_name + "/decode_fetch";
        std::get<1>(members).__inst_name = Pipeline::__inst_name + "/execute_calc";
        std::get<2>(members).__inst_name = Pipeline::__inst_name + "/memory_access";
        std::get<3>(members).__inst_name = Pipeline::__inst_name + "/write_back";

        std::get<0>(members).pc_in = &pc;
        std::get<0>(members).instr_valid_in = &valid;
        std::get<0>(members).instr_in = (Instr*) imem_read_data_in;
        std::get<0>(members).regs_data0_in = (uint32_t *) regs.read_data0_out;
        std::get<0>(members).regs_data1_in = (uint32_t *) regs.read_data1_out;
        std::get<0>(members).alu_result_in = std::get<1>(members).alu_result_out;
        std::get<0>(members).mem_data_in = dmem_read_data_in;

        Pipeline::connect();

        regs.read_addr0_in = &(*std::get<0>(members).regs_rd_id_out)[0];
        regs.read_addr1_in = &(*std::get<0>(members).regs_rd_id_out)[1];

        imem_read_addr_out = &pc;

        dmem_read_out = std::get<2>(members).mem_read_out;
        dmem_write_out = std::get<2>(members).mem_write_out;
        dmem_write_mask_out = std::get<2>(members).mem_mask_out;
        dmem_write_addr_out = std::get<2>(members).mem_addr_out;
        dmem_write_data_out = std::get<2>(members).mem_data_out;
//        dmem.read_in = std::get<2>(members).mem_read_out;

        regs.write_in = std::get<3>(members).regs_write_out;
        regs.write_addr_in = std::get<3>(members).regs_wr_id_out;
        regs.write_data_in = (logic<4UL*8>*) std::get<3>(members).regs_data_out;
    }

    void work(bool clk, bool reset)
    {
        Pipeline::work(clk, reset);

        if (!*std::get<0>(members).stall_out) {
            if (*std::get<1>(members).branch_taken_out) {
                pc.next = *std::get<1>(members).branch_target_out;
            }
            else {
                pc.next = pc + 4;
            }
        }
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
    Memory<32/8,1024>   imem;
    Memory<32/8,1024>   dmem;
#ifdef VERILATOR
    VERILATOR_MODEL riscv;
#else
    RiscV<WIDTH> riscv;
#endif

    bool imem_write;
    uint8_t imem_write_addr;
    uint32_t imem_write_data;
    bool error;

    size_t i;

public:
    uint32_t *dmem_read_data_out = (uint32_t*) dmem.read_data_out;
    uint32_t *imem_read_data_out = (uint32_t*) imem.read_data_out;
    uint8_t  *imem_write_mask_in = (uint8_t*)&ONES1024;

    bool      debugen_in;

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
        riscv.__inst_name = __inst_name + "/riscv";

        dmem.read_in = riscv.dmem_read_out;
        dmem.write_in = riscv.dmem_write_out;
        dmem.write_mask_in = (logic<4>*) riscv.dmem_write_mask_out;
        dmem.write_addr_in = (u<clog2(1024)>*)riscv.dmem_write_addr_out;
        dmem.write_data_in = (logic<32>*)riscv.dmem_write_data_out;
        imem.read_addr_in = (u<clog2(1024UL)>*) riscv.imem_read_addr_out;
        imem.read_in = ONE;
        imem.write_in = &imem_write;
        riscv.debugen_in = debugen_in;

        riscv.connect();
        dmem.connect();
        imem.connect();

        riscv.dmem_read_data_in = (uint32_t*)dmem.read_data_out;
        riscv.imem_read_data_in = (uint32_t*)imem.read_data_out;
#endif
    }

    void work(bool clk, bool reset)
    {
#ifndef VERILATOR
        riscv.work(clk, reset);
#else
//        memcpy(&riscv.data_in.m_storage, data_out, sizeof(riscv.data_in.m_storage));
        riscv.debugen_in    = debugen_in;

//        data_in           = (array<DTYPE,LENGTH>*) &riscv.data_out.m_storage;

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

    bool run(std::string filename, size_t start_offset)
    {
#ifdef VERILATOR
        std::print("VERILATOR TestRiscV, WIDTH: {}...", WiDTH);
#else
        std::print("C++HDL TestRiscV, WIDTH: {}...", WIDTH);
#endif
        if (debugen_in) {
            std::print("\n");
        }

        uint32_t ram[1024];
        FILE* fbin = fopen(filename.c_str(), "r");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        fseek(fbin, start_offset, SEEK_SET);
        fread(ram, 4*1024, 1, fbin);
        std::print("Filling memory with program\n");
        imem_write = true;
        imem.work(1, 1);
        for (size_t addr = 0; addr < 1024; ++addr) {
            imem_write_addr = addr;
            imem_write_data = ram[addr];
            imem.work(1, 0);
        }
        imem_write = false;
        fclose(fbin);

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
    && ((only != -1 && only != 0) || TestRiscV<32>(debug).run("rv32i.bin", 0x1312))
//    && ((only != -1 && only != 1) || TestRiscV<64>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

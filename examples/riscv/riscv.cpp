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


template<typename STATE, size_t ID, size_t LENGTH>
class DecodeFetch: public PipelineStage
{
public:

    enum AluOp
    {
        NONE, ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU, PASS_RS1,
    };

    enum MemOp
    {
        NONE, LOAD, STORE
    };

    enum WbSel
    {
        NONE, ALU, MEMORY, PC_PLUS_4,
    };

    enum BrType
    {
        NONE, BEQ, BNE, BLT, BGE, BLTU, BGEU, JAL, JALR
    };

    struct State
    {
        AluOp alu;
        MemOp mem;
        WbSel wb;
        BrType br;

        uint8_t    rd;
        uint8_t    rs1;
        uint8_t    rs2;
        int32_t    imm;   // sign-extended immediate
        bool       writes_rd;  // Writeback enable

        uint32_t  rs1_val;
        uint32_t  rs2_val;
        uint32_t  pc;
    };
    array<State,LENGTH-ID> state_reg;

    array<uint8_t,2> regs_rdid_comb;

public:
    uint32_t                  *pc_in       = nullptr;
    Instr                     *instr_in    = nullptr;
    STATE                     *prev_in     = nullptr;
    STATE                     *diagonal_in = nullptr;
    array<State,LENGTH-ID>    *state_out   = &state_reg;
    array<uint32_t,2>         *regs_in     = nullptr;  // this input works from combinationsl path of another module from regs_rdid_comb
    array<uint8_t,2>          *regs_rdid_out = &regs_rdid_comb;

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
        return regs_rdid1_comb;
    }

    void do_decode()
    {
        state_reg.next[0] = {};

        state_reg.next[0].rs1 = regs_rdid_comb_func()[0];
        state_reg.next[0].rs2 = regs_rdid_comb_func()[1];

        switch (instr_in->r.opcode)
        {
            case 0b0000011:  // LOAD  // LB, LH, LW, LBU, LHU
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = imm_I(*instr_in);
                state_reg.next[0].mem_op = MemOp::LOAD;
                state_reg.next[0].alu_op = AluOp::ADD;    // address = rs1 + imm
                state_reg.next[0].wb_sel = WbSel::MEMORY;
                state_reg.next[0].writes_rd = true;
                break;

            case 0b0100011:  // STORE  // SB, SH, SW
                state_reg.next[0].imm = imm_S(*instr_in);
                state_reg.next[0].mem_op = MemOp::STORE;
                state_reg.next[0].alu_op = AluOp::ADD;    // base + offset
                break;

            case 0b0010011:  // OP-IMM (immediate ALU)
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = imm_I(*instr_in);
                state_reg.next[0].writes_rd = true;
                state_reg.next[0].wb_sel = WbSel::ALU;

                switch (instr_in->i.funct3)
                {
                    case 0b000: state_reg.next[0].alu_op = AluOp::ADD; break;     // ADDI
                    case 0b010: state_reg.next[0].alu_op = AluOp::SLT; break;     // SLTI
                    case 0b011: state_reg.next[0].alu_op = AluOp::SLTU; break;    // SLTIU
                    case 0b100: state_reg.next[0].alu_op = AluOp::XOR; break;
                    case 0b110: state_reg.next[0].alu_op = AluOp::OR; break;
                    case 0b111: state_reg.next[0].alu_op = AluOp::AND; break;
                    case 0b001: state_reg.next[0].alu_op = AluOp::SLL; break;
                    case 0b101:
                        state_reg.next[0].alu_op = (instr_in->i.imm11_0 >> 10) & 1 ? AluOp::SRA : AluOp::SRL;
                        break;
                }
                break;

            case 0b0110011:  // OP (register ALU)
                state_reg.next[0].rd  = instr_in->r.rd;
                state_reg.next[0].writes_rd = true;
                state_reg.next[0].wb_sel = WbSel::ALU;

                switch (instr_in->r.funct3)
                {
                    case 0b000:
                        state_reg.next[0].alu_op = (instr_in->r.funct7 == 0b0100000) ? AluOp::SUB : AluOp::ADD;
                        break;
                    case 0b111: state_reg.next[0].alu_op = AluOp::AND; break;
                    case 0b110: state_reg.next[0].alu_op = AluOp::OR;  break;
                    case 0b100: state_reg.next[0].alu_op = AluOp::XOR; break;
                    case 0b001: state_reg.next[0].alu_op = AluOp::SLL; break;
                    case 0b101: state_reg.next[0].alu_op = (instr_in->r.funct7 == 0b0100000) ? AluOp::SRA : AluOp::SRL; break;
                    case 0b010: state_reg.next[0].alu_op = AluOp::SLT;  break;
                    case 0b011: state_reg.next[0].alu_op = AluOp::SLTU; break;
                }
                break;

            case 0b1100011:  // BRANCH
                state_reg.next[0].imm = imm_B(*instr_in);
                state_reg.next[0].branch = BranchType::NONE;

                switch (instr_in->b.funct3)
                {
                    case 0b000: state_reg.next[0].branch = BranchType::BEQ; break;
                    case 0b001: state_reg.next[0].branch = BranchType::BNE; break;
                    case 0b100: state_reg.next[0].branch = BranchType::BLT; break;
                    case 0b101: state_reg.next[0].branch = BranchType::BGE; break;
                    case 0b110: state_reg.next[0].branch = BranchType::BLTU; break;
                    case 0b111: state_reg.next[0].branch = BranchType::BGEU; break;
                }
                break;

            case 0b1101111:  // JAL
                state_reg.next[0].rd  = instr_in->j.rd;
                state_reg.next[0].imm = imm_J(*instr_in);
                state_reg.next[0].branch = BranchType::JAL;
                state_reg.next[0].wb_sel = WbSel::PC_PLUS_4;
                state_reg.next[0].writes_rd = true;
                break;

            case 0b1100111:  // JALR
                state_reg.next[0].rd  = instr_in->i.rd;
                state_reg.next[0].imm = imm_I(*instr_in);
                state_reg.next[0].branch = BranchType::JALR;
                state_reg.next[0].wb_sel = WbSel::PC_PLUS_4;
                state_reg.next[0].writes_rd = true;
                break;

            case 0b0110111:  // LUI
                state_reg.next[0].rd  = instr_in->u.rd;
                state_reg.next[0].imm = imm_U(*instr_in);
                state_reg.next[0].alu_op = AluOp::PASS_RS1;   // or NONE, since result = imm
                state_reg.next[0].wb_sel = WbSel::ALU;
                state_reg.next[0].writes_rd = true;
                break;

            case 0b0010111:  // AUIPC
                state_reg.next[0].rd  = instr_in->u.rd;
                state_reg.next[0].imm = imm_U(*instr_in);
                state_reg.next[0].alu_op = AluOp::ADD;  // PC + imm
                state_reg.next[0].wb_sel = WbSel::ALU;
                state_reg.next[0].writes_rd = true;
                break;
        }
    }

    void do_fetch()
    {
        state_reg.next[0].pc = pc;
        state_reg.next[0].rs1_val = regs_in[0];
        state_reg.next[0].rs2_val = regs_in[1];
    }
};

template<typename STATE, size_t ID, size_t LENGTH>
class ExecuteCalc
{
public:

    struct State
    {
        uint32_t alu_result;   // result of ALU (or branch target)
        bool     branch_taken;
        uint32_t branch_target;
    };

    array<State,LENGTH-ID> state_reg;

public:
    STATE                     *prev_in = nullptr;
    STATE                     *diagonal_in = nullptr;
    array<State,LENGTH-ID>    *state_out = &state_reg;

    void connect()
    {
        std::print("ExecuteCalc: {} of {}\n", ID, LENGTH);
    }

    void do_execute()
    {
        state_reg.next[0] = {};

        uint32_t a = rs1_val;
        uint32_t b = (prev_in->alu_op == AluOp::ADD && prev_in->mem_op != MemOp::NONE)
                        ? uint32_t(prev_in->imm)      // load/store address calc uses imm
                        : prev_in->rs2 ? rs2_val : uint32_t(prev_in->imm);

        switch (prev_in->alu_op)
        {
            case AluOp::ADD:  state_reg.next[0].alu_result = a + b; break;
            case AluOp::SUB:  state_reg.next[0].alu_result = a - b; break;
            case AluOp::AND:  state_reg.next[0].alu_result = a & b; break;
            case AluOp::OR:   state_reg.next[0].alu_result = a | b; break;
            case AluOp::XOR:  state_reg.next[0].alu_result = a ^ b; break;
            case AluOp::SLL:  state_reg.next[0].alu_result = a << (b & 0x1F); break;
            case AluOp::SRL:  state_reg.next[0].alu_result = a >> (b & 0x1F); break;
            case AluOp::SRA:  state_reg.next[0].alu_result = uint32_t(int32_t(a) >> (b & 0x1F)); break;
            case AluOp::SLT:  state_reg.next[0].alu_result = (int32_t(a) < int32_t(b)); break;
            case AluOp::SLTU: state_reg.next[0].alu_result = (a < b); break;
            case AluOp::PASS_RS1: state_reg.next[0].alu_result = a; break;
            case AluOp::NONE: break;
        }

        // -----------------------------------------
        // Branch handling (PC calculation happens here)
        // -----------------------------------------
        switch (prev_in->branch)
        {
            case BranchType::BEQ:
                state_reg.next[0].branch_taken = (rs1_val == rs2_val);
                break;
            case BranchType::BNE:
                state_reg.next[0].branch_taken = (rs1_val != rs2_val);
                break;
            case BranchType::BLT:
                state_reg.next[0].branch_taken = (int32_t(rs1_val) < int32_t(rs2_val));
                break;
            case BranchType::BGE:
                state_reg.next[0].branch_taken = (int32_t(rs1_val) >= int32_t(rs2_val));
                break;
            case BranchType::BLTU:
                state_reg.next[0].branch_taken = (rs1_val < rs2_val);
                break;
            case BranchType::BGEU:
                state_reg.next[0].branch_taken = (rs1_val >= rs2_val);
                break;
            case BranchType::JAL:
                state_reg.next[0].branch_taken = true;
                break;
            case BranchType::JALR:
                state_reg.next[0].branch_taken = true;
                break;
            case BranchType::NONE:
                break;
        }

        // Branch / jump target address
        if (prev_in->branch != BranchType::NONE)
        {
            if (prev_in->branch == BranchType::JAL)
                state_reg.next[0].branch_target = pc + prev_in->imm;
            else if (prev_in->branch == BranchType::JALR)
                state_reg.next[0].branch_target = (rs1_val + prev_in->imm) & ~1u;
            else     // conditional branch
                state_reg.next[0].branch_target = pc + prev_in->imm;
        }
    }


};

template<typename STATE, size_t ID, size_t LENGTH>
class MemoryAccess: public PipelineStage
{
public:

    struct State
    {
        uint32_t load_data = 0;   // valid only if MemOp::LOAD
        bool     load_valid = false;
    };
    array<State,LENGTH-ID> state_reg;

    uint32_t mem_addr_comb;
    uint32_t mem_write_comb;
    uint32_t mem_out_comb;
    uint8_t mem_mask_comb;
    bool mem_write_comb;

public:
    STATE                     *prev_in      = nullptr;
    STATE                     *diagonal_in  = nullptr;
    array<State,LENGTH-ID>    *state_out    = &state_reg;
    uint32_t                  *mem_addr_out = &mem_addr_comb;
    uint32_t                  *mem_in       = nullptr;
    uint32_t                  *mem_out      = &mem_out_comb;
    uint8_t                   *mem_mask_out = &mem_mask_comb;
    bool                      *mem_write_out = &mem_write_comb;

    void connect()
    {
        std::print("MemoryAccess: {} of {}\n", ID, LENGTH);
    }

    uint32_t mem_addr_comb_func()
    {
        return mem_addr_comb = prev_in->alu_result;
    }

    uint32_t mem_out_comb_func()
    {
        return mem_out_comb = rs2_val;
    }

    uint8_t mem_mask_comb_func()
    {
        return mem_mask_comb == MemOp::STORE ? (prev_in->funct3 == 0b000 ? 0x1 :
                                               (prev_in->funct3 == 0b001 ? 0x3 :
                                               (prev_in->funct3 == 0b010 ? 0xF : 0))) : 0;
    }

    bool mem_write_comb_func()
    {
        return mem_write_comb = prev_in->mem_op == MemOp::STORE && (prev_in->funct3 == 0b000 || prev_in->funct3 == 0b001 || prev_in->funct3 == 0b010);
    }

    void do_memory()
    {
        state_reg.next[0] = {};

        if (prev_in->mem_op == MemOp::NONE)
            return out;

        if (prev_in->mem_op == MemOp::LOAD)
        {
            state_reg.next[0].load_valid = true;

            switch (prev_in->funct3)   // load width/type is in funct3
            {
                case 0b000: // LB
                    state_reg.next[0].load_data = int8_t(*mem_in);
                    break;
                case 0b001: // LH
                    state_reg.next[0].load_data = int16_t(*mem_in);
                    break;
                case 0b010: // LW
                    state_reg.next[0].load_data = int32_t(*mem_in);
                    break;
                case 0b100: // LBU
                    state_reg.next[0].load_data = uint8_t(*mem_in);
                    break;
                case 0b101: // LHU
                    state_reg.next[0].load_data = uint16_t(*mem_in);
                    break;
                default:
                    state_reg.next[0].load_valid = false; // unsupported
                    break;
            }
        }
        else

        if (ex.branch_taken) {
            state_reg.next[0].next_pc = prev_in->branch_target;
        }
        else {
            state_reg.next[0].next_pc = prev_in->pc + 4;
        }
    }

};

template<typename STATE, size_t ID, size_t LENGTH>
class WriteBack: public PipelineStage
{
public:

    struct State
    {
        WbSel op;
        uint64_t data;
        int write_to_reg;
    };
    array<State,LENGTH-ID> state_reg;

    uint32_t regs_out_comb;
    uint8_t regs_wrid_comb;
    bool    regs_write_comb;

public:
    STATE                     *prev_in       = nullptr;
    STATE                     *diagonal_in   = nullptr;
    array<State,LENGTH-ID>    *state_out     = &state_reg;
    uint32_t                  *regs_out      = &regs_out_comb;
    uint8_t                   *regs_wrid_out   = &regs_id_comb;
    bool                      *regs_write_out = &regs_write_comb;

    void connect()
    {
        std::print("WriteBack: {} of {}\n", ID, LENGTH);
    }

    uint32_t regs_out_comb()
    {
        return regs_out_comb = prev_in->writes_rd ? prev_in->wb_sel == WbSel::ALU ? prev_in->alu_result :
                                                   (prev_in->wb_sel == WbSel::MEMORY && prev_in->load_valid ? prev_in->load_data :
                                                   (prev_in->wb_sel == WbSel::PC_PLUS_4 ? prev_in->branch_target : 0) ) : 0;  // can we use "full_case" or "parallel_case" here?
    }

    uint8_t regs_wrid_comb()
    {
        // NOTE! reg0 is ZERO, never write it
        return regs_wrid_comb = prev_in->rd;
    }

    bool regs_write_comb()
    {
        // NOTE! reg0 is ZERO, never write it
        return regs_write_comb = prev_in->writes_rd ? prev_in->wb_sel == WbSel::ALU ? true :
                                                   (prev_in->wb_sel == WbSel::MEMORY && prev_in->load_valid ? true :
                                                   (prev_in->wb_sel == WbSel::PC_PLUS_4 ? true : false) ) : false;
    }

    void do_writeback()
    {

    }
};



template<size_t WIDTH>
class RiscV: public Pipeline<PipelineStages<DecodeFetch,ExecuteCalc,MemoryAccess,WriteBack>>
{



public:


    bool             debugen_in;

//    void connect()
//    {
//    }

//    void work(bool clk, bool reset)
//    {
//    }

//    void strobe()
//    {
//    }

//    void comb()
//    {
//    }

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

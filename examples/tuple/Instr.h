#pragma once

#include <stdint.h>

constexpr const char* AOPS[] =
  {"ANONE", "ADD", "SUB", "AND", "OR", "XOR", "SLL", "SRL", "SRA", "SLT", "SLTU", "PASS", "MUL", "MULH", "DIV", "REM"};
enum Alu
{
    ANONE,   ADD,   SUB,   AND,   OR,   XOR,   SLL,   SRL,   SRA,   SLT,   SLTU,   PASS,   MUL,   MULH,   DIV,   REM
};

constexpr const char* MOPS[] =
  {"MNONE", "LOAD", "STORE"};
enum Mem
{
    MNONE,   LOAD,   STORE
};

constexpr const char* WOPS[] =
  {"WNONE", "ALU", "MEM", "PC2", "PC4"};
enum Wb
{
    WNONE,   ALU,   MEM,   PC2,   PC4
};

constexpr const char* BOPS[] =
  {"BNONE", "BEQ", "BNE", "BLT", "BGE", "BLTU", "BGEU", "JAL", "JALR", "JR", "BEQZ", "BNEZ"};
enum Br
{
    BNONE,   BEQ,   BNE,   BLT,   BGE,   BLTU,   BGEU,   JAL,   JALR,   JR,   BEQZ,   BNEZ
};

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
    }__PACKED r;
    struct
    {
        uint32_t opcode : 7;
        uint32_t rd     : 5;
        uint32_t funct3 : 3;
        uint32_t rs1    : 5;
        uint32_t imm11_0: 12;
    }__PACKED i;
    struct
    {
        uint32_t opcode  : 7;
        uint32_t imm4_0  : 5;
        uint32_t funct3  : 3;
        uint32_t rs1     : 5;
        uint32_t rs2     : 5;
        uint32_t imm11_5 : 7;
    }__PACKED s;
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
    }__PACKED b;
    struct
    {
        uint32_t opcode    : 7;
        uint32_t rd        : 5;
        uint32_t imm31_12  : 20;
    }__PACKED u;
    struct
    {
        uint32_t opcode     : 7;
        uint32_t rd         : 5;
        uint32_t imm19_12   : 8;
        uint32_t imm11      : 1;
        uint32_t imm10_1    : 10;
        uint32_t imm20      : 1;
    }__PACKED j;

    int32_t sext(uint32_t val, unsigned bits)
    {
        int32_t m = 1u<<(bits - 1);
        return (val ^ m) - m; 
    }

    int32_t imm_I() { return sext(i.imm11_0, 12); }
    int32_t imm_S() { return sext(s.imm4_0 | (s.imm11_5<<5), 12); }
    int32_t imm_B() { return sext((b.imm4_1<<1) | (b.imm11<<11) | (b.imm10_5<<5) | (b.imm12<<12), 13); }
    int32_t imm_U() { return int32_t(u.imm31_12<<12); }
    int32_t imm_J() { return sext((j.imm10_1<<1) | (j.imm11<<11) | (j.imm19_12<<12) | (j.imm20<<20), 21); }

    template<typename STATE>
    void decode(STATE& state_out)
    {
        state_out = {};

        if (r.opcode == 0b0000011) {  // LOAD  // LB, LH, LW, LBU, LHU
            state_out.rd  = i.rd;
            state_out.imm = imm_I();
            state_out.mem_op = Mem::LOAD;
            state_out.alu_op = Alu::ADD;    // address = rs1 + imm
            state_out.wb_op = Wb::MEM;
            state_out.funct3 = i.funct3;
            state_out.rs1 = i.rs1;
        }
        else if (r.opcode == 0b0100011) {  // STORE  // SB, SH, SW
            state_out.imm = imm_S();
            state_out.mem_op = Mem::STORE;
            state_out.alu_op = Alu::ADD;    // base + offset
            state_out.funct3 = s.funct3;
            state_out.rs1 = s.rs1;
            state_out.rs2 = s.rs2;
        }
        else if (r.opcode == 0b0010011) {  // OP-IMM (immediate ALU)
            state_out.rd  = i.rd;
            state_out.imm = imm_I();
            state_out.wb_op = Wb::ALU;
            switch (i.funct3) {
                case 0b000: state_out.alu_op = Alu::ADD; break;     // ADDI
                case 0b010: state_out.alu_op = Alu::SLT; break;     // SLTI
                case 0b011: state_out.alu_op = Alu::SLTU; break;    // SLTIU
                case 0b100: state_out.alu_op = Alu::XOR; break;
                case 0b110: state_out.alu_op = Alu::OR; break;
                case 0b111: state_out.alu_op = Alu::AND; break;
                case 0b001: state_out.alu_op = Alu::SLL; break;
                case 0b101: state_out.alu_op = (i.imm11_0>>10) & 1 ? Alu::SRA : Alu::SRL; break;
            }
            state_out.funct3 = i.funct3;
            state_out.rs1 = i.rs1;
        }
        else if (r.opcode == 0b0110011) {  // OP (register ALU)
            state_out.rd = r.rd;
            state_out.wb_op = Wb::ALU;
            switch (r.funct3) {
                case 0b000: state_out.alu_op = (r.funct7 == 0b0100000) ? Alu::SUB : ((r.funct7 == 0b0000001) ? Alu::MUL : Alu::ADD); break;
                case 0b111: state_out.alu_op = (r.funct7 == 0b0000001) ? Alu::REM : Alu::AND; break;
                case 0b110: state_out.alu_op = Alu::OR;  break;
                case 0b100: state_out.alu_op = Alu::XOR; break;
                case 0b001: state_out.alu_op = Alu::SLL; break;
                case 0b101: state_out.alu_op = (r.funct7 == 0b0100000) ? Alu::SRA : ((r.funct7 == 0b0000001) ? Alu::DIV : Alu::SRL); break;
                case 0b010: state_out.alu_op = Alu::SLT;  break;
                case 0b011: state_out.alu_op = (r.funct7 == 0b0000001) ? Alu::MULH : Alu::SLTU; break;
            }
            state_out.funct3 = r.funct3;
            state_out.rs1 = r.rs1;
            state_out.rs2 = r.rs2;
        }
        else if (r.opcode == 0b1100011) {  // BRANCH
            state_out.imm = imm_B();
            state_out.br_op = Br::BNONE;
            switch (b.funct3) {
                case 0b000: state_out.br_op = Br::BEQ; state_out.alu_op = Alu::SLTU; break;
                case 0b001: state_out.br_op = Br::BNE; state_out.alu_op = Alu::SLTU; break;
                case 0b100: state_out.br_op = Br::BLT; state_out.alu_op = Alu::SLT; break;
                case 0b101: state_out.br_op = Br::BGE; state_out.alu_op = Alu::SLT; break;
                case 0b110: state_out.br_op = Br::BLTU; state_out.alu_op = Alu::SLTU; break;
                case 0b111: state_out.br_op = Br::BGEU; state_out.alu_op = Alu::SLTU; break;
            }
            state_out.funct3 = b.funct3;
            state_out.rs1 = b.rs1;
            state_out.rs2 = b.rs2;
        }
        else if (r.opcode == 0b1101111) {  // JAL
            state_out.rd  = j.rd;
            state_out.imm = imm_J();
            state_out.br_op = Br::JAL;
            state_out.wb_op = Wb::PC4;
        }
        else if (r.opcode == 0b1100111) {  // JALR
            state_out.rd  = i.rd;
            state_out.imm = imm_I();
            state_out.br_op = Br::JALR;
            state_out.wb_op = Wb::PC4;
            state_out.rs1 = i.rs1;
        }
        else if (r.opcode == 0b0110111) {  // LUI
            state_out.rd  = u.rd;
            state_out.imm = imm_U();
            state_out.alu_op = Alu::PASS;   // or NONE, since result = imm
            state_out.wb_op = Wb::ALU;
        }
        else if (r.opcode == 0b0010111) {  // AUIPC
            state_out.rd  = u.rd;
            state_out.imm = imm_U();
            state_out.alu_op = Alu::ADD;  // PC + imm
            state_out.wb_op = Wb::ALU;
        }
//      else if (r.opcode == 0b1110011) {  // CSR
//            switch (i.funct3) {
//            case 0b001:  // CSRRW
//            case 0b010:  // CSRRS
//            case 0b011:  // CSRRC
//                regs_rd_id_comb[0] = i.rs1;
//            break;
//        }
    }

    ///////////////////////////////////////////////// compressed

    struct {
        uint16_t opcode  : 2;
        uint16_t rd_p    : 3;
        uint16_t bits6_5 : 2;
        uint16_t rs1_p   : 3;
        uint16_t bits11_10: 2;
        uint16_t b12     : 1;
        uint16_t funct3  : 3;
    }__PACKED c;
    struct {
        uint16_t opcode  : 2;
        uint16_t rd_p    : 3;
        uint16_t generic : 2;
        uint16_t rs1     : 5;
        uint16_t b12     : 1;
        uint16_t funct3  : 3;
    }__PACKED q1;
    struct {
        uint16_t opcode  : 2;
        uint16_t rs2     : 5;
        uint16_t rs1     : 5;
        uint16_t b12     : 1;
        uint16_t funct3  : 3;
    }__PACKED q2;

    uint32_t bit(int lo) { return (raw>>lo) & 1; }
    uint32_t bits(int hi, int lo) { return (raw>>lo) & ((1u<<(hi - lo + 1)) - 1); }

    template<typename STATE>
    void decode16(STATE& state_out)
    {
        int32_t imm_tmp;
        state_out = {};
        state_out.funct3 = 0b010;  // LW/SW

        if (c.opcode == 0b00) {
            if (c.funct3 == 0b000) {  // ADDI4SPN
                state_out.rd = c.rd_p+8;
                state_out.rs1 = 2; // sp
                state_out.imm = (bits(10,7) << 6) | (bits(12,11) << 4) | (bits(6,5) << 2);
                state_out.alu_op = Alu::ADD;
                state_out.wb_op  = Wb::ALU;
            }
            else if (c.funct3 == 0b010) {  // LW
                state_out.rd = c.rd_p+8;
                state_out.rs1 = c.rs1_p+8;
                state_out.imm = (bit(5)<<6) | (bits(12,10)<<3) | (bit(6)<<2);
                state_out.alu_op = Alu::ADD;
                state_out.mem_op = Mem::LOAD;
                state_out.wb_op  = Wb::MEM;
            }
            else if (c.funct3 == 0b110) {  // SW
                state_out.rs1 = c.rs1_p+8;
                state_out.rs2 = c.rd_p+8;
                state_out.imm = (bit(5)<<6) | (bits(12,10)<<3) | (bit(6)<<2);
                state_out.alu_op = Alu::ADD;
                state_out.mem_op = Mem::STORE;
            }
        }
        else if (c.opcode == 0b01) {
            if (c.funct3 == 0b000) {  // ADDI
                state_out.rd = q1.rs1;
                state_out.rs1 = q1.rs1;
                imm_tmp = (bit(12) << 5) | bits(6,2);
                imm_tmp = (imm_tmp << 26) >> 26;
                state_out.imm = imm_tmp;
                state_out.alu_op = Alu::ADD;
                state_out.wb_op  = Wb::ALU;
            }
            else if (c.funct3 == 0b001) {  // JAL
                state_out.rd = 1;
                state_out.wb_op = Wb::PC2;
                state_out.br_op = Br::JAL;
                state_out.imm = (c.b12<<11)|(bit(8)<<10)|(bits(10,9)<<8)|(bit(6)<<7)|(bit(7)<<6)|(bit(2)<<5)|(bit(11)<<4)|(bits(5,3)<<1);
            }
            else if (c.funct3 == 0b010) {  // LI
                state_out.rd = q1.rs1;
                imm_tmp = (bit(12) << 5) | bits(6, 2);
                imm_tmp = (imm_tmp << 26) >> 26;
                state_out.imm = imm_tmp;
                state_out.alu_op = Alu::PASS;
                state_out.wb_op = Wb::ALU;
            }
            else if (c.funct3 == 0b011) {  // ADDI16SP
                state_out.rd = 2;
                state_out.rs1 = 2; // sp
                imm_tmp = (bit(12) << 9) | (bit(4) << 8) | (bit(3) << 7) | (bit(5) << 6) | (bit(2) << 5) | (bit(6) << 4);
                imm_tmp = (imm_tmp << 22) >> 22;
                state_out.imm = imm_tmp;
                state_out.alu_op = Alu::ADD;
                state_out.wb_op = Wb::ALU;
            }
            else if (c.funct3 == 0b100) {
                if (c.bits11_10 == 0) {  // C.SRLI
                    state_out.rd = c.rs1_p + 8;
                    state_out.rs1 = c.rs1_p + 8;
                    state_out.imm = bits(6, 2);
                    state_out.alu_op = Alu::SRL;
                    state_out.wb_op = Wb::ALU;
                }
                else if (c.bits11_10 == 1) {  // C.SRAI
                    state_out.rd = c.rs1_p + 8;
                    state_out.rs1 = c.rs1_p + 8;
                    state_out.imm = bits(6, 2);
                    state_out.alu_op = Alu::SRA;
                    state_out.wb_op = Wb::ALU;
                }
                else if (c.bits11_10 == 2) {  // C.ANDI
                    state_out.rd = c.rs1_p + 8;
                    state_out.rs1 = c.rs1_p + 8;
                    imm_tmp = (bit(12) << 5) | bits(6,2);
                    imm_tmp = (imm_tmp << 26) >> 26;
                    state_out.imm = imm_tmp;
                    state_out.alu_op = Alu::AND;
                    state_out.wb_op = Wb::ALU;
                }
                else if (c.bits11_10 == 3 && c.b12 == 0) {  // C.SUB, C.XOR, C.OR, C.AND
                    state_out.rd = q2.rs1;
                    state_out.rs1 = q2.rs1;
                    state_out.rs2 = q2.rs2;
                    state_out.alu_op = c.bits6_5 == 0 ? Alu::SUB : ( c.bits6_5 == 1 ? Alu::XOR : ( c.bits6_5 == 2 ? Alu::OR : Alu::AND ) );
                    state_out.wb_op = Wb::ALU;
                }
            }
            else if (c.funct3 == 0b101) {  // J
                state_out.rd = 0;
                state_out.br_op = Br::JAL;
                state_out.imm = (c.b12<<11)|(bit(8)<<10)|(bits(10,9)<<8)|(bit(6)<<7)|(bit(7)<<6)|(bit(2)<<5)|(bit(11)<<4)|(bits(5,3)<<1);
            }
            else if (c.funct3 == 0b110) {  // BEQZ
                state_out.rs1 = c.rs1_p+8;
                state_out.br_op = Br::BEQZ;
                state_out.alu_op = Alu::SLTU;
                state_out.imm = (c.b12<<8)|(bits(6,5)<<6)|(bit(2)<<5)|(bits(11,10)<<3)|(bits(4,3)<<1);
                if (c.b12) {
                    state_out.imm |= ~0x1FF;
                }
            }
            else if (c.funct3 == 0b111) {  // BNEZ
                state_out.rs1 = c.rs1_p+8;
                state_out.br_op = Br::BNEZ;
                state_out.alu_op = Alu::SLTU;
                state_out.imm = (c.b12<<8)|(bits(6,5)<<6)|(bit(2)<<5)|(bits(11,10)<<3)|(bits(4,3)<<1);
                if (c.b12) {
                    state_out.imm |= ~0x1FF;
                }
            }
        }
        else if (c.opcode == 0b10) {
            if (c.funct3 == 0b000) {  // SLLI
                state_out.rd = q2.rs1;
                state_out.rs1 = q2.rs1;
                state_out.imm = (c.b12<<5) | bits(6,2);
                state_out.alu_op = Alu::SLL;
                state_out.wb_op  = Wb::ALU;
            }
            else if (c.funct3 == 0b010) {  // LWSP
                state_out.rd = q2.rs1;
                state_out.rs1 = 2;  // sp
                state_out.imm = (c.b12<<5) | (bits(6,4)<<2) | (bits(3,2)<<6);
                state_out.alu_op = Alu::ADD;
                state_out.mem_op = Mem::LOAD;
                state_out.wb_op  = Wb::MEM;
            }
            else if (c.funct3 == 0b100) {
                if (q2.rs2 != 0) {  // C.MV
                    state_out.rd = q2.rs1;
                    state_out.rs1 = q2.rs1;
                    state_out.rs2 = q2.rs2;
                    state_out.alu_op = c.b12 == 0 ? Alu::PASS : Alu::ADD;
                    state_out.wb_op  = Wb::ALU;
                }
                else if (q2.rs2 == 0 && c.b12 == 0) {  // C.JR
                    state_out.rs1 = q2.rs1;
                    state_out.br_op = Br::JR;
                    state_out.wb_op = Wb::PC2;
                }
                else if (q2.rs2 == 0 && c.b12 == 1) {  // C.JALR
                    state_out.rs1 = q2.rs2;
                    state_out.rd = 1;
                    state_out.br_op = Br::JALR;
                    state_out.wb_op = Wb::PC2;
                }
            }
            else if (c.funct3 == 0b110) {  // SWSP
                state_out.rs1 = 2;  // sp
                state_out.rs2 = q2.rs2;
                state_out.imm = (bits(8,7)<<6) | (bits(12,9)<<2);
                state_out.mem_op = Mem::STORE;
                state_out.alu_op = Alu::ADD;
            }
        }
    }

//#ifndef SYNTHESIS
    std::string mnemonic()
    {
        uint32_t op, f3, f7, b12, rs2, bits6_5, quadrant;
        if ((raw&3) == 3) {
            op = r.opcode;
            f3 = r.funct3;
            f7 = r.funct7;
            switch (op) {
            case 0b0110011:  // R-type
                if (f3 == 0 && f7 == 0b0000000) return "add   ";
                if (f3 == 0 && f7 == 0b0100000) return "sub   ";
                if (f3 == 0 && f7 == 0b0000001) return "mul   ";
                if (f3 == 7 && f7 == 0b0000001) return "remu  ";
                if (f3 == 7) return "and   ";
                if (f3 == 6) return "or    ";
                if (f3 == 4) return "xor   ";
                if (f3 == 1) return "sll   ";
                if (f3 == 5 && f7 == 0b0000000) return "srl   ";
                if (f3 == 5 && f7 == 0b0100000) return "sra   ";
                if (f3 == 5 && f7 == 0b0000001) return "divu  ";
                if (f3 == 2) return "slt   ";
                if (f3 == 3 && f7 == 0b0000001) return "mulhu ";
                if (f3 == 3) return "sltu  ";
                return "r-type";
            case 0b0010011:  // I-type ALU
                if (f3 == 0) return "addi  ";
                if (f3 == 7) return "andi  ";
                if (f3 == 6) return "ori   ";
                if (f3 == 4) return "xori  ";
                if (f3 == 1) return "slli  ";
                if (f3 == 5 && f7 == 0) return "srli  ";
                if (f3 == 5 && f7 == 0b0100000) return "srai  ";
                if (f3 == 2) return "slti  ";
                if (f3 == 3) return "sltiu ";
                return "aluimm";
            case 0b0000011: return "load  ";
            case 0b0100011: return "store ";
            case 0b1100011: return "branch";
            case 0b1101111: return "jal   ";
            case 0b1100111: return "jalr  ";
            case 0b0110111: return "lui   ";
            case 0b0010111: return "auipc ";
            default:        return "unknwn";
            }
        }
        else {
            op = r.opcode;
            f3 = c.funct3;
            b12 = c.b12;
            rs2 = q2.rs2;
            bits6_5 = c.bits6_5;
            quadrant = op & 0b11;
            switch (quadrant) {
            case 0b00:
                switch (f3) {
                    case 0b000: return "addi4s";
                    case 0b010: return "lw    ";
                    case 0b110: return "sw    ";
                    case 0b011: return "ld    ";   // RV64/128
                    case 0b111: return "sd    ";   // RV64/128
                    default:    return "rsrvd ";
                }
            case 0b01:
                switch (f3) {
                    case 0b000: return "addi  ";
                    case 0b001: return "jal   ";      // RV32 only
                    case 0b010: return "li    ";
                    case 0b011: return "addisp"; // or lui
                    case 0b100: {
                        if (c.bits11_10 == 0) { return "srli  "; }
                        if (c.bits11_10 == 1) { return "srai  "; }
                        if (c.bits11_10 == 2) { return "andi  "; }
                        if (c.bits11_10 == 3 && b12 == 0 && bits6_5 == 0) { return "sub   "; }
                        if (c.bits11_10 == 3 && b12 == 0 && bits6_5 == 1) { return "xor   "; }
                        if (c.bits11_10 == 3 && b12 == 0 && bits6_5 == 2) { return "or    "; }
                        if (c.bits11_10 == 3 && b12 == 0 && bits6_5 == 3) { return "and   "; }
                        return "illgl ";
                    }
                    case 0b101: return "j     ";
                    case 0b110: return "beqz  ";
                    case 0b111: return "bnez  ";
                }
            case 0b10:
                switch (f3) {
                    case 0b000: return "slli  ";
                    case 0b001: return "fldsp ";
                    case 0b010: return "lwsp  ";
                    case 0b100: {
                        if (rs2 != 0 && b12 == 0) { return "mv    "; }
                        if (rs2 != 0 && b12 == 1) { return "add   "; }
                        if (rs2 == 0 && b12 == 0) { return "jr    "; }
                        if (rs2 == 0 && b12 == 1) { return "jalr  "; }
                        return "illgl ";
                    }
                    case 0b110: return "swsp  ";
                    case 0b011: return "ldsp  ";  // RV64/128
                    case 0b111: return "sdsp  ";  // RV64/128
                    default:    return "rsrvd ";
                }
            }
        }
        return "unknwn";
    }
//#endif
}__PACKED;

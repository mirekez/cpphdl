#include "Rv32im.h"
#include "riscv_opcode_cases.generated.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t bits(uint32_t raw, unsigned hi, unsigned lo)
{
    return (raw >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

constexpr uint32_t bit(uint32_t raw, unsigned pos)
{
    return (raw >> pos) & 1u;
}

constexpr int32_t sext(uint32_t value, unsigned width)
{
    uint32_t sign = 1u << (width - 1);
    return int32_t((value ^ sign) - sign);
}

constexpr uint32_t imm_i(uint32_t raw)
{
    return uint32_t(sext(bits(raw, 31, 20), 12));
}

constexpr uint32_t imm_s(uint32_t raw)
{
    return uint32_t(sext((bits(raw, 31, 25) << 5) | bits(raw, 11, 7), 12));
}

constexpr uint32_t imm_b(uint32_t raw)
{
    uint32_t imm = (bit(raw, 31) << 12) | (bit(raw, 7) << 11) |
                   (bits(raw, 30, 25) << 5) | (bits(raw, 11, 8) << 1);
    return uint32_t(sext(imm, 13));
}

constexpr uint32_t imm_u(uint32_t raw)
{
    return raw & 0xfffff000u;
}

constexpr uint32_t imm_j(uint32_t raw)
{
    uint32_t imm = (bit(raw, 31) << 20) | (bits(raw, 19, 12) << 12) |
                   (bit(raw, 20) << 11) | (bits(raw, 30, 21) << 1);
    return uint32_t(sext(imm, 21));
}

constexpr uint32_t c_imm_i(uint32_t raw)
{
    return uint32_t(sext((bit(raw, 12) << 5) | bits(raw, 6, 2), 6));
}

constexpr uint32_t c_imm_addi4spn(uint32_t raw)
{
    return (bits(raw, 10, 7) << 6) | (bits(raw, 12, 11) << 4) |
           (bits(raw, 6, 5) << 2);
}

constexpr uint32_t c_imm_lw_sw(uint32_t raw)
{
    return (bit(raw, 5) << 6) | (bits(raw, 12, 10) << 3) | (bit(raw, 6) << 2);
}

constexpr uint32_t c_imm_addi16sp(uint32_t raw)
{
    uint32_t imm = (bit(raw, 12) << 9) | (bit(raw, 4) << 8) |
                   (bit(raw, 3) << 7) | (bit(raw, 5) << 6) |
                   (bit(raw, 2) << 5) | (bit(raw, 6) << 4);
    return uint32_t(sext(imm, 10));
}

constexpr uint32_t c_imm_j(uint32_t raw)
{
    uint32_t imm = (bit(raw, 12) << 11) | (bit(raw, 8) << 10) |
                   (bits(raw, 10, 9) << 8) | (bit(raw, 6) << 7) |
                   (bit(raw, 7) << 6) | (bit(raw, 2) << 5) |
                   (bit(raw, 11) << 4) | (bits(raw, 5, 3) << 1);
    return uint32_t(sext(imm, 12));
}

constexpr uint32_t c_imm_branch(uint32_t raw)
{
    uint32_t imm = (bit(raw, 12) << 8) | (bits(raw, 6, 5) << 6) |
                   (bit(raw, 2) << 5) | (bits(raw, 11, 10) << 3) |
                   (bits(raw, 4, 3) << 1);
    return uint32_t(sext(imm, 9));
}

constexpr uint32_t c_imm_lwsp(uint32_t raw)
{
    return (bit(raw, 12) << 5) | (bits(raw, 6, 4) << 2) | (bits(raw, 3, 2) << 6);
}

constexpr uint32_t c_imm_swsp(uint32_t raw)
{
    return (bits(raw, 8, 7) << 6) | (bits(raw, 12, 9) << 2);
}

constexpr uint8_t rd(uint32_t raw) { return uint8_t(bits(raw, 11, 7)); }
constexpr uint8_t rs1(uint32_t raw) { return uint8_t(bits(raw, 19, 15)); }
constexpr uint8_t rs2(uint32_t raw) { return uint8_t(bits(raw, 24, 20)); }
constexpr uint8_t funct3(uint32_t raw) { return uint8_t(bits(raw, 14, 12)); }
constexpr uint8_t crd_p(uint32_t raw) { return uint8_t(bits(raw, 4, 2) + 8); }
constexpr uint8_t crs1_p(uint32_t raw) { return uint8_t(bits(raw, 9, 7) + 8); }
constexpr uint8_t crs2_p(uint32_t raw) { return uint8_t(bits(raw, 4, 2) + 8); }
constexpr uint8_t crd_rs1(uint32_t raw) { return uint8_t(bits(raw, 11, 7)); }
constexpr uint8_t crs2(uint32_t raw) { return uint8_t(bits(raw, 6, 2)); }

State expected(uint32_t imm = 0, Alu alu = Alu::ANONE, Mem mem = Mem::MNONE,
               Wb wb = Wb::WNONE, Br br = Br::BNONE, uint8_t f3 = 0,
               uint8_t rd_id = 0, uint8_t rs1_id = 0, uint8_t rs2_id = 0)
{
    State s{};
    s.imm = imm;
    s.alu_op = alu;
    s.mem_op = mem;
    s.wb_op = wb;
    s.br_op = br;
    s.funct3 = f3;
    s.rd = rd_id;
    s.rs1 = rs1_id;
    s.rs2 = rs2_id;
    return s;
}

bool state_for(std::string_view name, uint32_t raw, State& out)
{
    if (name == "lb" || name == "lh" || name == "lw" || name == "lbu" || name == "lhu") {
        out = expected(imm_i(raw), Alu::ADD, Mem::LOAD, Wb::MEM, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw));
    } else if (name == "sb" || name == "sh" || name == "sw") {
        out = expected(imm_s(raw), Alu::ADD, Mem::STORE, Wb::WNONE, Br::BNONE,
                       funct3(raw), 0, rs1(raw), rs2(raw));
    } else if (name == "addi") {
        out = expected(imm_i(raw), Alu::ADD, Mem::MNONE, Wb::ALU, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw));
    } else if (name == "slti") {
        out = expected(imm_i(raw), Alu::SLT, Mem::MNONE, Wb::ALU, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw));
    } else if (name == "sltiu") {
        out = expected(imm_i(raw), Alu::SLTU, Mem::MNONE, Wb::ALU, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw));
    } else if (name == "xori") {
        out = expected(imm_i(raw), Alu::XOR, Mem::MNONE, Wb::ALU, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw));
    } else if (name == "ori") {
        out = expected(imm_i(raw), Alu::OR, Mem::MNONE, Wb::ALU, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw));
    } else if (name == "andi") {
        out = expected(imm_i(raw), Alu::AND, Mem::MNONE, Wb::ALU, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw));
    } else if (name == "slli" || name == "srli" || name == "srai") {
        Alu op = name == "slli" ? Alu::SLL : (name == "srli" ? Alu::SRL : Alu::SRA);
        out = expected(imm_i(raw), op, Mem::MNONE, Wb::ALU, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw));
    } else if (name == "add" || name == "sub" || name == "sll" || name == "slt" ||
               name == "sltu" || name == "xor" || name == "srl" || name == "sra" ||
               name == "or" || name == "and" || name == "mul" || name == "mulh" ||
               name == "mulhsu" || name == "mulhu" || name == "div" || name == "divu" ||
               name == "rem" || name == "remu") {
        Alu op = Alu::ANONE;
        if (name == "add") op = Alu::ADD;
        if (name == "sub") op = Alu::SUB;
        if (name == "sll") op = Alu::SLL;
        if (name == "slt") op = Alu::SLT;
        if (name == "sltu") op = Alu::SLTU;
        if (name == "xor") op = Alu::XOR;
        if (name == "srl") op = Alu::SRL;
        if (name == "sra") op = Alu::SRA;
        if (name == "or") op = Alu::OR;
        if (name == "and") op = Alu::AND;
        if (name == "mul") op = Alu::MUL;
        if (name == "mulh") op = Alu::MULH;
        if (name == "mulhsu") op = Alu::MULHSU;
        if (name == "mulhu") op = Alu::MULHU;
        if (name == "div") op = Alu::DIV;
        if (name == "divu") op = Alu::DIVU;
        if (name == "rem") op = Alu::REM;
        if (name == "remu") op = Alu::REMU;
        out = expected(0, op, Mem::MNONE, Wb::ALU, Br::BNONE,
                       funct3(raw), rd(raw), rs1(raw), rs2(raw));
    } else if (name == "beq" || name == "bne" || name == "blt" || name == "bge" ||
               name == "bltu" || name == "bgeu") {
        Br br = Br::BNONE;
        Alu op = Alu::SLTU;
        if (name == "beq") br = Br::BEQ;
        if (name == "bne") br = Br::BNE;
        if (name == "blt") { br = Br::BLT; op = Alu::SLT; }
        if (name == "bge") { br = Br::BGE; op = Alu::SLT; }
        if (name == "bltu") br = Br::BLTU;
        if (name == "bgeu") br = Br::BGEU;
        out = expected(imm_b(raw), op, Mem::MNONE, Wb::WNONE, br,
                       funct3(raw), 0, rs1(raw), rs2(raw));
    } else if (name == "jal") {
        out = expected(imm_j(raw), Alu::ANONE, Mem::MNONE, Wb::PC4, Br::JAL, 0, rd(raw));
    } else if (name == "jalr") {
        out = expected(imm_i(raw), Alu::ANONE, Mem::MNONE, Wb::PC4, Br::JALR, 0, rd(raw), rs1(raw));
    } else if (name == "lui") {
        out = expected(imm_u(raw), Alu::PASS, Mem::MNONE, Wb::ALU, Br::BNONE, 0, rd(raw));
    } else if (name == "auipc") {
        out = expected(imm_u(raw), Alu::ADD, Mem::MNONE, Wb::ALU, Br::BNONE, 0, rd(raw));
    } else if (name == "c.addi4spn") {
        out = expected(c_imm_addi4spn(raw), Alu::ADD, Mem::MNONE, Wb::ALU, Br::BNONE,
                       0b010, crd_p(raw), 2);
    } else if (name == "c.lw") {
        out = expected(c_imm_lw_sw(raw), Alu::ADD, Mem::LOAD, Wb::MEM, Br::BNONE,
                       0b010, crd_p(raw), crs1_p(raw));
    } else if (name == "c.sw") {
        out = expected(c_imm_lw_sw(raw), Alu::ADD, Mem::STORE, Wb::WNONE, Br::BNONE,
                       0b010, 0, crs1_p(raw), crs2_p(raw));
    } else if (name == "c.addi") {
        out = expected(c_imm_i(raw), Alu::ADD, Mem::MNONE, Wb::ALU, Br::BNONE,
                       0b010, crd_rs1(raw), crd_rs1(raw));
    } else if (name == "c.jal") {
        out = expected(c_imm_j(raw), Alu::ANONE, Mem::MNONE, Wb::PC2, Br::JAL,
                       0b010, 1);
    } else if (name == "c.li") {
        out = expected(c_imm_i(raw), Alu::PASS, Mem::MNONE, Wb::ALU, Br::BNONE,
                       0b010, crd_rs1(raw));
    } else if (name == "c.addi16sp") {
        out = expected(c_imm_addi16sp(raw), Alu::ADD, Mem::MNONE, Wb::ALU, Br::BNONE,
                       0b010, 2, 2);
    } else if (name == "c.srli" || name == "c.srai" || name == "c.andi") {
        Alu op = name == "c.srli" ? Alu::SRL : (name == "c.srai" ? Alu::SRA : Alu::AND);
        uint32_t imm = name == "c.andi" ? c_imm_i(raw) : bits(raw, 6, 2);
        out = expected(imm, op, Mem::MNONE, Wb::ALU, Br::BNONE,
                       0b010, crs1_p(raw), crs1_p(raw));
    } else if (name == "c.sub" || name == "c.xor" || name == "c.or" || name == "c.and") {
        Alu op = Alu::SUB;
        if (name == "c.xor") op = Alu::XOR;
        if (name == "c.or") op = Alu::OR;
        if (name == "c.and") op = Alu::AND;
        out = expected(0, op, Mem::MNONE, Wb::ALU, Br::BNONE,
                       0b010, crs1_p(raw), crs1_p(raw), crs2_p(raw));
    } else if (name == "c.j") {
        out = expected(c_imm_j(raw), Alu::ANONE, Mem::MNONE, Wb::WNONE, Br::JAL,
                       0b010, 0);
    } else if (name == "c.beqz" || name == "c.bnez") {
        out = expected(c_imm_branch(raw), Alu::SLTU, Mem::MNONE, Wb::WNONE,
                       name == "c.beqz" ? Br::BEQZ : Br::BNEZ, 0b010, 0, crs1_p(raw));
    } else if (name == "c.slli") {
        out = expected((bit(raw, 12) << 5) | bits(raw, 6, 2), Alu::SLL, Mem::MNONE, Wb::ALU,
                       Br::BNONE, 0b010, crd_rs1(raw), crd_rs1(raw));
    } else if (name == "c.lwsp") {
        out = expected(c_imm_lwsp(raw), Alu::ADD, Mem::LOAD, Wb::MEM, Br::BNONE,
                       0b010, crd_rs1(raw), 2);
    } else if (name == "c.mv" || name == "c.add") {
        out = expected(0, name == "c.mv" ? Alu::PASS : Alu::ADD, Mem::MNONE, Wb::ALU,
                       Br::BNONE, 0b010, crd_rs1(raw), name == "c.mv" ? 0 : crd_rs1(raw), crs2(raw));
    } else if (name == "c.jr") {
        out = expected(0, Alu::ANONE, Mem::MNONE, Wb::PC2, Br::JR, 0b010, 0, crd_rs1(raw));
    } else if (name == "c.jalr") {
        out = expected(0, Alu::ANONE, Mem::MNONE, Wb::PC2, Br::JALR, 0b010, 1, crd_rs1(raw));
    } else if (name == "c.swsp") {
        out = expected(c_imm_swsp(raw), Alu::ADD, Mem::STORE, Wb::WNONE, Br::BNONE,
                       0b010, 0, 2, crs2(raw));
    } else {
        return false;
    }

    return true;
}

uint32_t legal_random_raw(const RiscvOpcodeCase& opcode, std::mt19937& rng)
{
    uint32_t raw = opcode.match | (rng() & ~opcode.mask);
    return raw;
}

uint32_t legalize_raw(const RiscvOpcodeCase& opcode, uint32_t raw)
{
    raw = (raw & ~opcode.mask) | opcode.match;
    if (opcode.name == std::string_view("c.mv") || opcode.name == std::string_view("c.add") ||
        opcode.name == std::string_view("c.swsp")) {
        raw |= 1u << 2; // rs2 must not be x0 for these compressed forms.
    }
    if (opcode.name == std::string_view("c.jr") || opcode.name == std::string_view("c.jalr") ||
        opcode.name == std::string_view("c.lwsp")) {
        raw |= 1u << 7; // rd/rs1 must not be x0 for these compressed forms.
    }
    if (opcode.width == 16) {
        raw &= 0xffffu;
    }

    return raw;
}

constexpr uint32_t field_mask(unsigned hi, unsigned lo)
{
    return ((1u << (hi - lo + 1)) - 1u) << lo;
}

bool field_is_free(const RiscvOpcodeCase& opcode, unsigned hi, unsigned lo)
{
    return (opcode.mask & field_mask(hi, lo)) == 0;
}

uint32_t set_field(uint32_t raw, unsigned hi, unsigned lo, uint32_t value)
{
    uint32_t mask = field_mask(hi, lo);
    return (raw & ~mask) | ((value << lo) & mask);
}

bool check_field(const char* test, uint32_t raw, const char* field, uint32_t actual, uint32_t expected_value)
{
    if (actual == expected_value) {
        return true;
    }
    std::cerr << test << " raw=0x" << std::hex << raw << " " << field
              << " expected 0x" << expected_value << " got 0x" << actual
              << std::dec << "\n";
    return false;
}

bool check_state(const RiscvOpcodeCase& opcode, uint32_t raw)
{
    State expected_state{};
    if (!state_for(opcode.name, raw, expected_state)) {
        std::cerr << "missing State rule for opcode case: " << opcode.name << "\n";
        return false;
    }

    Rv32im instr = {{{raw}}};
    State actual{};
    instr.decode(actual);

    bool ok = true;
    ok &= check_field(opcode.name, raw, "pc", actual.pc, expected_state.pc);
    ok &= check_field(opcode.name, raw, "rs1_val", actual.rs1_val, expected_state.rs1_val);
    ok &= check_field(opcode.name, raw, "rs2_val", actual.rs2_val, expected_state.rs2_val);
    ok &= check_field(opcode.name, raw, "imm", actual.imm, expected_state.imm);
    ok &= check_field(opcode.name, raw, "valid", actual.valid, expected_state.valid);
    ok &= check_field(opcode.name, raw, "alu_op", actual.alu_op, expected_state.alu_op);
    ok &= check_field(opcode.name, raw, "mem_op", actual.mem_op, expected_state.mem_op);
    ok &= check_field(opcode.name, raw, "wb_op", actual.wb_op, expected_state.wb_op);
    ok &= check_field(opcode.name, raw, "br_op", actual.br_op, expected_state.br_op);
    ok &= check_field(opcode.name, raw, "funct3", actual.funct3, expected_state.funct3);
    ok &= check_field(opcode.name, raw, "rd", actual.rd, expected_state.rd);
    ok &= check_field(opcode.name, raw, "rs1", actual.rs1, expected_state.rs1);
    ok &= check_field(opcode.name, raw, "rs2", actual.rs2, expected_state.rs2);
    return ok;
}

struct SpecRunResult {
    bool ok = true;
    uint64_t encodings = 0;
};

template <size_t ActualCount, size_t ExpectedCount>
bool check_spec_manifest(const char* spec_name,
                         const std::array<RiscvOpcodeCase, ActualCount>& opcodes,
                         const std::array<std::string_view, ExpectedCount>& expected_names)
{
    bool ok = true;
    std::set<std::string> actual;

    for (const RiscvOpcodeCase& opcode : opcodes) {
        if (opcode.spec != std::string_view(spec_name)) {
            std::cerr << spec_name << ": opcode " << opcode.name
                      << " has generated spec tag " << opcode.spec << "\n";
            ok = false;
        }
        if (!actual.insert(opcode.name).second) {
            std::cerr << spec_name << ": duplicate opcode case name: " << opcode.name << "\n";
            ok = false;
        }
    }

    for (std::string_view expected_name : expected_names) {
        if (!actual.contains(std::string(expected_name))) {
            std::cerr << spec_name << ": missing opcode case: " << expected_name << "\n";
            ok = false;
        }
    }

    for (const std::string& name : actual) {
        bool found = false;
        for (std::string_view expected_name : expected_names) {
            found = found || name == expected_name;
        }
        if (!found) {
            std::cerr << spec_name << ": unexpected opcode case: " << name << "\n";
            ok = false;
        }
    }

    return ok;
}

template <size_t Count>
SpecRunResult run_spec_section(const char* spec_name,
                               const std::array<RiscvOpcodeCase, Count>& opcodes,
                               std::mt19937& rng,
                               unsigned random_cases_per_instruction,
                               const std::array<uint32_t, 16>& free_bit_patterns)
{
    SpecRunResult result;

    std::cout << "[ " << spec_name << " ] " << opcodes.size() << " opcode entries\n";
    for (const RiscvOpcodeCase& opcode : opcodes) {
        result.ok &= check_state(opcode, legalize_raw(opcode, opcode.match));
        ++result.encodings;

        for (uint32_t pattern : free_bit_patterns) {
            result.ok &= check_state(opcode, legalize_raw(opcode, opcode.match | (pattern & ~opcode.mask)));
            ++result.encodings;
        }

        unsigned width = opcode.width == 16 ? 16 : 32;
        for (unsigned i = 0; i < width; ++i) {
            if (((opcode.mask >> i) & 1u) == 0) {
                result.ok &= check_state(opcode, legalize_raw(opcode, opcode.match | (1u << i)));
                ++result.encodings;
            }
        }

        if (opcode.width == 32) {
            for (uint32_t value = 0; value < 32; ++value) {
                if (field_is_free(opcode, 11, 7)) {
                    result.ok &= check_state(opcode, legalize_raw(opcode, set_field(opcode.match, 11, 7, value)));
                    ++result.encodings;
                }
                if (field_is_free(opcode, 19, 15)) {
                    result.ok &= check_state(opcode, legalize_raw(opcode, set_field(opcode.match, 19, 15, value)));
                    ++result.encodings;
                }
                if (field_is_free(opcode, 24, 20)) {
                    result.ok &= check_state(opcode, legalize_raw(opcode, set_field(opcode.match, 24, 20, value)));
                    ++result.encodings;
                }
            }
        } else {
            for (uint32_t value = 0; value < 32; ++value) {
                if (field_is_free(opcode, 11, 7)) {
                    result.ok &= check_state(opcode, legalize_raw(opcode, set_field(opcode.match, 11, 7, value)));
                    ++result.encodings;
                }
                if (field_is_free(opcode, 6, 2)) {
                    result.ok &= check_state(opcode, legalize_raw(opcode, set_field(opcode.match, 6, 2, value)));
                    ++result.encodings;
                }
            }
            for (uint32_t value = 0; value < 8; ++value) {
                if (field_is_free(opcode, 4, 2)) {
                    result.ok &= check_state(opcode, legalize_raw(opcode, set_field(opcode.match, 4, 2, value)));
                    ++result.encodings;
                }
                if (field_is_free(opcode, 9, 7)) {
                    result.ok &= check_state(opcode, legalize_raw(opcode, set_field(opcode.match, 9, 7, value)));
                    ++result.encodings;
                }
            }
        }

        for (unsigned i = 0; i < random_cases_per_instruction; ++i) {
            result.ok &= check_state(opcode, legalize_raw(opcode, legal_random_raw(opcode, rng)));
            ++result.encodings;
        }
    }

    std::cout << "[ " << spec_name << " ] checked " << result.encodings << " legal decode encodings\n";
    return result;
}

} // namespace

int main()
{
    constexpr unsigned random_cases_per_instruction = 4096;
    constexpr std::array<uint32_t, 16> free_bit_patterns = {{
        0x00000000u, 0xffffffffu, 0xaaaaaaaau, 0x55555555u,
        0x80000000u, 0x7fffffffu, 0x00000fffu, 0xfffff000u,
        0x0000001fu, 0x000f8000u, 0x01f00000u, 0x00000f80u,
        0x0000f000u, 0x0f0f0f0fu, 0xf0f0f0f0u, 0x13579bdfu,
    }};

    constexpr std::array<std::string_view, 37> rv32i_expected = {{
        "lb", "lh", "lw", "lbu", "lhu",
        "sb", "sh", "sw",
        "addi", "slti", "sltiu", "xori", "ori", "andi", "slli", "srli", "srai",
        "add", "sub", "sll", "slt", "sltu", "xor", "srl", "sra", "or", "and",
        "beq", "bne", "blt", "bge", "bltu", "bgeu",
        "jal", "jalr", "lui", "auipc",
    }};
    constexpr std::array<std::string_view, 8> rv32m_expected = {{
        "mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem", "remu",
    }};
    constexpr std::array<std::string_view, 24> rv32c_expected = {{
        "c.addi4spn", "c.lw", "c.sw", "c.addi", "c.jal", "c.li", "c.addi16sp",
        "c.srli", "c.srai", "c.andi", "c.sub", "c.xor", "c.or", "c.and",
        "c.j", "c.beqz", "c.bnez", "c.slli", "c.lwsp", "c.mv", "c.add",
        "c.jr", "c.jalr", "c.swsp",
    }};

    bool ok = true;
    std::mt19937 rng(0x54524942u);

    ok &= check_spec_manifest("RV32I", kRv32iOpcodeCases, rv32i_expected);
    ok &= check_spec_manifest("RV32M", kRv32mOpcodeCases, rv32m_expected);
    ok &= check_spec_manifest("RV32C", kRv32cOpcodeCases, rv32c_expected);

    SpecRunResult rv32i = run_spec_section("RV32I", kRv32iOpcodeCases, rng, random_cases_per_instruction, free_bit_patterns);
    SpecRunResult rv32m = run_spec_section("RV32M", kRv32mOpcodeCases, rng, random_cases_per_instruction, free_bit_patterns);
    SpecRunResult rv32c = run_spec_section("RV32C", kRv32cOpcodeCases, rng, random_cases_per_instruction, free_bit_patterns);

    ok &= rv32i.ok && rv32m.ok && rv32c.ok;

    if (!ok) {
        return EXIT_FAILURE;
    }

    uint64_t checked_encodings = rv32i.encodings + rv32m.encodings + rv32c.encodings;
    size_t checked_opcodes = kRv32iOpcodeCases.size() + kRv32mOpcodeCases.size() + kRv32cOpcodeCases.size();

    std::cout << "checked " << checked_opcodes << " riscv-opcodes instruction entries"
              << " across " << checked_encodings << " legal decode encodings"
              << " (" << random_cases_per_instruction << " randomized per instruction plus deterministic sweeps)\n";
    return EXIT_SUCCESS;
}

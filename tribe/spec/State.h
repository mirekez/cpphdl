#pragma once

#include <stdint.h>

constexpr const char* AOPS[] =
  {"ANONE", "ADD", "SUB", "AND", "OR", "XOR", "SLL", "SRL", "SRA", "SLT", "SLTU", "PASS", "MUL", "MULH", "MULHSU", "MULHU", "DIV", "DIVU", "REM", "REMU"};
enum Alu
{
    ANONE,   ADD,   SUB,   AND,   OR,   XOR,   SLL,   SRL,   SRA,   SLT,   SLTU,   PASS,   MUL,   MULH,   MULHSU,   MULHU,   DIV,   DIVU,   REM,   REMU
};

constexpr const char* MOPS[] =
  {"MNONE", "LOAD", "STORE"};
enum Mem
{
    MNONE,   LOAD,   STORE
};

constexpr const char* AMOOPS[] =
  {"AMONONE", "LR_W", "SC_W", "AMOSWAP_W", "AMOADD_W", "AMOXOR_W", "AMOAND_W", "AMOOR_W", "AMOMIN_W", "AMOMAX_W", "AMOMINU_W", "AMOMAXU_W"};
enum Amo
{
    AMONONE,   LR_W,   SC_W,   AMOSWAP_W,   AMOADD_W,   AMOXOR_W,   AMOAND_W,   AMOOR_W,   AMOMIN_W,   AMOMAX_W,   AMOMINU_W,   AMOMAXU_W
};

constexpr const char* WOPS[] =
  {"WNONE", "ALU", "MEM", "PC2", "PC4"};
enum Wb
{
    WNONE,   ALU,   MEM,   PC2,   PC4
};

constexpr const char* COPS[] =
  {"CNONE", "CSRRW", "CSRRS", "CSRRC", "CSRRWI", "CSRRSI", "CSRRCI"};
enum Csr
{
    CNONE,   CSRRW,   CSRRS,   CSRRC,   CSRRWI,   CSRRSI,   CSRRCI
};

constexpr const char* SOPS[] =
  {"SNONE", "ECALL", "EBREAK", "MRET", "SRET", "WFI", "FENCEI", "TRAP"};
enum Sys
{
    SNONE,   ECALL,   EBREAK,   MRET,   SRET,   WFI,   FENCEI,   TRAP
};

constexpr const char* TOPS[] =
  {"TNONE", "INST_MISALIGNED", "ILLEGAL_INST", "BREAKPOINT", "LOAD_MISALIGNED", "STORE_MISALIGNED", "ECALL_U", "ECALL_S", "ECALL_M"};
enum Trap
{
    TNONE,   INST_MISALIGNED,   ILLEGAL_INST,   BREAKPOINT,   LOAD_MISALIGNED,   STORE_MISALIGNED,   ECALL_U,   ECALL_S,   ECALL_M
};

constexpr const char* BOPS[] =
  {"BNONE", "BEQ", "BNE", "BLT", "BGE", "BLTU", "BGEU", "JAL", "JALR", "JR", "BEQZ", "BNEZ"};
enum Br
{
    BNONE,   BEQ,   BNE,   BLT,   BGE,   BLTU,   BGEU,   JAL,   JALR,   JR,   BEQZ,   BNEZ
};

struct State
{
    uint32_t pc;
    uint32_t rs1_val;
    uint32_t rs2_val;

    uint32_t imm;

    uint8_t valid:1;
    uint8_t alu_op:5;
    uint8_t mem_op:2;
    uint8_t wb_op:3;
    uint8_t br_op:4;
    uint8_t funct3:3;
    uint8_t rd:5;
    uint8_t rs1:5;
    uint8_t rs2:5;

    uint8_t amo_op:4;

    uint16_t csr_addr:12;
    uint8_t csr_op:3;
    uint8_t csr_imm:5;
    uint8_t sys_op:3;
    uint8_t trap_op:4;
};//__PACKED;

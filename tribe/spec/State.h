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

struct State
{
    uint32_t pc;
    uint32_t rs1_val;
    uint32_t rs2_val;

    uint32_t imm;

    uint8_t valid:1;
    uint8_t alu_op:4;
    uint8_t mem_op:2;
    uint8_t wb_op:3;
    uint8_t br_op:4;
    uint8_t funct3:3;
    uint8_t rd:5;
    uint8_t rs1:5;
    uint8_t rs2:5;
};//__PACKED;

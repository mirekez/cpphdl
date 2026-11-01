#pragma once

#include <cstddef>
#include <cstdint>

struct CpphdlCommit {
    bool valid = false;
    uint64_t order = 0;
    uint32_t pc = 0;
    uint32_t nextPc = 0;
    uint32_t insn = 0;
    uint32_t cause = 0;
    uint32_t data = 0;
    uint8_t trap = 0;
    uint8_t rd = 0;
};

void cpphdl_model_create();
void cpphdl_model_assign();
void cpphdl_model_set_reset(bool value);
void cpphdl_model_set_rtc(bool value);
void cpphdl_model_strobe();
void cpphdl_model_work(bool reset);
void cpphdl_model_write_byte(uint64_t offset, uint8_t value);
void cpphdl_model_apply_memory();
uint64_t cpphdl_model_read_word(uint64_t offset);
void cpphdl_model_configure(uint64_t maxCycles, uint32_t tohost);
std::size_t cpphdl_model_commit_lanes();
CpphdlCommit cpphdl_model_commit(std::size_t lane);
uint32_t cpphdl_model_exit();
void cpphdl_model_trace_hartinfo();
void cpphdl_model_trace_rvfi_rd(uint64_t cycle);

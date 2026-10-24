#include "model.h"
#include "native_memory.h"
#include "run_cpphdl_testharness_model.h"
#include <memory>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>

namespace {
std::unique_ptr<cpphdl_native::Model> model;
bool resetInput = false, rtcInput = false, preparedReset = false, preparedRtc = false, preparedWorkReset = true;
uint64_t edges = 0;
}

void cpphdl_model_create() { model = std::make_unique<cpphdl_native::Model>(); }
void cpphdl_model_assign() {}
void cpphdl_model_set_reset(bool value) { resetInput = value; }
void cpphdl_model_set_rtc(bool value) { rtcInput = value; }
void cpphdl_model_strobe() {
    model->rst_ni[0] = preparedReset;
    model->rtc[0] = preparedRtc;
    model->work_reset[0] = preparedWorkReset;
    model->step();
    ++edges;
}
void cpphdl_model_work(bool reset) {
    preparedReset = resetInput;
    preparedRtc = rtcInput;
    preparedWorkReset = reset;
    model->rst_ni[0] = resetInput;
    model->rtc[0] = rtcInput;
    model->work_reset[0] = reset;
    model->eval(false);
}
void cpphdl_model_write_byte(uint64_t offset, uint8_t value) {
    auto& word = model->CVA6_GRAPH_SRAM.at(offset / 8)[0];
    const unsigned shift = (offset % 8) * 8;
    word = (word & ~(0xffull << shift)) | (uint64_t(value) << shift);
}
void cpphdl_model_apply_memory() {}
uint64_t cpphdl_model_read_word(uint64_t offset) { return model->CVA6_GRAPH_SRAM.at(offset / 8)[0]; }
void cpphdl_model_configure(uint64_t maxCycles, uint32_t tohost) {
    model->cycle_limit[0] = maxCycles;
    model->tohost[0] = tohost;
}
std::size_t cpphdl_model_commit_lanes() { return 2; }
CpphdlCommit cpphdl_model_commit(std::size_t lane) {
    const auto& port = lane == 0 ? model->commit0 : model->commit1;
    return {.valid = bool(port[7] >> 24), .order = port[0] | (uint64_t(port[1]) << 32),
            .pc = port[2], .nextPc = port[3], .insn = port[4], .cause = port[6], .data = port[5],
            .trap = uint8_t(port[7] >> 8), .rd = uint8_t(port[7] >> 16)};
}
uint32_t cpphdl_model_exit() {
    if (model->exit[0] && std::getenv("CVA6_BENCH_STATS"))
        std::fprintf(stderr, "CVA6_GRAPH_EDGES total_cycles=%llu\n", static_cast<unsigned long long>(edges));
    return model->exit[0];
}
void cpphdl_model_trace_hartinfo() { throw std::runtime_error("native graph does not export this diagnostic probe"); }
void cpphdl_model_trace_rvfi_rd(uint64_t) { throw std::runtime_error("native graph does not export this diagnostic probe"); }
void cpphdl_model_trace_irq(uint64_t) { throw std::runtime_error("native graph does not export this diagnostic probe"); }

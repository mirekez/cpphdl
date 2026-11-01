#include "run_cpphdl_testharness_model.h"
#include "run_cpphdl_testharness_model_internal.h"

namespace {

auto& modelMemory()
{
    return cpphdlModel->dut->i_sram.i_tc_sram_wrapper[0].i_tc_sram.sram;
}

} // namespace

void cpphdl_model_write_byte(uint64_t offset, uint8_t value)
{
    auto& target = modelMemory();
    const auto word = static_cast<std::size_t>(offset / 8);
    const auto shift = static_cast<unsigned>((offset % 8) * 8);
    uint64_t data = static_cast<uint64_t>(target.data[word][0]);
    data = (data & ~(0xffull << shift)) | (uint64_t(value) << shift);
    target.data[word][0] = logic<64>(data);
}

void cpphdl_model_apply_memory()
{
    modelMemory().apply();
}

uint64_t cpphdl_model_read_word(uint64_t offset)
{
    return static_cast<uint64_t>(modelMemory().data[offset / 8][0]);
}

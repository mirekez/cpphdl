#include "model_api.h"
#include "model_top.h"
#ifdef OPENC910_OPTIMIZE_COMBS
#include "OpenC910Model_optimized_combs.h"
#endif
#include <memory>

long _system_clock = 0;
namespace {
std::unique_ptr<OpenC910Model> model;
cpphdl::logic<1> reset_n = 1, jreset_n = 1, jclk = 0;
}
void model_create() {
    model = std::make_unique<OpenC910Model>();
    model->rst_n_in = _ASSIGN_REG(reset_n);
    model->jrst_n_in = _ASSIGN_REG(jreset_n);
    model->jclk_in = _ASSIGN_REG(jclk);
    model->_assign();
#ifdef OPENC910_OPTIMIZE_COMBS
    cpphdl_optimized_bind_ports_abi(model.get());
#endif
}
void model_inputs(bool reset, bool jreset, bool clock) {
    reset_n = reset; jreset_n = jreset; jclk = clock;
    ++_system_clock;
}
void model_write_byte(uint64_t offset, uint8_t value) {
    auto& bank = model->dut.x_axi_slave128.x_f_spsram_large;
    switch (offset % 16) {
#define BANK(number) case number: bank.ram##number.mem[offset / 16] = cpphdl::logic<8>(value); bank.ram##number.mem.apply(); break;
        BANK(0) BANK(1) BANK(2) BANK(3) BANK(4) BANK(5) BANK(6) BANK(7)
        BANK(8) BANK(9) BANK(10) BANK(11) BANK(12) BANK(13) BANK(14) BANK(15)
#undef BANK
    }
}
Observation model_observe() {
    Observation out{};
#define WIDE(name) uint64_t(model->name##_out())
    out.pc[0] = WIDE(pc0); out.pc[1] = WIDE(pc1); out.pc[2] = WIDE(pc2);
    out.wb[0] = WIDE(wb0); out.wb[1] = WIDE(wb1); out.wb[2] = WIDE(wb2);
    out.address = WIDE(address); out.retire = WIDE(retire);
    out.awlen = WIDE(awlen); out.wstrb = WIDE(wstrb);
    out.wvalid = WIDE(wvalid); out.clock_enable = WIDE(clock_enable);
#undef WIDE
    const auto data = model->wdata_out();
    for (unsigned i = 0; i < 4; ++i) out.wdata[i] = uint64_t(data.bits(i * 32 + 31, i * 32));
    return out;
}
void model_step() {
#ifdef OPENC910_OPTIMIZE_COMBS
    calc_all(*model, false);
    commit_optimized_regs(*model);
#else
    model->_work(false);
    model->_strobe();
#endif
    ++_system_clock;
}

#include "model_api.h"
#include "model.h"
#include "native_memory.h"
#include <memory>
#include <stdexcept>

namespace {
std::unique_ptr<cpphdl_native::Model> model;
bool outputs_ready = false;
}
void model_create() {
    model = std::make_unique<cpphdl_native::Model>();
    outputs_ready = false;
}
void model_inputs(bool reset_n, bool jreset_n, bool jclk) {
    model->rst_n[0] = reset_n;
    model->jrst_n[0] = jreset_n;
    model->jclk[0] = jclk;
    model->work_reset[0] = 0;
    outputs_ready = false;
}
void model_write_byte(uint64_t offset, uint8_t value) {
    outputs_ready = false;
    switch (offset % 16) {
#define BANK(number) case number: model->OPENC910_MEMORY_##number.at(offset / 16)[0] = value; break;
        BANK(0) BANK(1) BANK(2) BANK(3) BANK(4) BANK(5) BANK(6) BANK(7)
        BANK(8) BANK(9) BANK(10) BANK(11) BANK(12) BANK(13) BANK(14) BANK(15)
#undef BANK
    }
}
Observation model_observe() {
    // step() already settles outputs after committing registers. Only initial
    // stimulus or an external write requires another combinational evaluation.
    if (!outputs_ready) { model->eval(false); outputs_ready = true; }
    Observation out{};
#define WIDE(name) (uint64_t(model->name[0]) | (uint64_t(model->name[1]) << 32))
    out.pc[0] = WIDE(pc0); out.pc[1] = WIDE(pc1); out.pc[2] = WIDE(pc2);
    out.wb[0] = WIDE(wb0); out.wb[1] = WIDE(wb1); out.wb[2] = WIDE(wb2);
#undef WIDE
    for (unsigned i = 0; i < 4; ++i) out.wdata[i] = model->wdata[i];
    out.address = model->address[0]; out.retire = model->retire[0];
    out.awlen = model->awlen[0]; out.wstrb = model->wstrb[0];
    out.wvalid = model->wvalid[0]; out.clock_enable = model->clock_enable[0];
    return out;
}
void model_step() { model->step(); outputs_ready = true; }

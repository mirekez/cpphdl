#include "Variane_testharness.h"
#include "Variane_testharness___024root.h"
#include "BusTrace.h"
#include <cstdlib>
#include <cstring>

static std::array<uint32_t, 24> latest_requests{};
extern "C" void cpphdl_observe_bus_requests(const uint32_t* words) {
    std::memcpy(latest_requests.data(), words, sizeof(latest_requests));
    latest_requests.back() &= 0xfff;
}

extern "C" void __real__ZN19Variane_testharness9eval_stepEv(Variane_testharness*);
extern "C" void __wrap__ZN19Variane_testharness9eval_stepEv(Variane_testharness* model) {
    static FILE* file = [] {
        const char* path = std::getenv("CPPHDL_BUS_TRACE");
        auto* output = path ? std::fopen(path, "wb") : nullptr;
        if (!output) std::abort();
        return output;
    }();
    if (model->clk_i) {
        const auto& root = *model->rootp;
        BusTrace record{};
        record.reset_n = (root.ariane_testharness__DOT__i_rstgen_main__DOT__i_rstgen_bypass__DOT__synch_regs_q >> 3) & 1;
        record.requests = latest_requests;
        std::memcpy(record.responses.data(), root.ariane_testharness__DOT__i_axi_xbar__DOT____Vcellinp__i_xbar__mst_ports_resp_i.data(), sizeof(record.responses));
        std::memcpy(record.slave_outputs.data(), root.__VdfgRegularize_h6e95ff9d_0_19.data(), sizeof(record.slave_outputs));
        std::memcpy(record.master_outputs.data(), root.ariane_testharness__DOT__i_axi_xbar__DOT__mst_reqs.data(), sizeof(record.master_outputs));
        record.responses.back() &= 0xff;
        record.slave_outputs.back() &= 0xf;
        record.master_outputs.back() &= 0xffff;
        if (std::fwrite(&record, sizeof(record), 1, file) != 1) std::abort();
    }
    __real__ZN19Variane_testharness9eval_stepEv(model);
}

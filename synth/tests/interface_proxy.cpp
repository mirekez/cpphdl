#include "../../tests/interface/GraphInterfaceProxy.h"
GraphInterfaceProxy cpphdl_top;
#ifdef SYNTH_INTERFACE_PROXY_RUN
#include <cstdio>
#ifdef SYNTH_INTERFACE_PROXY_GRAPH
#include "model.h"
#else
#include "VSynthInterface_Proxy.h"
#endif
long _system_clock = 0;
int main() {
    uint8_t value = 0;
    cpphdl_top.bus_in.data_in = _ASSIGN(value);
    cpphdl_top._assign();
#ifdef SYNTH_INTERFACE_PROXY_GRAPH
    cpphdl_native::Model dut;
#else
    VSynthInterface_Proxy dut;
#endif
    for (unsigned i = 0; i < 256; ++i) {
        value = i;
#ifdef SYNTH_INTERFACE_PROXY_GRAPH
        dut.bus_in__data[0] = i; dut.work_reset[0] = 0; dut.step();
        bool correct = dut.result[0] == uint8_t(i * 3) && dut.bus_in__ready[0] == (i & 1);
#else
        dut.bus_in___05Fdata = i; dut.work_reset = 0; dut.clk = 0; dut.eval();
        bool correct = dut.result == uint8_t(i * 3) && dut.bus_in___05Fready == (i & 1);
#endif
        if (!correct || cpphdl_top.result_out() != uint8_t(i * 3) || cpphdl_top.bus_in.ready_out() != bool(i & 1)) return 1;
        ++_system_clock;
    }
    std::puts("PASS: graph interface proxies connect both directions through two hierarchy levels");
}
#endif

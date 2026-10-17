#include "PortAliases.h"
#include <cstdio>
#ifdef PORT_ALIAS_RTL
#include "VPortAliases.h"
#endif

long _system_clock = 0;

class PortAliasSource : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<PORT_ALIAS_WIDTH>) value_in;
    PortAliasInterface<PORT_ALIAS_WIDTH> source_out;

    void _assign() {
        source_out.data_in = _ASSIGN(value_in());
    }
};

class PortAliasHarness : public cpphdl::Module {
public:
    PortAliases<PORT_ALIAS_WIDTH> native;
    PortAliasSource source;
    cpphdl::logic<PORT_ALIAS_WIDTH> data = 0, interface_data = 0;

    void _assign() {
        native.data_in = _ASSIGN_REG(data);
        source.value_in = _ASSIGN_REG(interface_data);
        assignIf(source, native, source.source_out, native.sink_in);
    }
};

int main() {
    PortAliasHarness harness;
    auto& native = harness.native;
    auto& data = harness.data;
    auto& interface_data = harness.interface_data;
    harness._assign();
#ifdef PORT_ALIAS_RTL
    VPortAliases rtl;
#endif
    const uint64_t mask = (uint64_t(1) << PORT_ALIAS_WIDTH) - 1;
    for (unsigned i = 0; i != 2048; ++i) {
        ++_system_clock;
        data = (uint64_t(i) * 0x51a273ULL) & mask;
        interface_data = ~uint64_t(data) & mask;
        if (native.data_out() != data || native.interface_out() != interface_data) return 1;
#ifdef PORT_ALIAS_RTL
        rtl.data_in = uint64_t(data);
        rtl.sink_in___05Fdata_in = uint64_t(interface_data);
        rtl.eval();
        if (rtl.data_out != uint64_t(data) || rtl.interface_out != uint64_t(interface_data)) return 2;
#endif
    }
    std::puts("Port alias bindings and parameterized widths passed");
}

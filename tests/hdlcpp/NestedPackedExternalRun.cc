#include "generated/NestedPackedExternalDeclaration.h"
#include "generated/NestedPackedExternal.h"
#include <cstdio>
long _system_clock = 0;
int main() {
    NestedPackedExternal dut;
    NestedPackedExternalTypes::packet_t packet;
    cpphdl::logic<3> index;
    dut.packet_i_in = _ASSIGN(packet);
    dut.packet_i_in__field_inner_window = _ASSIGN(packet.inner.window);
    dut.index_i_in = _ASSIGN(index);
    dut._assign();
    for (unsigned value = 0; value < 256; ++value) {
        packet.inner.window = value;
        for (unsigned bit = 0; bit < 8; ++bit) {
            index = bit;
            ++_system_clock;
            if (uint64_t(dut.bit_o_out()) != ((value >> bit) & 1)) return 1;
        }
    }
    std::puts("2048 external nested-field samples pass");
}

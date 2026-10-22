#include <cassert>
#include <cstdint>
#include <cstdio>
#if defined(ROM_GRAPH)
#include "model.h"
#elif defined(ROM_RTL)
#include "VConstPackedRom.h"
#else
#include "generated/ConstPackedRom.h"
long _system_clock = 0;
#endif

int main() {
#if defined(ROM_GRAPH)
    cpphdl_native::Model model;
#elif defined(ROM_RTL)
    VConstPackedRom model;
#else
    ConstPackedRom model;
    cpphdl::logic<2> address;
    model.address_i_in = _ASSIGN(address);
    model._assign();
#endif
    const uint64_t expected[] = {0x1050007303c58593ull, 0x0123456789abcdefull,
                                 0xfedcba9876543210ull, 0};
    for (unsigned sample = 0; sample < 4096; ++sample) {
        const unsigned index = sample & 3;
        uint64_t actual;
#if defined(ROM_GRAPH)
        model.address_i[0] = index;
        model.eval();
        actual = uint64_t(model.data_o[0]) | (uint64_t(model.data_o[1]) << 32);
#elif defined(ROM_RTL)
        model.address_i = index;
        model.eval();
        actual = model.data_o;
#else
        address = index;
        ++_system_clock;
        actual = uint64_t(model.data_o_out());
#endif
        assert(actual == expected[index]);
    }
    std::puts("constant packed ROM: 4096 cases passed");
}

#include <cpphdl.h>

namespace Constants {
constexpr uint64_t MASK = (uint64_t(1) << 48) - 1;
constexpr unsigned COUNT = 3;
constexpr int NEGATIVE = -7;
enum class Mode : uint8_t { Adjust = 5 };
constexpr Mode MODE = Mode::Adjust;
namespace Nested { constexpr uint32_t STRIDE = 11; }
}
struct ConstantPolicy { static constexpr uint32_t SCALE = 17; };

template<unsigned BITS>
class NamespaceConstants : public cpphdl::Module {
public:
    static constexpr unsigned OFFSET = BITS + 3;
    _PORT(cpphdl::logic<48>) data_in;
    _PORT(uint32_t) result_out = _ASSIGN_REG(result);
private:
    cpphdl::reg<cpphdl::u32> result;
public:
    void _work(bool reset) {
        uint32_t value = uint32_t(uint64_t(data_in()) & Constants::MASK);
        for (unsigned i = 0; i < Constants::COUNT; ++i)
            value += Constants::Nested::STRIDE * i;
        value = value * ConstantPolicy::SCALE + OFFSET + Constants::NEGATIVE + uint32_t(Constants::MODE);
        result._next = reset ? 0u : value;
    }
    void _strobe() { result.strobe(); }
};
extern NamespaceConstants<19> cpphdl_top;

#ifndef SYNTHESIS
#include <cstdio>
#ifdef VERILATOR
#include "VNamespaceConstants.h"
#endif
long _system_clock = 0;
int main() {
#ifdef VERILATOR
    VNamespaceConstants dut;
#else
    NamespaceConstants<19> dut;
    uint64_t data = 0;
    dut.data_in = _ASSIGN(data);
#endif
    for (unsigned i = 0; i < 4096; ++i) {
        uint64_t input = (uint64_t(i) << 40) | uint32_t(i * 2654435761u);
        bool reset = i % 31 == 0;
#ifdef VERILATOR
        dut.clk = 0; dut.reset = reset; dut.data_in = input; dut.eval();
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
        uint32_t got = dut.result_out;
#else
        data = input; dut._work(reset); dut._strobe();
        uint32_t got = dut.result_out();
#endif
        ++_system_clock;
        uint32_t expected = reset ? 0u : (uint32_t(input) + 33u) * 17u + 20u;
        if (got != expected) return 1;
    }
    std::puts("Namespace/class constants, signed/enum/48-bit values and module template parameters passed");
}
#endif

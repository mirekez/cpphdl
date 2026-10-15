#include "ResolvedTypeAliases.h"
#include <cstdio>
#include <limits>

long _system_clock = 0;

#ifdef ALIAS_RTL
#include "VResolvedTypeAliases.h"
#else
class AliasHarness : public cpphdl::Module {
public:
    ResolvedTypeAliases<ALIAS_WIDTH> dut;
    uint64_t input = 0;
    void _assign() { dut.data_in = _ASSIGN(input); }
};
#endif

int main() {
    using DUT = ResolvedTypeAliases<ALIAS_WIDTH>;
    using Expected = std::conditional_t<(ALIAS_WIDTH <= 32), uint32_t, uint64_t>;
    static_assert(std::is_same_v<typename DUT::DTYPE, Expected>);
    static_assert(std::is_same_v<typename DUT::chained_t, Expected>);
    static_assert(std::is_same_v<typename DUT::wide_t, uint64_t>);
    static_assert(std::is_same_v<typename DUT::signed_t, int64_t>);
    static_assert(std::is_same_v<typename DUT::vector_t, cpphdl::logic<ALIAS_WIDTH>>);
#ifdef ALIAS_RTL
    VResolvedTypeAliases dut;
    dut.clk = 0;
    dut.reset = 0;
#else
    AliasHarness harness;
    harness._assign();
    auto& dut = harness.dut;
#endif
    uint64_t random = 0x123456789abcdef0ull;
    for (unsigned sample = 0; sample < 1024; ++sample) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        const uint64_t input = sample == 0 ? 0 : sample == 1 ? UINT64_MAX : random;
        const uint64_t mask = std::numeric_limits<Expected>::max();
        const uint64_t selected = input & mask;
#ifdef ALIAS_RTL
        dut.data_in = input;
        dut.eval();
        const uint64_t result = dut.result_out;
        const uint64_t wide = dut.wide_out;
        const int64_t sign = int64_t(dut.signed_out);
#else
        harness.input = input;
        ++_system_clock;
        const uint64_t result = dut.result_out();
        const uint64_t wide = dut.wide_out();
        const int64_t sign = dut.signed_out();
#endif
        if (result != ((selected + 1) & mask) || wide != selected || sign != -7) {
            std::fprintf(stderr, "alias mismatch: width=%u sample=%u\n", ALIAS_WIDTH, sample);
            return 1;
        }
    }
    std::printf("resolved aliases: width=%u, 1024 oracle samples passed\n", ALIAS_WIDTH);
}

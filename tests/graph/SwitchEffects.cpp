#include "cpphdl.h"
#include <cstdlib>

class SwitchEffects : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<4>) selector_in;
    _PORT(cpphdl::logic<3>) flags_in;
    _PORT(cpphdl::logic<64>) result_out = _ASSIGN_REG(state);
    cpphdl::reg<cpphdl::logic<64>> state;
    uint64_t sample(unsigned s, unsigned f) {
        unsigned i;
        uint64_t x = ::random();
        switch (s) {
        case 0:
            (void)::random();
            if (f & 1) return x;
            if (f & 2) break;
            x ^= ::random();
            [[fallthrough]];
        case 1: x ^= ::random(); break;
        case 2:
            for (i = 0; i < 4; ++i) {
                if (i == f) return x;
                (void)::random();
                if (f & 4) break;
            }
            break;
        default:
            if (f & 1) break;
            return ::random();
        }
        (void)::random();
        return x ^ uint64_t(::random());
    }
    void _work(bool reset) {
        uint64_t first, second;
        if (reset) state._next = 0;
        else {
            first = sample(uint64_t(selector_in()), uint64_t(flags_in()));
            second = sample(uint64_t(flags_in()), uint64_t(selector_in()));
            state._next = (first << 32) ^ second;
        }
    }
    void _strobe() { state.strobe(); }
};
SwitchEffects cpphdl_top;

#ifdef SWITCH_EFFECTS_RUN
#include "model.h"
#include <cstdio>
long _system_clock = 0;
static unsigned calls;
extern "C" long __real_random() noexcept;
extern "C" long __wrap_random() noexcept { ++calls; return __real_random(); }
int main() {
    cpphdl_native::Model dut;
    cpphdl::logic<4> selector;
    cpphdl::logic<3> flags;
    cpphdl_top.selector_in = _ASSIGN(selector);
    cpphdl_top.flags_in = _ASSIGN(flags);
    cpphdl_top._assign();
    for (unsigned n = 0; n < 2048; ++n) {
        selector = n & 15; flags = (n >> 4) & 7;
        bool reset = n % 31 == 0;
        ++_system_clock;
        ::srandom(n + 1); calls = 0;
        cpphdl_top._work(reset); cpphdl_top._strobe();
        unsigned expectedCalls = calls;
        uint64_t expected = cpphdl_top.result_out();
        ::srandom(n + 1); calls = 0;
        dut.selector[0] = uint64_t(selector); dut.flags[0] = uint64_t(flags);
        dut.work_reset[0] = reset;
        dut.step();
        dut.eval(false); dut.eval(false);
        uint64_t actual = uint64_t(dut.result[0]) | (uint64_t(dut.result[1]) << 32);
        if (actual != expected || calls != expectedCalls) {
            std::fprintf(stderr, "sample=%u: calls=%u expected=%u value=%llu expected=%llu\n",
                         n, calls, expectedCalls, (unsigned long long)actual,
                         (unsigned long long)expected);
            return 1;
        }
    }
    std::puts("switch control: 2048 ordered host-effect values/counts match C++");
}
#endif

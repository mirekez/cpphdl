#include "cpphdl.h"
#include <cstdlib>

namespace custom {
inline long random() { return 17; }
}

class GraphRandom : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) enable_in;
    _PORT(cpphdl::logic<1>) driven_in;
    _PORT(cpphdl::logic<1>) data_in;
    _PORT(cpphdl::logic<2>) choose_in;
    _PORT(cpphdl::logic<64>) result_out = _ASSIGN_REG(state);
    cpphdl::reg<cpphdl::logic<64>> state;
    static constexpr bool __cpphdl_net_random_bits_comb = true;
    cpphdl::logic<32> random_bits_comb;
    cpphdl::logic<32>& random_bits_comb_func() {
        random_bits_comb = ::random();
        return random_bits_comb;
    }
    static constexpr bool __cpphdl_net_tdo_comb = true;
    cpphdl::logic<1> tdo_comb;
    cpphdl::logic<1>& tdo_comb_func() {
        tdo_comb = driven_in() ? data_in() : cpphdl::logic<1>(uint64_t(random_bits_comb_func()) & 1);
        return tdo_comb;
    }
    void _work(bool reset) {
        state._next = state;
        if (reset) state._next = 0;
        else if (enable_in()) {
            state._next = uint64_t(tdo_comb_func());
            if (choose_in()[0]) (void)::random();
            bool conjunction = driven_in() && (::random() != 0);
            bool disjunction = driven_in() || (::random() != 0);
            uint64_t selected = driven_in() ? ::random() : ::random();
            state._next = uint64_t(state._next) ^ (selected << 1) ^ uint64_t(conjunction) ^ uint64_t(disjunction);
            switch (uint64_t(choose_in())) {
            case 0: { (void)::random(); break; }
            case 1: { (void)::random(); (void)::random(); break; }
            default: { state._next = uint64_t(state._next) ^ uint64_t(::random()); break; }
            }
            uint64_t first = ::random();
            uint64_t second = ::random();
            state._next = uint64_t(state._next) ^ (first << 32) ^ second ^ uint64_t(custom::random());
            (void)::random();
        }
    }
    void _strobe() { state.strobe(); }
};

GraphRandom cpphdl_top;

#ifdef CPP_GRAPH_RANDOM_RUN
#include "model.h"
#include <array>
#include <cstdio>

long _system_clock = 0;
static unsigned calls = 0;
extern "C" long __real_random() noexcept;
extern "C" long __wrap_random() noexcept {
    ++calls;
    return __real_random();
}

int main() {
    constexpr unsigned samples = 2048;
    std::array<uint64_t, samples> expected;
    std::array<unsigned, samples> expectedCalls;
    cpphdl::logic<1> enable, driven, data;
    cpphdl::logic<2> choose;
    cpphdl_top.enable_in = _ASSIGN(enable);
    cpphdl_top.driven_in = _ASSIGN(driven);
    cpphdl_top.data_in = _ASSIGN(data);
    cpphdl_top.choose_in = _ASSIGN(choose);
    for (unsigned seed : {1u, 23u, 1234567u}) {
        cpphdl_top._work(true);
        cpphdl_top._strobe();
        ::srandom(seed);
        calls = 0;
        for (unsigned sample = 0; sample < samples; ++sample) {
            enable = sample % 5 != 0;
            driven = (sample >> 2) & 1;
            data = (sample >> 3) & 1;
            choose = sample & 3;
            ++_system_clock;
            cpphdl_top._work(sample % 19 == 0);
            cpphdl_top._strobe();
            expected[sample] = uint64_t(cpphdl_top.state);
            expectedCalls[sample] = calls;
        }
        cpphdl_native::Model model;
        ::srandom(seed);
        calls = 0;
        for (unsigned sample = 0; sample < samples; ++sample) {
            model.enable[0] = sample % 5 != 0;
            model.driven[0] = (sample >> 2) & 1;
            model.data[0] = (sample >> 3) & 1;
            model.choose[0] = sample & 3;
            model.work_reset[0] = sample % 19 == 0;
            auto before = calls;
            model.eval(false);
            model.eval(false);
            if (calls != before) return 1;
            if (sample & 1) model.step();
            else { model.eval(true); model.eval(false); }
            model.eval(false);
            uint64_t actual = uint64_t(model.result[0]) | (uint64_t(model.result[1]) << 32);
            if (actual != expected[sample] || calls != expectedCalls[sample]) {
                std::fprintf(stderr, "seed=%u sample=%u result=%llu expected=%llu calls=%u expected_calls=%u\n",
                    seed, sample, static_cast<unsigned long long>(actual),
                    static_cast<unsigned long long>(expected[sample]), calls, expectedCalls[sample]);
                return 2;
            }
        }
    }
    std::puts("ordinary C++ graph: 6144 libc random transactions match values and call counts; settling consumes none");
}
#endif

#include "corev_apu/tb/dpi_adapters.h"
#include <cstdlib>

class GraphJtag : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) enable_in;
    _PORT(cpphdl::logic<2>) mode_in;
    _PORT(cpphdl::logic<8>) tdo_in;
    _PORT(cpphdl::logic<64>) pins_out = _ASSIGN(packed());
    _PORT(cpphdl::logic<64>) wrapped_out = _ASSIGN(wrapped());
    _PORT(cpphdl::logic<64>) mirror_out = _ASSIGN_REG(mirror);
    _PORT(cpphdl::logic<64>) sampled_out = _ASSIGN_REG(sampled);
    _PORT(cpphdl::logic<8>) cached_out = _ASSIGN(cached_func());
    unsigned char tck, tms, tdi, trstn;
    int result;
    cpphdl::logic<1> wrappedTck, wrappedTms, wrappedTdi, wrappedTrstn;
    int wrappedResult;
    cpphdl::logic<64> mirror;
    cpphdl::reg<cpphdl::logic<64>> sampled;
    cpphdl::logic<8> cached, nested;
    static constexpr bool __cpphdl_net_cached = true;
    unsigned char& direct() { return tck; }
    cpphdl::logic<8>& nested_func() { nested = direct() ^ 0x5a; return nested; }
    cpphdl::logic<8>& cached_func() { cached = nested_func() ^ 0xa5; return cached; }
    cpphdl::logic<64> packed() {
        return uint64_t(uint32_t(result)) | (uint64_t(tck) << 32) | (uint64_t(tms) << 40) |
            (uint64_t(tdi) << 48) | (uint64_t(trstn) << 56);
    }
    cpphdl::logic<64> wrapped() {
        return uint64_t(uint32_t(wrappedResult)) | (uint64_t(wrappedTck) << 32) |
            (uint64_t(wrappedTms) << 33) | (uint64_t(wrappedTdi) << 34) | (uint64_t(wrappedTrstn) << 35);
    }
    void _work(bool reset) {
        sampled._next = sampled;
        if (reset) {
            tck = 0x81; tms = 0x42; tdi = 0xc3; trstn = 0x24;
            result = -9;
            wrappedTck = 0; wrappedTms = 1; wrappedTdi = 0; wrappedTrstn = 1;
            wrappedResult = -3;
            mirror = 0;
            sampled._next = 0;
        } else if (enable_in()) {
            (void)::random();
            result = ::jtag_tick(&tck, &tms, &tdi, &trstn, static_cast<unsigned char>(uint64_t(tdo_in())));
            auto first = uint64_t(cached_func());
            if (mode_in()[0]) (void)::jtag_tick(&tck, &tms, &tdi, &trstn, static_cast<unsigned char>(uint64_t(tdo_in()) ^ 128));
            auto second = uint64_t(cached_func());
            wrappedResult = jtag_tick(wrappedTck, wrappedTms, wrappedTdi, wrappedTrstn, cpphdl::logic<1>(tdo_in()));
            if (mode_in()[1]) (void)::random();
            mirror = uint64_t(int64_t(result));
            sampled._next = packed() ^ (first << 8) ^ (second << 16);
        }
    }
    void _strobe() { sampled.strobe(); }
};

GraphJtag cpphdl_top;

#ifdef CPP_GRAPH_JTAG_RUN
#include "model.h"
#include <array>
#include <cstdio>

long _system_clock = 0;
static unsigned calls, randomCalls;
static uint64_t history;
extern "C" long __real_random() noexcept;
extern "C" long __wrap_random() noexcept {
    ++randomCalls;
    return __real_random();
}
extern "C" int jtag_tick(unsigned char* tck, unsigned char* tms, unsigned char* tdi,
                         unsigned char* trstn, unsigned char tdo) {
    ++calls;
    uint64_t inputs = uint64_t(*tck) | (uint64_t(*tms) << 8) | (uint64_t(*tdi) << 16) |
        (uint64_t(*trstn) << 24) | (uint64_t(tdo) << 32);
    history = (history ^ inputs ^ calls) * 1099511628211ull;
    uint32_t draw = uint32_t(::random());
    *tck = static_cast<unsigned char>(draw);
    if (calls & 1) *tms ^= *tck;
    *tdi = static_cast<unsigned char>((draw >> 16) + tdo);
    if (calls % 3) *trstn = static_cast<unsigned char>(~*trstn);
    return calls & 1 ? -int(draw) - 1 : int(draw);
}

int main() {
    constexpr unsigned samples = 2048;
    struct Sample { uint64_t pins, wrapped, mirror, sampled, cached, history; unsigned calls, randomCalls; };
    std::array<Sample, samples> expected;
    cpphdl::logic<1> enable;
    cpphdl::logic<2> mode;
    cpphdl::logic<8> tdo;
    cpphdl_top.enable_in = _ASSIGN(enable);
    cpphdl_top.mode_in = _ASSIGN(mode);
    cpphdl_top.tdo_in = _ASSIGN(tdo);
    for (unsigned seed : {1u, 23u, 1234567u}) {
        ::srandom(seed);
        calls = randomCalls = 0; history = 0;
        for (unsigned sample = 0; sample < samples; ++sample) {
            enable = sample % 5 != 0; mode = sample & 3; tdo = sample & 255;
            ++_system_clock;
            cpphdl_top._work(sample % 19 == 0);
            cpphdl_top._strobe();
            ++_system_clock;
            expected[sample] = {uint64_t(cpphdl_top.pins_out()), uint64_t(cpphdl_top.wrapped_out()),
                uint64_t(cpphdl_top.mirror_out()), uint64_t(cpphdl_top.sampled_out()),
                uint64_t(cpphdl_top.cached_out()), history, calls, randomCalls};
        }
        cpphdl_native::Model model;
        ::srandom(seed);
        calls = randomCalls = 0; history = 0;
        for (unsigned sample = 0; sample < samples; ++sample) {
            model.enable[0] = sample % 5 != 0; model.mode[0] = sample & 3; model.tdo[0] = sample & 255;
            model.work_reset[0] = sample % 19 == 0;
            auto before = calls, randomBefore = randomCalls;
            model.eval(false); model.eval(false);
            if (calls != before || randomCalls != randomBefore) return 1;
            if (sample & 1) model.step();
            else { model.eval(true); model.eval(false); }
            model.eval(false);
            const auto& wanted = expected[sample];
            auto word = [](const auto& port) { return uint64_t(port[0]) | (uint64_t(port[1]) << 32); };
            if (word(model.pins) != wanted.pins || word(model.wrapped) != wanted.wrapped ||
                word(model.mirror) != wanted.mirror || word(model.sampled) != wanted.sampled ||
                model.cached[0] != wanted.cached || history != wanted.history ||
                calls != wanted.calls || randomCalls != wanted.randomCalls) {
                std::fprintf(stderr, "seed=%u sample=%u pins=%llx/%llx calls=%u/%u random=%u/%u\n",
                    seed, sample, static_cast<unsigned long long>(word(model.pins)),
                    static_cast<unsigned long long>(wanted.pins), calls, wanted.calls, randomCalls, wanted.randomCalls);
                return 2;
            }
        }
    }
    std::puts("ordinary C++ graph: 6144 JTAG transactions match return values, four outputs, retention, call order and random stream");
}
#endif

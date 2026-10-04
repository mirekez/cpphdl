#include "corev_apu/tb/dpi_adapters.h"
#include <cstdlib>

class GraphDebug : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<2>) mode_in;
    _PORT(cpphdl::logic<32>) input_in;
    _PORT(cpphdl::logic<64>) raw0_out = _ASSIGN(raw0());
    _PORT(cpphdl::logic<64>) raw1_out = _ASSIGN(raw1());
    _PORT(cpphdl::logic<64>) result_out = _ASSIGN(uint64_t(int64_t(result)));
    _PORT(cpphdl::logic<64>) wrapped0_out = _ASSIGN(wrapped0());
    _PORT(cpphdl::logic<64>) wrapped1_out = _ASSIGN(wrapped1());
    _PORT(cpphdl::logic<64>) wrapped_result_out = _ASSIGN(uint64_t(int64_t(wrappedResult)));
    _PORT(cpphdl::logic<32>) cached_out = _ASSIGN(cached_func());
    _PORT(cpphdl::logic<64>) sampled_out = _ASSIGN_REG(sampled);
    unsigned char reqValid, respReady;
    int addr, op, data, result, wrappedResult;
    cpphdl::logic<1> wrappedValid, wrappedReady;
    cpphdl::u32 wrappedAddr, wrappedOp, wrappedData;
    cpphdl::logic<32> cached;
    cpphdl::reg<cpphdl::logic<64>> sampled;
    static constexpr bool __cpphdl_net_cached = true;
    cpphdl::logic<32>& cached_func() { cached = uint32_t(data) ^ 0x12345678u; return cached; }
    cpphdl::logic<64> raw0() { return uint64_t(uint32_t(addr)) | (uint64_t(uint32_t(op)) << 32); }
    cpphdl::logic<64> raw1() { return uint64_t(uint32_t(data)) | (uint64_t(reqValid) << 32) | (uint64_t(respReady) << 40); }
    cpphdl::logic<64> wrapped0() { return uint64_t(wrappedAddr) | (uint64_t(wrappedOp) << 32); }
    cpphdl::logic<64> wrapped1() { return uint64_t(wrappedData) | (uint64_t(wrappedValid) << 32) | (uint64_t(wrappedReady) << 40); }
    void _work(bool reset) {
        sampled._next = sampled;
        if (reset) {
            reqValid = 0x81; respReady = 0xc3;
            addr = -1; op = -2; data = -3; result = -4;
            wrappedValid = 1; wrappedReady = 0;
            wrappedAddr = 0xffffffffu; wrappedOp = 0x80000000u; wrappedData = 0x87654321u;
            wrappedResult = -5;
            sampled._next = 0;
        } else if (mode_in()) {
            (void)::random();
            auto input = uint64_t(input_in());
            auto ready = [input]() { return static_cast<unsigned char>(input); }();
            auto response = [&input]() { input ^= 0x55; return static_cast<int>(input); }();
            result = ::debug_tick(&reqValid, ready,
                &addr, &op, &data, static_cast<unsigned char>(uint64_t(input_in()) >> 8),
                &respReady, static_cast<int>(uint64_t(input_in()) ^ 0x80000000u), response);
            auto first = uint64_t(cached_func());
            if (mode_in()[0])
                (void)::debug_tick(&reqValid, 0xfe, &addr, &op, &data, 0xab, &respReady, -7, -9);
            auto second = uint64_t(cached_func());
            wrappedResult = debug_tick(wrappedValid, cpphdl::logic<1>(mode_in()[0]), wrappedAddr,
                wrappedOp, wrappedData, cpphdl::logic<1>(mode_in()[1]), wrappedReady,
                cpphdl::logic<2>(input_in()), input_in());
            if (uint64_t(mode_in()) == 3) {
                unsigned char deadValid = 3, deadReady = 7;
                int deadAddr = -1, deadOp = 6, deadData = -2;
                (void)::debug_tick(&deadValid, 0, &deadAddr, &deadOp, &deadData, 1, &deadReady, -9, 5);
            }
            if (mode_in()[1]) (void)::random();
            sampled._next = first | (second << 32);
        }
    }
    void _strobe() { sampled.strobe(); }
};

GraphDebug cpphdl_top;

#ifdef CPP_GRAPH_DEBUG_RUN
#include "model.h"
#include <array>
#include <cstdio>

long _system_clock = 0;
static unsigned calls, randomCalls;
static uint64_t history;
extern "C" long __real_random() noexcept;
extern "C" long __wrap_random() noexcept { ++randomCalls; return __real_random(); }
extern "C" int debug_tick(unsigned char* reqValid, unsigned char reqReady, int* addr, int* op,
                           int* data, unsigned char respValid, unsigned char* respReady, int resp, int respData) {
    ++calls;
    uint64_t inputs[] = {*reqValid, reqReady, uint32_t(*addr), uint32_t(*op), uint32_t(*data),
                         respValid, *respReady, uint32_t(resp), uint32_t(respData)};
    for (auto input : inputs) history = (history ^ input ^ calls) * 1099511628211ull;
    uint32_t draw = uint32_t(::random());
    *reqValid = static_cast<unsigned char>(draw >> 8);
    if (calls & 1) *addr = static_cast<int>(draw ^ uint32_t(respData) ^ 0x80000000u);
    *op = static_cast<int>(uint32_t(*op) + draw + reqReady);
    if (calls % 3) *data = static_cast<int>(uint32_t(resp) ^ draw ^ respValid);
    if (calls % 5) *respReady ^= static_cast<unsigned char>(draw);
    return calls & 1 ? -int(draw) - 1 : int(draw);
}

int main() {
    constexpr unsigned samples = 2048;
    struct Sample { std::array<uint64_t, 8> ports; uint64_t history; unsigned calls, randomCalls; };
    std::array<Sample, samples> expected;
    cpphdl::logic<2> mode;
    cpphdl::logic<32> input;
    cpphdl_top.mode_in = _ASSIGN(mode);
    cpphdl_top.input_in = _ASSIGN(input);
    for (unsigned seed : {1u, 23u, 1234567u}) {
        ::srandom(seed); calls = randomCalls = 0; history = 0;
        for (unsigned sample = 0; sample < samples; ++sample) {
            mode = sample & 3; input = uint32_t(sample * 2654435761u);
            ++_system_clock;
            cpphdl_top._work(sample % 31 == 0);
            cpphdl_top._strobe();
            ++_system_clock;
            expected[sample] = {{uint64_t(cpphdl_top.raw0_out()), uint64_t(cpphdl_top.raw1_out()),
                uint64_t(cpphdl_top.result_out()), uint64_t(cpphdl_top.wrapped0_out()), uint64_t(cpphdl_top.wrapped1_out()),
                uint64_t(cpphdl_top.wrapped_result_out()), uint64_t(cpphdl_top.cached_out()),
                uint64_t(cpphdl_top.sampled_out())}, history, calls, randomCalls};
        }
        cpphdl_native::Model model;
        ::srandom(seed); calls = randomCalls = 0; history = 0;
        for (unsigned sample = 0; sample < samples; ++sample) {
            model.mode[0] = sample & 3; model.input[0] = sample * 2654435761u;
            model.work_reset[0] = sample % 31 == 0;
            auto before = calls, randomBefore = randomCalls;
            model.eval(false); model.eval(false);
            if (calls != before || randomCalls != randomBefore) return 1;
            if (sample & 1) model.step();
            else { model.eval(true); model.eval(false); }
            model.eval(false);
            auto word = [](const auto& port) { return uint64_t(port[0]) | (uint64_t(port[1]) << 32); };
            std::array<uint64_t, 8> actual{word(model.raw0), word(model.raw1), word(model.result), word(model.wrapped0),
                word(model.wrapped1), word(model.wrapped_result), model.cached[0], word(model.sampled)};
            const auto& wanted = expected[sample];
            if (actual != wanted.ports || history != wanted.history || calls != wanted.calls || randomCalls != wanted.randomCalls) {
                std::fprintf(stderr, "debug seed=%u sample=%u calls=%u/%u random=%u/%u\n",
                    seed, sample, calls, wanted.calls, randomCalls, wanted.randomCalls);
                return 2;
            }
        }
    }
    std::puts("ordinary C++ graph: 6144 debug transactions match return, five outputs, adapters, cache versions, call order and libc stream");
}
#endif

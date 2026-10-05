#pragma once
#include <array>
#include <cstdio>
#include <deque>
#include <random>
#include <stdexcept>
#include "Transformer.h"
#ifdef VERILATOR
#include "VWeightProduct.h"
#endif

long _system_clock = 0;

class ProductBench : public cpphdl::Module {
    using Q = integer_llm::Q;
    using Wide = integer_llm::Wide;
    using UWide = integer_llm::UWide;
#ifdef VERILATOR
    VWeightProduct dut;
#else
    WeightProduct dut;
#endif
    std::mt19937 random{0x4c4c4d};
    bool offered = false, consumer = true, memory_ready = false, memory_valid = false;
    bool memory_error = false, pending = false;
    bool expect_fault = false, fault_seen = false;
    unsigned delay = 0;
    uint32_t address = 0, request = 0;
    uint64_t activation = 0;
    cpphdl::logic<LLM_DDR_BITS> beat{0};
#ifndef VERILATOR
    struct MemoryResponder : cpphdl::Module {
        cpphdl::hls::DramReadIf<LLM_DDR_BITS> weights_in;
        bool& ready;
        bool& valid;
        bool& error;
        cpphdl::logic<LLM_DDR_BITS>& data;
        MemoryResponder(bool& r, bool& v, bool& e, cpphdl::logic<LLM_DDR_BITS>& d)
            : ready(r), valid(v), error(e), data(d) {}
        void _assign() {
            weights_in.ready_out = _ASSIGN(ready);
            weights_in.valid_out = _ASSIGN(valid);
            weights_in.error_out = _ASSIGN(error);
            weights_in.data_out = _ASSIGN(data);
        }
    } responder{memory_ready, memory_valid, memory_error, beat};
#endif
    std::deque<UWide> expected;
    std::deque<UWide> received;
public:
    std::array<Q, 1024> memory{};
    uint64_t clocks = 0, reads = 0, accepts = 0, results = 0;
    ProductBench() {
#ifndef VERILATOR
        _assign();
#endif
        reset();
    }
#ifndef VERILATOR
    void _assign() {
        dut.valid_in = _ASSIGN(offered); dut.ready_in = _ASSIGN(consumer);
        dut.address_in = _ASSIGN(address); dut.activation_in = _ASSIGN(activation);
        assignIf(dut, responder, dut.weights_out, responder.weights_in);
    }
#endif
    void reset() {
        offered = false; consumer = true; pending = false; memory_valid = false;
        memory_error = false; expected.clear(); received.clear();
        expect_fault = false; fault_seen = false;
        tick(true);
    }
    bool tick(bool reset = false) {
#ifndef VERILATOR
        // Before the first reset, combinational outputs may depend on
        // uninitialized registers. Do not sample them in the native bench.
        if (reset) {
            dut._work(true); dut._strobe();
            ++clocks; ++_system_clock;
            return false;
        }
#endif
        memory_ready = !pending && !memory_valid && random() % 4 != 0;
        if (pending && !memory_valid) {
            if (delay) --delay;
            else {
                beat = 0;
                for (unsigned i = 0; i < LLM_DDR_BITS / 64; ++i)
                    beat.bits(64 * i + 63, 64 * i) = uint64_t(memory.at(request / 8 + i));
                memory_valid = true;
            }
        }
#ifdef VERILATOR
        dut.clk = 0; dut.reset = reset;
        dut.valid_in = offered; dut.ready_in = consumer;
        dut.address_in = address; dut.activation_in = activation;
        dut.weights_out___05Fready_in = memory_ready;
        dut.weights_out___05Fvalid_in = memory_valid;
        dut.weights_out___05Ferror_in = memory_error;
#if LLM_DDR_BITS == 64
        dut.weights_out___05Fdata_in = uint64_t(beat);
#else
        for (unsigned i = 0; i < LLM_DDR_BITS / 32; ++i)
            dut.weights_out___05Fdata_in[i] = uint32_t(beat.bits(i * 32 + 31, i * 32));
#endif
        dut.eval();
        bool accepted = offered && dut.ready_out;
        bool request_fire = dut.weights_out___05Fvalid_out && memory_ready;
        bool response_fire = dut.weights_out___05Fready_out && memory_valid;
        bool output_valid = dut.valid_out;
        uint32_t request_address = dut.weights_out___05Faddr_out;
        UWide answer = 0;
        for (unsigned i = 0; i < 4; ++i) answer |= UWide(dut.product_out[i]) << (i * 32);
        bool fault = dut.fault_out;
#else
        bool accepted = offered && dut.ready_out();
        bool request_fire = dut.weights_out.valid_in() && memory_ready;
        bool response_fire = dut.weights_out.ready_in() && memory_valid;
        bool output_valid = dut.valid_out();
        uint32_t request_address = uint32_t(dut.weights_out.addr_in());
        auto bits = dut.product_out();
        UWide answer = (UWide(uint64_t(bits >> 64)) << 64) | uint64_t(bits);
        bool fault = dut.fault_out();
#endif
        if (!reset) {
            fault_seen = fault;
            if (fault && !expect_fault) throw std::runtime_error("unexpected DDR/product fault");
            if (output_valid) {
                if (expected.empty() || expected.front() != answer)
                    throw std::runtime_error("128-bit product or response ordering mismatch");
                if (consumer) { expected.pop_front(); received.push_back(answer); ++results; }
            }
            if (accepted) {
                expected.push_back(UWide(Wide(Q(activation)) * memory.at(address / 8)));
                ++accepts;
            }
        }
#ifdef VERILATOR
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#else
        dut._work(reset); dut._strobe();
#endif
        if (response_fire) { pending = false; memory_valid = false; }
        if (request_fire) {
            if (pending || request_address % (LLM_DDR_BITS / 8))
                throw std::runtime_error("illegal DDR request");
            request = request_address; pending = true; delay = 1 + random() % 11; ++reads;
        }
        ++clocks; ++_system_clock;
        return accepted;
    }
    Q dot(uint32_t base, const Q* input, unsigned size) {
        unsigned sent = 0, got = 0, watchdog = 0;
        UWide sum = 0;
        while (sent != size || got != size) {
            if (++watchdog > 100000) throw std::runtime_error("DDR dot-product timeout");
            offered = sent != size;
            address = base + sent * 8;
            activation = sent != size ? uint64_t(input[sent]) : 0;
            consumer = random() % 5 != 0;
            if (tick()) ++sent;
            while (!received.empty()) { sum += received.front(); received.pop_front(); ++got; }
        }
        offered = false; consumer = true;
        return Q(Wide(sum) >> 48);
    }
    void cancel_inflight() {
        offered = true; address = 512; activation = 123;
        for (unsigned i = 0; i < 3; ++i) tick();
        reset();
        for (unsigned i = 0; i < 30; ++i) tick();
        if (!received.empty()) throw std::runtime_error("reset leaked a response");
    }
    void continuous_hits() {
        Q x = integer_llm::one;
        dot(64, &x, 1);
        const auto initial_reads = reads;
        offered = true; address = 64; consumer = true;
        for (unsigned i = 0; i < 48; ++i) {
            activation = uint64_t(Q(i + 1) * (integer_llm::one / 64));
            if (!tick()) throw std::runtime_error("cached pipeline did not sustain II=1");
        }
        offered = false;
        for (unsigned i = 0; !expected.empty() && i < 1000; ++i) tick();
        if (!expected.empty() || received.size() != 48 || reads != initial_reads)
            throw std::runtime_error("cached burst did not drain exactly once");
        received.clear();
    }
    void faults() {
        reset(); expect_fault = true; offered = true; address = 3;
        const auto initial_reads = reads;
        tick(); tick();
        if (!fault_seen || reads != initial_reads) throw std::runtime_error("unaligned address reached DDR");
        reset(); expect_fault = true; offered = true; address = 64; memory_error = true;
        for (unsigned i = 0; !fault_seen && i < 1000; ++i) tick();
        if (!fault_seen || !expected.empty() || !received.empty())
            throw std::runtime_error("DDR error was not reported without accepting a product");
        reset();
    }
};

int main() {
    try {
        ProductBench bench;
        std::mt19937_64 random(0x51483438);
        std::array<integer_llm::Q, 31> input{};
        for (auto& word : bench.memory) word = int64_t(random() & ((uint64_t(1) << 53) - 1)) - (int64_t(1) << 52);
        for (unsigned row = 0; row < 80; ++row) {
            for (auto& x : input) x = int64_t(random() & ((uint64_t(1) << 53) - 1)) - (int64_t(1) << 52);
            unsigned size = 1 + row % input.size();
            unsigned start = (row * 19) % (bench.memory.size() - input.size());
            integer_llm::Wide reference = 0;
            for (unsigned i = 0; i < size; ++i) reference += integer_llm::Wide(input[i]) * bench.memory[start + i];
            auto actual = bench.dot(start * 8, input.data(), size);
            if (actual != int64_t(reference >> 48)) throw std::runtime_error("Q16.48 row rounded before accumulation");
            if (row == 39) bench.cancel_inflight();
        }
        if (LLM_DDR_BITS > 64 && bench.reads >= bench.accepts)
            throw std::runtime_error("DDR beat cache did not reuse weights");
        bench.continuous_hits();
        bench.faults();
        for (auto a : {INT64_MIN, INT64_MAX, int64_t(-1), int64_t(0), int64_t(1)}) {
            bench.reset(); bench.memory[0] = a;
            for (auto b : {INT64_MIN, INT64_MAX, int64_t(-1), int64_t(0), int64_t(1)}) {
                auto actual = bench.dot(0, &b, 1);
                if (actual != int64_t((integer_llm::Wide(a) * b) >> 48))
                    throw std::runtime_error("signed product boundary mismatch");
            }
        }
        // End-to-end algorithm check with all projection/output matvec products
        // offloaded to the DUT. Control, reductions and nonlinear math remain
        // host C++; this is NOT a fully synthesized transformer.
        bench.reset();
        using C = integer_llm::SmallModel;
        using L = integer_llm::Layout<C>;
        static_assert(L::words <= 1024);
        for (auto& word : bench.memory) word = (int64_t(random() % 65536) - 32768) * (integer_llm::one / 262144);
        for (unsigned i = 0; i < C::dim; ++i) {
            bench.memory[L::final_norm + i] = integer_llm::one;
            bench.memory[L::layer_start + L::in + i] = integer_llm::one;
            bench.memory[L::layer_start + L::post + i] = integer_llm::one;
        }
        // RoPE for the two-component head, positions 0..3, prepared in host memory.
        const int64_t rotations[8] = {281474976710656LL, 0,
            152081576426341LL, 236853021793284LL, -117134918411447LL, 255938459051995LL,
            -278658113292530LL, 39721750808104LL};
        for (unsigned i = 0; i < 8; ++i) bench.memory[L::rope + i] = rotations[i];
        integer_llm::Transformer<C> reference, accelerated;
        integer_llm::NativeDot native_dot;
        auto hardware_dot = [&](const int64_t* weights, const int64_t* x, unsigned count) {
            return bench.dot(uint32_t(weights - bench.memory.data()) * 8, x, count);
        };
        for (unsigned sequence = 0; sequence < 2; ++sequence) {
            reference.reset(); accelerated.reset();
            uint32_t token = sequence + 1;
            for (unsigned position = 0; position < C::context; ++position) {
                int64_t expected_logits[C::vocab], actual_logits[C::vocab];
                uint32_t expected_token = 0, actual_token = 0;
                if (!reference.forward(token, bench.memory.data(), expected_logits, expected_token, native_dot) ||
                    !accelerated.forward(token, bench.memory.data(), actual_logits, actual_token, hardware_dot))
                    throw std::runtime_error("transformer refused a valid token");
                for (unsigned i = 0; i < C::vocab; ++i)
                    if (expected_logits[i] != actual_logits[i]) throw std::runtime_error("transformer logit mismatch");
                if (expected_token != actual_token) throw std::runtime_error("transformer token mismatch");
                token = actual_token;
            }
            int64_t logits[C::vocab]; uint32_t answer;
            if (accelerated.forward(0, bench.memory.data(), logits, answer, hardware_dot))
                throw std::runtime_error("context overflow accepted");
            accelerated.reset();
            if (accelerated.forward(C::vocab, bench.memory.data(), logits, answer, hardware_dot))
                throw std::runtime_error("invalid token accepted");
        }
        std::puts("8 transformer tokens: all logits match; matvec products offloaded, remaining algorithm in host C++");
        std::printf("Q16.48 DDR products: %llu accepted, %llu responses, %llu DDR reads, %llu clocks; %u-bit bus\n",
            (unsigned long long)bench.accepts, (unsigned long long)bench.results,
            (unsigned long long)bench.reads, (unsigned long long)bench.clocks, LLM_DDR_BITS);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "LLM product: %s\n", e.what()); return 1; }
}

#pragma once
#include <array>
#include <cstdio>
#include <random>
#include <stdexcept>
#ifdef VERILATOR
#include "VMatrixMath.h"
#endif
long _system_clock = 0;

class MatrixBench : public cpphdl::Module {
    using Q = integer_llm::Q;
#ifdef VERILATOR
    VMatrixMath dut;
#else
    MatrixMath dut;
#endif
    bool load = false, command = false, ready = true;
    uint8_t load_address = 0, rows = 1, columns = 1, depth = 1;
    uint64_t load_data = 0;
    uint32_t base = 0;
    bool mem_ready = false, mem_valid = false, pending = false;
    unsigned delay = 0, request = 0;
    cpphdl::logic<LLM_DDR_BITS> beat{0};
    std::array<Q, 128> weights{};
    std::array<Q, 64> input{}, expected{};
    std::mt19937_64 rng{785};
    unsigned outputs = 0;
    bool allow_fault = false;
#ifndef VERILATOR
    struct Responder : cpphdl::Module {
        cpphdl::hls::DramReadIf<LLM_DDR_BITS> weights_in;
        MatrixBench& b;
        explicit Responder(MatrixBench& b) : b(b) {}
        void _assign() {
            weights_in.ready_out = _ASSIGN(b.mem_ready);
            weights_in.valid_out = _ASSIGN(b.mem_valid);
            weights_in.data_out = _ASSIGN(b.beat);
            weights_in.error_out = _ASSIGN(false);
        }
    } responder{*this};
#endif
public:
    unsigned clocks = 0, reads = 0, matrices = 0;
    MatrixBench() {
#ifndef VERILATOR
        _assign();
#endif
        reset();
    }
#ifndef VERILATOR
    void _assign() {
        dut.load_in = _ASSIGN(load); dut.load_address_in = _ASSIGN(load_address);
        dut.load_data_in = _ASSIGN(load_data); dut.command_valid_in = _ASSIGN(command);
        dut.rows_in = _ASSIGN(rows); dut.columns_in = _ASSIGN(columns); dut.depth_in = _ASSIGN(depth);
        dut.base_in = _ASSIGN(base); dut.ready_in = _ASSIGN(ready);
        assignIf(dut, responder, dut.weights_out, responder.weights_in);
    }
#endif
    void reset() {
        load = false; command = false; pending = false; mem_valid = false;
        ready = true; outputs = 0; allow_fault = false;
        tick(true);
    }
    bool tick(bool reset = false) {
#ifndef VERILATOR
        if (reset) {
            dut._work(true); dut._strobe(); ++_system_clock; ++clocks;
            return false;
        }
#endif
        mem_ready = !pending && !mem_valid && rng() % 4 != 0;
        if (pending && !mem_valid) {
            if (delay) --delay;
            else {
                beat = 0;
                for (unsigned i = 0; i < LLM_DDR_BITS / 64; ++i)
                    beat.bits(i * 64 + 63, i * 64) = uint64_t(weights.at(request / 8 + i));
                mem_valid = true;
            }
        }
#ifdef VERILATOR
        dut.clk = 0; dut.reset = reset;
        dut.load_in = load; dut.load_address_in = load_address; dut.load_data_in = load_data;
        dut.command_valid_in = command; dut.rows_in = rows; dut.columns_in = columns;
        dut.depth_in = depth; dut.base_in = base; dut.ready_in = ready;
        dut.weights_out___05Fready_in = mem_ready;
        dut.weights_out___05Fvalid_in = mem_valid;
        dut.weights_out___05Ferror_in = 0;
#if LLM_DDR_BITS == 64
        dut.weights_out___05Fdata_in = uint64_t(beat);
#else
        for (unsigned i = 0; i < LLM_DDR_BITS / 32; ++i)
            dut.weights_out___05Fdata_in[i] = uint32_t(beat >> (i * 32));
#endif
        dut.eval();
        bool accepted = command && dut.command_ready_out;
        bool request_fire = dut.weights_out___05Fvalid_out && mem_ready;
        bool response_fire = dut.weights_out___05Fready_out && mem_valid;
        unsigned address = dut.weights_out___05Faddr_out;
        bool valid = dut.valid_out;
        Q result = Q(dut.data_out);
        unsigned r = dut.row_out, c = dut.column_out;
        bool fault = dut.fault_out;
#else
        bool accepted = command && dut.command_ready_out();
        bool request_fire = dut.weights_out.valid_in() && mem_ready;
        bool response_fire = dut.weights_out.ready_in() && mem_valid;
        unsigned address = uint32_t(dut.weights_out.addr_in());
        bool valid = dut.valid_out();
        Q result = Q(dut.data_out());
        unsigned r = dut.row_out(), c = dut.column_out();
        bool fault = dut.fault_out();
#endif
        if (!reset) {
            if (fault && !allow_fault) throw std::runtime_error("matrix fault");
            if (valid) {
                if (outputs >= unsigned(rows) * columns || r != outputs / columns || c != outputs % columns ||
                    result != expected.at(outputs)) {
                    std::fprintf(stderr, "matrix %u result %u at (%u,%u): got %lld expected %lld\n",
                        matrices, outputs, r, c, (long long)result, (long long)expected.at(outputs));
                    throw std::runtime_error("hardware mmul mismatch");
                }
                if (ready) ++outputs;
            }
        }
#ifdef VERILATOR
        dut.clk = 1; dut.eval();
#else
        dut._work(reset); dut._strobe();
#endif
        if (response_fire) { pending = false; mem_valid = false; }
        if (request_fire && !reset) {
            if (pending || address % (LLM_DDR_BITS / 8)) throw std::runtime_error("bad DDR transaction");
            request = address; pending = true; delay = 1 + rng() % 11; ++reads;
        }
        ++_system_clock; ++clocks;
        return !reset && accepted;
    }
    void matrix(unsigned m, unsigned n, unsigned k, bool edges = false) {
        reset(); rows = m; columns = n; depth = k;
        base = matrices % 2 ? 24 : 0; // Cross DDR beat boundaries with unaligned rows.
        for (auto& w : weights) w = Q(rng() % (4 * integer_llm::one)) - 2 * integer_llm::one;
        for (auto& x : input) x = Q(rng() % (4 * integer_llm::one)) - 2 * integer_llm::one;
        if (edges) {
            static constexpr Q v[] = {INT64_MIN, INT64_MAX, -1, 1, 0, integer_llm::one, -integer_llm::one, 17};
            for (unsigned i = 0; i < 64; ++i) { weights[base / 8 + i] = v[i % 8]; input[i] = v[(i + 3) % 8]; }
        }
        integer_llm::mmul(expected.data(), weights.data() + base / 8, input.data(), m, n, k);
        for (unsigned i = 0; i < n * k; ++i) {
            load = true; load_address = i; load_data = uint64_t(input[i]); tick();
        }
        load = false; command = true;
        if (!tick()) throw std::runtime_error("idle matrix refused command");
        command = false;
        unsigned watchdog = 0;
        while (outputs != m * n && ++watchdog < 100000) { ready = rng() % 4 != 0; tick(); }
        if (outputs != m * n) throw std::runtime_error("mmul timeout");
        for (unsigned i = 0; i < 20; ++i) tick();
        ++matrices;
    }
    void invalid() {
        reset(); rows = 0; columns = 1; depth = 1; command = true; allow_fault = true;
        tick(); command = false; tick();
#ifdef VERILATOR
        if (!dut.fault_out) throw std::runtime_error("invalid geometry accepted");
#else
        if (!dut.fault_out()) throw std::runtime_error("invalid geometry accepted");
#endif
        reset();
    }
    void cancel() {
        reset(); rows = 1; columns = 1; depth = 8; base = 0;
        for (unsigned i = 0; i < 8; ++i) {
            load = true; load_address = i; load_data = integer_llm::one; tick();
        }
        load = false; command = true; tick(); command = false;
        for (unsigned i = 0; i < 4; ++i) tick();
        reset();
        for (unsigned i = 0; i < 100; ++i) tick();
        if (outputs) throw std::runtime_error("reset leaked a matrix result");
    }
};

int main() {
    MatrixBench bench;
    bench.matrix(8, 8, 8, true);
    bench.matrix(2, 3, 1);
    bench.matrix(1, 1, 8);
#if LLM_MAX_DEPTH >= 17
    bench.matrix(2, 3, 17);
#endif
    for (unsigned i = 0; i < 30; ++i) bench.matrix(1 + i % 8, 1 + (i * 3) % 8, 1 + (i * 5) % 8);
    bench.invalid();
    bench.cancel();
    std::printf("PASS: %u hardware matrices, %u DDR reads, %u clocks; full128 accumulation and final rounding\n",
        bench.matrices, bench.reads, bench.clocks);
}

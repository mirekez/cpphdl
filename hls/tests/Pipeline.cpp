#include "../Clocked.h"

#ifndef PIPELINE_STAGES
#define PIPELINE_STAGES 3
#endif
#ifndef PIPELINE_LATENCY
#define PIPELINE_LATENCY PIPELINE_STAGES
#endif

#ifndef PIPELINE_CUSTOM_METHODS
struct PipelineMethods {
    static uint32_t mix(uint32_t a, uint32_t b) {
        uint32_t sum = a + b;
        return (sum * (a ^ 0x12345u)) + (b >> 3);
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t result;
        if (operation & 1) result = mix(value,index) ^ (index << 5);
        else if (operation & 2) result = mix(index,value) + mix(value,17);
        else result = value;
        // The tag takes a shorter path than the calculation, but must arrive
        // with the result of the same invocation.
        return (uint64_t(index) << 32) | result;
    }
};
#endif

class PipelineTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedPipeline<PipelineMethods, PIPELINE_STAGES> worker;
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
    void _assign() {
        worker.command_valid_in = _ASSIGN(command_valid_in());
        worker.operation_in = _ASSIGN(operation_in());
        worker.index_in = _ASSIGN(index_in());
        worker.value_in = _ASSIGN(value_in());
        worker.response_ready_in = _ASSIGN(response_ready_in());
        worker._assign();
        command_ready_out = _ASSIGN(worker.command_ready_out());
        response_valid_out = _ASSIGN(worker.response_valid_out());
        result_out = _ASSIGN(worker.result_out());
        fault_out = _ASSIGN(worker.fault_out());
    }
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};

#ifndef SYNTHESIS
#include <array>
#include <cstdio>
#include <random>
#include <stdexcept>
#ifdef VERILATOR
#include "VPipelineTop.h"
#endif
long _system_clock = 0;

int main() {
    try {
        bool valid = false, ready = true;
        uint32_t operation = 0, index = 0, value = 0;
        std::array<bool,PIPELINE_LATENCY> occupied{};
        std::array<uint64_t,PIPELINE_LATENCY> expected{};
        std::array<PipelineMethods,PIPELINE_LATENCY> updates{};
        PipelineMethods reference{};
        unsigned accepted = 0, delivered = 0;
#ifdef VERILATOR
        VPipelineTop dut;
        auto drive = [&](bool reset) {
            dut.clk = 0; dut.reset = reset;
            dut.command_valid_in = valid; dut.response_ready_in = ready;
            dut.operation_in = operation; dut.index_in = index; dut.value_in = value;
            dut.eval();
        };
        auto tick = [&](bool reset) { drive(reset); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval(); ++_system_clock; };
        auto inputReady = [&] { return bool(dut.command_ready_out); };
        auto outputValid = [&] { return bool(dut.response_valid_out); };
        auto result = [&] { return uint64_t(dut.result_out); };
        auto fault = [&] { return uint32_t(dut.fault_out); };
#else
        PipelineTop dut;
        dut.command_valid_in = _ASSIGN(valid); dut.response_ready_in = _ASSIGN(ready);
        dut.operation_in = _ASSIGN(operation); dut.index_in = _ASSIGN(index); dut.value_in = _ASSIGN(value);
        dut._assign();
        auto drive = [&](bool) {};
        auto tick = [&](bool reset) { dut._work(reset); dut._strobe(); ++_system_clock; };
        auto inputReady = [&] { return dut.command_ready_out(); };
        auto outputValid = [&] { return dut.response_valid_out(); };
        auto result = [&] { return dut.result_out(); };
        auto fault = [&] { return dut.fault_out(); };
#endif
        tick(true);
        auto cycle = [&](bool reset = false) {
            drive(reset);
            bool advance = !occupied.back() || ready;
            if (!reset) {
                if (inputReady() != advance || outputValid() != occupied.back())
                    throw std::runtime_error("pipeline throughput/latency mismatch");
                if (occupied.back() && (result() != expected.back() || fault()))
                    throw std::runtime_error("pipeline lost or misaligned data/tag under stalls");
                if (occupied.back() && ready) ++delivered;
            }
            if (reset) { occupied.fill(false); expected.fill(0); reference = PipelineMethods{}; }
            else if (advance) {
                auto candidate = reference;
                uint64_t answer = valid ? candidate.command(operation,index,value) : 0;
                if constexpr (PIPELINE_LATENCY == 1) { if (valid) reference = candidate; }
                else if (occupied[PIPELINE_LATENCY-2]) reference = updates[PIPELINE_LATENCY-2];
                for (unsigned i = PIPELINE_LATENCY-1; i; --i) {
                    occupied[i] = occupied[i-1]; expected[i] = expected[i-1];
                    updates[i] = updates[i-1];
                }
                occupied[0] = valid;
                if (valid) { expected[0] = answer; updates[0] = candidate; ++accepted; }
            }
            tick(reset);
            return advance;
        };
        // No testbench waiting: a distinct input on EVERY edge.
        valid = true;
        for (unsigned i = 0; i < 1024; ++i) {
            operation = i & 3; index = i; value = i * 0x112233u;
            if (!cycle()) throw std::runtime_error("II=1 was not achieved");
        }
        valid = false;
        for (unsigned i = 0; i <= PIPELINE_LATENCY; ++i) cycle();
        if (accepted != 1024 || delivered != accepted) throw std::runtime_error("pipeline drain mismatch");

        std::mt19937 random(0x3157);
        bool mayChange = true;
        for (unsigned i = 0; i < 4000; ++i) {
            if (mayChange) {
                valid = (random() & 3) != 0;
                operation = random(); index = random(); value = random();
            }
            ready = i % 173 < 90 && (random() & 3) != 0;
            bool reset = i == 511 || i == 999 || i == 2999;
            mayChange = cycle(reset) || reset;
        }
        ready = true; valid = false;
        for (unsigned i = 0; i <= PIPELINE_LATENCY; ++i) cycle();
        std::printf("Pipeline: II=1, %u stages, %u accepted, %u delivered; stalls/reset/branches/helpers passed\n",
            PIPELINE_LATENCY,accepted,delivered);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"Pipeline failure at clock %ld: %s\n",_system_clock,error.what());
        return 1;
    }
}
#endif

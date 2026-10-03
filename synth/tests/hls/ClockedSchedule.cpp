#include "../../../hls/Clocked.h"
#ifndef HLS_RAM
#define HLS_RAM 0
#endif

struct ScheduledMethods {
    uint32_t words[4] = {};
    uint32_t count = 0;
    uint32_t transform(uint32_t x) {
        x += 17;
        x ^= x >> 3;
        x *= count | 1u;
        return (x + count) ^ (x << 2);
    }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        uint32_t result = 0;
        if (operation == 0) {
            words[index & 3] = transform(value);
            ++count;
            return words[index & 3];
        }
        for (uint32_t i = 0; i < 4; ++i) result += words[i] ^ value;
        return result;
    }
};

class ScheduledTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<ScheduledMethods, 0, 16, 64, HLS_RAM, HLS_RAM> worker;
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
    void _work(bool reset) {
#if SYNTH_SCHEDULE_ERROR == 1
        if (command_valid_in()) worker._work(reset);
#elif SYNTH_SCHEDULE_ERROR != 2
        worker._work(reset);
#endif
    }
    void _strobe() {
#if SYNTH_SCHEDULE_ERROR != 3
        worker._strobe();
#endif
    }
};

#ifndef SYNTHESIS
#include <cstdio>
#include <stdexcept>
#ifdef VERILATOR
#include "VScheduledTop.h"
#endif
long _system_clock = 0;
struct Snapshot { bool ready, valid; uint64_t result; uint32_t fault; };
class Bench {
#ifdef VERILATOR
    VScheduledTop dut;
    void edge() { dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval(); ++clocks; }
#else
    ScheduledTop dut;
#endif
public:
    uint64_t clocks = 0;
    uint64_t trace = 14695981039346656037ull;
    uint32_t operation = 0, index = 0, value = 0;
    bool valid = false, ready = false;
    Bench() {
#ifndef VERILATOR
        dut.command_valid_in = _ASSIGN(valid); dut.response_ready_in = _ASSIGN(ready);
        dut.operation_in = _ASSIGN(operation); dut.index_in = _ASSIGN(index); dut.value_in = _ASSIGN(value);
        dut._assign();
#endif
        reset();
    }
    void reset() {
#ifdef VERILATOR
        dut.clk = 0; dut.reset = 1; dut.eval(); edge(); dut.reset = 0;
#else
        dut._work(true); dut._strobe(); ++_system_clock;
#endif
    }
    Snapshot step() {
        Snapshot result;
#ifdef VERILATOR
        dut.operation_in = operation; dut.index_in = index; dut.value_in = value;
        dut.command_valid_in = valid; dut.response_ready_in = ready; dut.eval();
#ifdef RETIMED
        if (!dut.retiming_ready_out) throw std::runtime_error("expected transaction admission");
        if (!dut.retiming_commit_out) {
            edge();
            unsigned wait = 0;
            // Inputs may change immediately after admission. The entire
            // logical transition must still use the captured bundle.
            dut.operation_in = 91; dut.index_in = 99; dut.value_in = 0xdeadbeef;
            dut.command_valid_in = !valid; dut.response_ready_in = !ready; dut.eval();
            while (!dut.retiming_commit_out) {
                if (++wait > 1000) throw std::runtime_error("pipeline commit timeout");
                edge();
            }
        }
#endif
        result = {bool(dut.command_ready_out), bool(dut.response_valid_out), dut.result_out, dut.fault_out};
        edge();
#else
        result = {dut.command_ready_out(), dut.response_valid_out(), dut.result_out(), dut.fault_out()};
        dut._work(false); dut._strobe(); ++_system_clock; ++clocks;
#endif
        if (result.fault) throw std::runtime_error("scheduled method fault");
        for (uint64_t part : {uint64_t(result.ready), uint64_t(result.valid), result.result, uint64_t(result.fault)})
            trace = (trace ^ part) * 1099511628211ull;
        return result;
    }
    void cancel() {
#ifdef RETIMED
        dut.command_valid_in = 1; dut.operation_in = 0; dut.index_in = 2; dut.value_in = 123456;
        dut.eval(); edge();
#endif
        reset(); valid = false; ready = false;
    }
};
int main() {
    try {
        Bench bench;
        ScheduledMethods reference;
        for (unsigned epoch = 0; epoch < 2; ++epoch) {
            for (unsigned n = 0; n < 100; ++n) {
                bench.operation = n % 3 == 0 ? 1 : 0;
                bench.index = n & 3; bench.value = 0xfedcba98u + n * 73;
                uint64_t expected = reference.command(bench.operation, bench.index, bench.value);
                bench.valid = true;
                unsigned wait = 0;
                while (!bench.step().ready) if (++wait > 2000) throw std::runtime_error("command timeout");
                bench.valid = false;
                bench.operation = 99; bench.index = 99; bench.value = 99;
                Snapshot result;
                wait = 0;
                do {
                    result = bench.step();
                    if (++wait > 2000) throw std::runtime_error("response timeout");
                } while (!result.valid);
                if (result.result != expected) {
                    std::fprintf(stderr, "command %u expected %llu got %llu\n", n,
                        (unsigned long long)expected, (unsigned long long)result.result);
                    throw std::runtime_error("response mismatch");
                }
                for (unsigned stall = 0; stall < 3; ++stall) {
                    result = bench.step();
                    if (!result.valid || result.result != expected || result.ready)
                        throw std::runtime_error("backpressure corrupted response");
                }
                bench.ready = true; bench.step(); bench.ready = false;
            }
            bench.cancel(); reference = ScheduledMethods{};
        }
        std::printf("200 scheduled commands passed (%llu physical clocks)\n", (unsigned long long)bench.clocks);
        std::printf("cycle trace: %llx\n", (unsigned long long)bench.trace);
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
#endif

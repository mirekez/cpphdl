#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include <deque>
#include <random>
#include <stdexcept>
#ifdef VERILATOR
#include "VDramStream.h"
#endif

long _system_clock = 0;

class DramBench : public cpphdl::Module {
    struct Command { uint32_t index, count, scale, tag; };
    struct Answer { uint64_t data; uint32_t tag, error; };
    struct Read { uint32_t index; uint64_t due; bool error; };
#ifdef VERILATOR
    VDramStream dut;
#else
    DramStream dut;
    struct NativeMemory : cpphdl::Module {
        cpphdl::hls::ExternalMemoryIf<> memory_in;
        void _assign() {
            memory_in.ready_out = _ASSIGN(false);
            memory_in.valid_out = _ASSIGN(false);
            memory_in.data_out = _ASSIGN(uint64_t(0));
            memory_in.error_out = _ASSIGN(false);
        }
    } native_memory;
#endif
    std::array<uint64_t, 1024> memory{};
    std::array<unsigned, 1024> requested{};
    std::deque<Answer> answers;
    std::deque<Read> reads;
    std::mt19937_64 random{0xdd12345};
    std::mt19937_64 commands{0xc012345};
    Command command{};
    bool valid = false, ready = true, expect_error = false, fast = false;
    bool held = false, held_request = false;
    bool pause_responses = false;
    Answer held_answer{};
    uint32_t held_address = 0;
    unsigned received = 0, accepted = 0, memory_reads = 0, overlaps = 0, max_outstanding = 0;
    unsigned request_run = 0, longest_request_run = 0;
    unsigned answer_run = 0, longest_answer_run = 0;
    unsigned request_stalls = 0, result_stalls = 0;
    uint64_t last_request = 0, last_answer = 0, clocks = 0, start = 0;
    static void require(bool good, const char* text) { if (!good) throw std::runtime_error(text); }
    Answer reference(const Command& c) {
        uint64_t sum = 0;
        for (unsigned i = c.index; i < c.index + c.count; ++i) sum += memory.at(i);
        uint64_t product = (sum & 0xffffffffull) * uint64_t(c.scale) + (sum >> 32);
        uint64_t result = (product ^ (product >> 17)) + 0x12345678ull;
        bool error = expect_error && c.index == 1000;
        return {error ? 0 : result, c.tag, error ? 5u : 0u};
    }
public:
    DramBench() {
        for (auto& value : memory) value = random();
        memory[0] = 0; memory[1] = UINT64_MAX; memory[2] = 1; memory[3] = uint64_t(1) << 63;
#ifndef VERILATOR
        cpphdl::hls::bind_external_memory(memory.data(), sizeof(memory), 0x1000);
#endif
        _assign();
        reset();
    }
    void _assign() {
#ifndef VERILATOR
        dut.valid_in = _ASSIGN(valid); dut.index_in = _ASSIGN(command.index);
        dut.count_in = _ASSIGN(command.count); dut.scale_in = _ASSIGN(command.scale);
        dut.tag_in = _ASSIGN(command.tag); dut.ready_in = _ASSIGN(ready);
        assignIf(dut, native_memory, dut.memory_out, native_memory.memory_in);
#endif
    }
    bool tick(bool resetting = false) {
#ifndef VERILATOR
        if (resetting) {
            dut._work(true); dut._strobe(); ++_system_clock; ++clocks;
            return false;
        }
#endif
        bool mem_ready = fast || (clocks - start > 25 && random() % 4 != 0);
        bool mem_valid = !pause_responses && !reads.empty() && reads.front().due <= clocks;
        uint64_t mem_data = mem_valid ? memory.at(reads.front().index) : 0;
        bool mem_error = mem_valid && reads.front().error;
#ifdef VERILATOR
        dut.clk = 0; dut.reset = resetting;
        dut.valid_in = valid; dut.index_in = command.index; dut.count_in = command.count;
        dut.scale_in = command.scale; dut.tag_in = command.tag; dut.ready_in = ready;
        dut.memory_out___05Fready_in = mem_ready;
        dut.memory_out___05Fvalid_in = mem_valid;
        dut.memory_out___05Fdata_in = mem_data;
        dut.memory_out___05Ferror_in = mem_error;
        dut.eval();
        bool input_fire = valid && dut.ready_out;
        bool output_valid = dut.valid_out;
        Answer actual{dut.data_out, dut.tag_out, dut.error_out};
        uint32_t fault = dut.fault_out;
        bool request_valid = dut.memory_out___05Fvalid_out;
        uint32_t address = dut.memory_out___05Faddr_out;
        bool request_fire = request_valid && mem_ready;
        bool response_fire = mem_valid && dut.memory_out___05Fready_out;
#else
        bool input_fire = valid && dut.ready_out();
        bool output_valid = dut.valid_out();
        Answer actual{dut.data_out(), dut.tag_out(), dut.error_out()};
        uint32_t fault = dut.fault_out();
#endif
        if (!resetting) {
            require(!fault || expect_error, "unexpected loader/calculation fault");
            if (input_fire) {
                answers.push_back(reference(command)); ++accepted;
                for (unsigned i = 0; i < command.count; ++i) ++requested.at(command.index + i);
            }
            if (held) require(output_valid && actual.data == held_answer.data && actual.tag == held_answer.tag &&
                actual.error == held_answer.error, "result changed during backpressure");
            held = output_valid && !ready; held_answer = actual;
            if (held) ++result_stalls;
            if (output_valid) {
                require(!answers.empty(), "unsolicited or duplicate calculation result");
                const auto& expected = answers.front();
                if (actual.data != expected.data || actual.tag != expected.tag || actual.error != expected.error) {
                    std::fprintf(stderr, "clock %llu tag %08x/%08x data %016llx/%016llx error %u/%u\n",
                        (unsigned long long)clocks, actual.tag, expected.tag,
                        (unsigned long long)actual.data, (unsigned long long)expected.data, actual.error, expected.error);
                    throw std::runtime_error("calculation, order or memory-error mismatch");
                }
                if (ready) {
                    answers.pop_front(); ++received; if (!reads.empty()) ++overlaps;
                    answer_run = last_answer + 1 == clocks ? answer_run + 1 : 1;
                    longest_answer_run = std::max(longest_answer_run, answer_run); last_answer = clocks;
                }
            }
#ifdef VERILATOR
            if (held_request) require(request_valid && address == held_address, "DDR request changed while stalled");
            held_request = request_valid && !mem_ready; held_address = address;
            if (held_request) ++request_stalls;
            if (request_valid) require(!dut.memory_out___05Fwrite_out && dut.memory_out___05Fsize_out == 8 &&
                dut.memory_out___05Fdata_out == 0, "bad read-only DDR request");
            if (response_fire) reads.pop_front();
            if (request_fire) {
                require(address >= 0x1000 && address < 0x3000 && address % 8 == 0, "invalid DDR address");
                uint32_t index = (address - 0x1000) / 8;
                require(requested.at(index) != 0, "duplicate or unrequested pointer read");
                --requested[index]; ++memory_reads;
                reads.push_back({index, clocks + (fast ? 4 : 1 + random() % 37), expect_error && index == 1000});
                max_outstanding = std::max(max_outstanding, unsigned(reads.size()));
                require(reads.size() <= 4, "controller outstanding-read bound exceeded");
                request_run = last_request + 1 == clocks ? request_run + 1 : 1;
                longest_request_run = std::max(longest_request_run, request_run); last_request = clocks;
            }
#endif
        }
#ifdef VERILATOR
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#else
        dut._work(resetting); dut._strobe();
#endif
        ++_system_clock; ++clocks;
        return !resetting && input_fire;
    }
    void reset() {
        valid = false; ready = true; held = false; held_request = false;
        answers.clear(); reads.clear(); requested.fill(0);
        received = accepted = memory_reads = overlaps = max_outstanding = 0;
        request_run = longest_request_run = 0; last_request = 0;
        answer_run = longest_answer_run = 0; last_answer = 0;
        request_stalls = result_stalls = 0; pause_responses = false;
        expect_error = false; start = clocks;
        tick(true); tick(true);
    }
    void run(unsigned number, bool quick, bool error = false) {
        reset(); fast = quick; expect_error = error;
        commands.seed(0xc012345);
        unsigned sent = 0, steps = 0;
        while (received < number && ++steps < 200000) {
            if (!valid && sent < number) {
                command = {uint32_t(commands() % 990), quick ? 1u : uint32_t(1 + commands() % 4),
                    uint32_t(commands()), uint32_t(commands())};
                if (sent < 4) command = {sent, 1u, UINT32_MAX - sent, 0xffff0000u + sent};
                if (sent == 4) command = {1023, 1, 0, 0};
                if (sent == 5 && !quick) command = {1020, 4, UINT32_MAX, 1};
                if (error && sent == number - 1) command = {1000, 1, 3, 0xffffffffu};
                valid = true;
            }
            ready = quick || (steps > 250 && steps % 313 > 35 && random() % 3 != 0);
            if (tick()) { ++sent; valid = false; }
        }
        require(received == number && accepted == number, "DDR stream timeout or lost command");
        ready = true;
        for (unsigned i = 0; i < 100; ++i) tick();
        require(answers.empty() && reads.empty(), "stream failed to drain");
#ifdef VERILATOR
        for (unsigned count : requested) require(count == 0, "pointer loop skipped a read");
        if (number > 10) {
            require(max_outstanding >= 2, "read requests did not overlap");
            if (!quick) {
                require(max_outstanding == 4, "four loader lanes were not exercised");
                require(overlaps != 0, "memory requests and calculation did not overlap");
                require(request_stalls && result_stalls, "backpressure coverage missing");
            }
            if (quick) {
                require(longest_request_run >= 2, "read-request stream has no consecutive transfers");
                require(longest_answer_run >= 2, "calculation pipeline has no consecutive answers");
            }
        }
#endif
        std::printf("PASS: %u blocks, %u DDR reads, max outstanding %u, overlap %u, consecutive requests/answers %u/%u, %u clocks%s\n",
            number, memory_reads, max_outstanding, overlaps, longest_request_run, longest_answer_run,
            steps, error ? " (injected error)" : "");
    }
    void cancel() {
        reset(); fast = false; ready = false; pause_responses = true;
        unsigned sent = 0;
        for (unsigned i = 0; i < 60; ++i) {
            command = {sent * 4, 4, 7, sent}; valid = sent < 4;
            if (tick()) ++sent;
        }
        require(sent == 4, "reset test did not admit all loaders");
#ifdef VERILATOR
        require(reads.size() == 4, "reset test did not leave four DDR reads pending");
#endif
        reset(); // The controller and DUT share reset; old DDR answers are cancelled.
        for (unsigned i = 0; i < 200; ++i) tick();
        require(received == 0, "reset leaked an old result");
        std::puts("PASS: reset cancels pending commands and controller responses");
    }
};

int main() {
    try {
        DramBench bench;
        bench.run(256, true);
        bench.run(1000, false);
        bench.cancel();
#ifdef VERILATOR
        bench.run(4, false, true);
#endif
        bench.run(128, false);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "DRAM stream: %s\n", error.what()); return 1;
    }
}

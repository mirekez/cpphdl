#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#if !defined(SYNTHESIS)
#include "cpphdl_vcd.h"
#endif
#include <print>

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

template<size_t WIDTH, size_t DEPTH>
class Buffer : public Module
{
    static_assert(WIDTH > 0, "Buffer WIDTH must be non-zero");
    static_assert(DEPTH > 0, "Buffer DEPTH must be non-zero");

    static constexpr size_t INDEX_BITS = DEPTH <= 1 ? 1 : clog2(DEPTH);
    static constexpr size_t COUNT_BITS = clog2(DEPTH + 1);

public:
    _PORT(bool)         valid_in;
    _PORT(logic<WIDTH>) data_in;
    _PORT(bool)         ready_out = _ASSIGN_COMB(ready_comb_func());

    _PORT(bool)         valid_out = _ASSIGN_COMB(valid_comb_func());
    _PORT(logic<WIDTH>) data_out = _ASSIGN_COMB(data_comb_func());
    _PORT(bool)         ready_in;

private:
    reg<array<logic<WIDTH>, DEPTH>> data_reg;
    reg<u<INDEX_BITS>> head_reg;
    reg<u<INDEX_BITS>> tail_reg;
    reg<u<COUNT_BITS>> count_reg;

    bool ready_comb;
    bool& ready_comb_func()
    {
        return ready_comb = ((uint32_t)count_reg < DEPTH) || ready_in();
    }

    bool valid_comb;
    bool& valid_comb_func()
    {
        return valid_comb = ((uint32_t)count_reg != 0) || valid_in();
    }

    logic<WIDTH> data_comb;
    logic<WIDTH>& data_comb_func()
    {
        if ((uint32_t)count_reg != 0) {
            return data_comb = data_reg[(uint32_t)head_reg];
        }
        return data_comb = data_in();
    }

public:
    void _work(bool reset)
    {
        bool input_fire;
        bool output_fire;
        bool had_stored;
        uint32_t head;
        uint32_t tail;
        uint32_t count;

        if (reset) {
            head_reg._next = 0;
            tail_reg._next = 0;
            count_reg._next = 0;
            return;
        }

        input_fire = valid_in() && ready_comb_func();
        output_fire = valid_comb_func() && ready_in();
        head = (uint32_t)head_reg;
        tail = (uint32_t)tail_reg;
        count = (uint32_t)count_reg;
        had_stored = count != 0;

        if (output_fire && count != 0) {
            head = (head + 1) % DEPTH;
            --count;
        }

        if (input_fire) {
            if (had_stored || !output_fire) {
                data_reg._next[tail] = data_in();
                tail = (tail + 1) % DEPTH;
                ++count;
            }
        }

        head_reg._next = u<INDEX_BITS>(head);
        tail_reg._next = u<INDEX_BITS>(tail);
        count_reg._next = u<COUNT_BITS>(count);
    }

    void _strobe()
    {
        data_reg.strobe();
        head_reg.strobe();
        tail_reg.strobe();
        count_reg.strobe();
    }

    void _assign()
    {
    }

#if !defined(SYNTHESIS)
    void add_vcd_signals(VcdFile& vcd, const std::string& prefix)
    {
        vcd.signals.push_back({prefix + "data_reg", WIDTH * DEPTH, &data_reg});
        vcd.signals.push_back({prefix + "head_reg", INDEX_BITS, &head_reg});
        vcd.signals.push_back({prefix + "tail_reg", INDEX_BITS, &tail_reg});
        vcd.signals.push_back({prefix + "count_reg", COUNT_BITS, &count_reg});
    }
#endif
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

template class Buffer<32, 1>;
template class Buffer<32, 2>;
template class Buffer<32, 4>;
template class Buffer<64, 8>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <deque>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

template<size_t WIDTH, size_t DEPTH>
class TestBuffer : public Module
{
    static constexpr long VCD_MAX_SAMPLES = 4096;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    Buffer<WIDTH, DEPTH> dut;
#endif

    reg<u1> valid_in_reg;
    reg<u1> ready_in_reg;
    reg<logic<WIDTH>> data_in_reg;

    std::deque<logic<WIDTH>> pending;
    uint64_t produced = 0;
    uint64_t consumed = 0;
    uint32_t prbs = 0x12345678u ^ (WIDTH << 8) ^ DEPTH;
    bool error = false;
    bool debugen_in = false;

    bool ready_out_value = false;
    bool valid_out_value = false;
    logic<WIDTH> data_out_value;
    VcdFile vcd;

    uint32_t next_prbs()
    {
        uint32_t bit = ((prbs >> 31) ^ (prbs >> 21) ^ (prbs >> 1) ^ prbs) & 1u;
        prbs = (prbs << 1) | bit;
        return prbs;
    }

    logic<WIDTH> make_word()
    {
        logic<WIDTH> word = 0;
        for (size_t bit = 0; bit < WIDTH; bit += 32) {
            size_t hi = bit + 31 < WIDTH ? bit + 31 : WIDTH - 1;
            word.bits(hi, bit) = next_prbs();
        }
        return word;
    }

    bool producer_valid(uint64_t cycle) const
    {
        return (cycle % 23) < 19 || (cycle % 97) == 0 || (cycle % 211) < 7;
    }

    bool consumer_ready(uint64_t cycle) const
    {
        return (cycle % 31) < 11 || (cycle % 89) > 70 || (cycle % 233) == 0;
    }

    void eval_dut(bool reset)
    {
#ifdef VERILATOR
        dut.valid_in = valid_in_reg;
        dut.ready_in = ready_in_reg;
        memcpy(&dut.data_in, &data_in_reg, sizeof(dut.data_in));
        dut.reset = reset;
        dut.eval();
        ready_out_value = dut.ready_out;
        valid_out_value = dut.valid_out;
        data_out_value = *(logic<WIDTH>*)&dut.data_out;
#else
        dut._work(reset);
        ready_out_value = dut.ready_out();
        valid_out_value = dut.valid_out();
        data_out_value = dut.data_out();
#endif
    }

public:
    explicit TestBuffer(bool debug)
    {
        debugen_in = debug;
    }

    void _assign()
    {
#ifndef VERILATOR
        dut.valid_in = _ASSIGN_REG(valid_in_reg);
        dut.ready_in = _ASSIGN_REG(ready_in_reg);
        dut.data_in = _ASSIGN_REG(data_in_reg);
        dut.__inst_name = __inst_name + "/buffer";
        dut._assign();
#endif
    }

    void _work(bool reset)
    {
        if (reset) {
            valid_in_reg.clr();
            ready_in_reg.clr();
            data_in_reg.clr();
            pending.clear();
            produced = 0;
            consumed = 0;
            prbs = 0x12345678u ^ (WIDTH << 8) ^ DEPTH;
        }

#ifdef VERILATOR
        dut.clk = 0;
#endif
        eval_dut(reset);

        if (reset) {
            return;
        }

        bool input_fire = valid_in_reg && ready_out_value;
        bool output_fire = valid_out_value && ready_in_reg;

        if (input_fire) {
            pending.push_back(data_in_reg);
            ++produced;
        }

        if (output_fire) {
            if (pending.empty()) {
                std::print("{:s} ERROR: output handshake with empty reference queue at cycle {}\n",
                    __inst_name, sys_clock);
                error = true;
            }
            else if (data_out_value != pending.front()) {
                std::print("{:s} ERROR: data mismatch at cycle {}: got {} expected {}\n",
                    __inst_name, sys_clock, data_out_value, pending.front());
                error = true;
            }
            else {
                pending.pop_front();
                ++consumed;
            }
        }

        if (pending.size() > DEPTH) {
            std::print("{:s} ERROR: reference queue exceeded DEPTH at cycle {}: {}\n",
                __inst_name, sys_clock, pending.size());
            error = true;
        }

        valid_in_reg._next = producer_valid(sys_clock) && produced < 20000;
        ready_in_reg._next = consumer_ready(sys_clock);
        data_in_reg._next = make_word();

        if (debugen_in && (sys_clock % 1000) == 0) {
            std::print("{:s}: cycle={} produced={} consumed={} pending={} valid={} ready={}\n",
                __inst_name, sys_clock, produced, consumed, pending.size(),
                (bool)valid_in_reg, (bool)ready_in_reg);
        }
    }

    void _strobe()
    {
#ifndef VERILATOR
        dut._strobe();
#endif
        valid_in_reg.strobe();
        ready_in_reg.strobe();
        data_in_reg.strobe();
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        dut.clk = 1;
        eval_dut(reset);
#endif
    }

    void _strobe_neg()
    {
    }

    void setup_vcd()
    {
        vcd.signals.clear();
        vcd.signals.push_back({"valid_in_reg", 1, &valid_in_reg});
        vcd.signals.push_back({"ready_in_reg", 1, &ready_in_reg});
        vcd.signals.push_back({"data_in_reg", WIDTH, &data_in_reg});
        vcd.signals.push_back({"ready_out", 1, &ready_out_value});
        vcd.signals.push_back({"valid_out", 1, &valid_out_value});
        vcd.signals.push_back({"data_out", WIDTH, &data_out_value});
#ifndef VERILATOR
        dut.add_vcd_signals(vcd, "dut.");
#endif
        vcd.create("output.vcd");
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestBuffer<WIDTH={},DEPTH={}>...", WIDTH, DEPTH);
#else
        std::print("CppHDL TestBuffer<WIDTH={},DEPTH={}>...", WIDTH, DEPTH);
#endif
        if (debugen_in) {
            std::print("\n");
        }

        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "buffer_test";
        _assign();
        _work(true);
        _work_neg(true);
        setup_vcd();
        vcd.sample(0);

        int cycles = 80000;
        while (--cycles) {
            _strobe();
            ++sys_clock;
            _work(false);
            _strobe_neg();
            _work_neg(false);
            if (sys_clock <= VCD_MAX_SAMPLES) {
                vcd.sample(sys_clock);
            }

            if (produced >= 20000 && pending.empty()) {
                break;
            }
            if (error) {
                break;
            }
        }

        if (produced < 20000 || !pending.empty()) {
            std::print("{:s} ERROR: test did not drain: produced={} consumed={} pending={}\n",
                __inst_name, produced, consumed, pending.size());
            error = true;
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        else if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        else {
            positional.emplace_back(argv[i]);
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "Buffer", {"Predef_pkg"}, {"../../../../include"}, 32, 1);
        ok &= VerilatorCompile(__FILE__, "Buffer", {"Predef_pkg"}, {"../../../../include"}, 32, 2);
        ok &= VerilatorCompile(__FILE__, "Buffer", {"Predef_pkg"}, {"../../../../include"}, 32, 4);
        ok &= VerilatorCompile(__FILE__, "Buffer", {"Predef_pkg"}, {"../../../../include"}, 64, 8);
        auto compile_us = (std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count();
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok
            && std::system((std::string("Buffer_32_1/obj_dir/VBuffer 32 1") + (debug ? " --debug" : "")).c_str()) == 0
            && std::system((std::string("Buffer_32_2/obj_dir/VBuffer 32 2") + (debug ? " --debug" : "")).c_str()) == 0
            && std::system((std::string("Buffer_32_4/obj_dir/VBuffer 32 4") + (debug ? " --debug" : "")).c_str()) == 0
            && std::system((std::string("Buffer_64_8/obj_dir/VBuffer 64 8") + (debug ? " --debug" : "")).c_str()) == 0;
        std::cout << "Verilator compilation time: " << compile_us / 4 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    if (positional.size() >= 2) {
        size_t width = std::stoull(positional[0]);
        size_t depth = std::stoull(positional[1]);
        if (width == 32 && depth == 1) {
            return !(ok && TestBuffer<32, 1>(debug).run());
        }
        if (width == 32 && depth == 2) {
            return !(ok && TestBuffer<32, 2>(debug).run());
        }
        if (width == 32 && depth == 4) {
            return !(ok && TestBuffer<32, 4>(debug).run());
        }
        if (width == 64 && depth == 8) {
            return !(ok && TestBuffer<64, 8>(debug).run());
        }
        std::print("Unsupported Buffer test parameters: WIDTH={} DEPTH={}\n", width, depth);
        return 1;
    }

    ok = ok && TestBuffer<32, 1>(debug).run();
    ok = ok && TestBuffer<32, 2>(debug).run();
    ok = ok && TestBuffer<32, 4>(debug).run();
    ok = ok && TestBuffer<64, 8>(debug).run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

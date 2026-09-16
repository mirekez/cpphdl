#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

// Transpose SIZE rows into SIZE columns, low lane first. A valid start beat
// arms the next row; its own data is not part of the new matrix. data_valid_in
// is a pipeline enable for BOTH directions (there is no ready port). The first
// column appears on the next enabled cycle after the final input row. Bubbles
// suppress data_valid_out and hold the stream position. A start coincident
// with the final row can arm the next matrix without interrupting that row.
// Reset takes effect RESET_DELAY cycles after it is sampled, even if stalled.
template<size_t SIZE = 4, size_t WIDTH = 16, size_t RESET_DELAY = 1>
class Transposer : public Module
{
    static_assert(SIZE > 1, "Transposer SIZE must be greater than 1");
    static_assert(WIDTH > 0, "Transposer WIDTH must be positive");
    static_assert(RESET_DELAY > 0, "Transposer reset delay must be positive");

public:
    _PORT(bool) start_in;
    _PORT(array<SIZE, logic<WIDTH>>) data_in;
    _PORT(bool) data_valid_in;
    _PORT(array<SIZE, logic<WIDTH>>) data_out;
    _PORT(bool) data_valid_out;

private:
    reg<array<SIZE, logic<WIDTH>>> mesh1[SIZE];
    reg<array<SIZE, logic<WIDTH>>> mesh2[SIZE];
    reg<u1> valid1[SIZE];
    reg<u1> valid2[SIZE];
    reg<u1> busy1;
    reg<u1> busy2;
    reg<u1> swap;
    reg<u<clog2(SIZE + 1)>> rows_written;
    reg<u<clog2(SIZE + 1)>> rows_to_read;
    reg<u1> data_valid_in_delayed;
    reg<logic<RESET_DELAY>> reset1;
    bool data_valid_out_comb;

    bool& data_valid_out_comb_func()
    {
        return data_valid_out_comb = data_valid_in_delayed && valid2[SIZE - 1];
    }

public:
    void _assign()
    {
        data_out = _ASSIGN_REG(mesh2[SIZE - 1]);
        data_valid_out = _ASSIGN_COMB(data_valid_out_comb_func());
    }

    void _work(bool reset)
    {
        size_t i, j;
        data_valid_in_delayed._next = data_valid_in();
        if (data_valid_in()) {
            valid1[0]._next = 0;
            if (busy1) {
                for (i = 0; i < SIZE; ++i) {
                    mesh1[0]._next[i] = data_in()[i];
                }
                valid1[0]._next = 1;
                rows_written._next = rows_written + 1;
                if (rows_written._next == SIZE) {
                    busy1._next = 0;
                    busy2._next = 1;
                    rows_written._next = 0;
                    rows_to_read._next = SIZE;
                    swap._next = 1;
                }
            }
            if (start_in()) {
                busy1._next = 1;
                rows_written._next = 0;
            }
            valid2[0]._next = 0;
            for (j = 0; j < SIZE - 1; ++j) {
                for (i = 0; i < SIZE; ++i) {
                    mesh1[j + 1]._next[i] = mesh1[j][i];
                }
                valid1[j + 1]._next = valid1[j];
            }
            for (i = 0; i < SIZE - 1; ++i) {
                for (j = 0; j < SIZE; ++j) {
                    mesh2[i + 1]._next[j] = mesh2[i][j];
                }
                valid2[i + 1]._next = valid2[i];
            }
            if (!valid2[SIZE - 2]) {
                for (j = 0; j < SIZE; ++j) {
                    mesh2[SIZE - 1]._next[j] = 0;
                }
            }
            if (swap) {
                for (i = 0; i < SIZE; ++i) {
                    valid2[i]._next = 1;
                }
                swap._next = 0;
            }
            else if (busy2) {
                rows_to_read._next = rows_to_read - 1;
                if (rows_to_read._next == 0) {
                    busy2._next = 0;
                }
            }
        }
        // The pending transpose may be copied during a stall; it is consumed
        // only on the next enabled cycle, when valid2 is loaded above.
        if (swap) {
            for (i = 0; i < SIZE; ++i) {
                for (j = 0; j < SIZE; ++j) {
                    mesh2[SIZE - 1 - i]._next[SIZE - 1 - j] = mesh1[j][i];
                }
            }
        }
        reset1._next = (reset1 >> 1) | (logic<RESET_DELAY>(reset) << (RESET_DELAY - 1));
        if (reset1 & 1) {
            busy1._next = 0;
            busy2._next = 0;
            swap._next = 0;
            rows_written._next = 0;
            rows_to_read._next = 0;
            data_valid_in_delayed._next = 0;
            for (i = 0; i < SIZE; ++i) {
                valid1[i]._next = 0;
                valid2[i]._next = 0;
                mesh2[i]._next = 0;
            }
        }
    }

    void _strobe()
    {
        size_t i;
        reset1.strobe();
        for (i = 0; i < SIZE; ++i) {
            valid1[i].strobe();
            valid2[i].strobe();
            mesh1[i].strobe();
            mesh2[i].strobe();
        }
        busy1.strobe();
        busy2.strobe();
        swap.strobe();
        rows_written.strobe();
        rows_to_read.strobe();
        data_valid_in_delayed.strobe();
    }
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)
#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "../tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

// Access Verilator's scalar (<=64 bits) and word-array (>64 bits) ports
// explicitly, including non-byte-aligned lane widths and high bus words.
#ifdef VERILATOR
template<typename Port>
static void write_bit(Port& port, size_t bit, bool value)
{
    if constexpr (std::is_integral_v<Port>) {
        const Port mask = Port(1) << bit;
        port = (port & ~mask) | (value ? mask : 0);
    }
    else {
        const uint32_t mask = uint32_t(1) << (bit % 32);
        port[bit / 32] = (port[bit / 32] & ~mask) | (value ? mask : 0);
    }
}

template<typename Port>
static bool read_bit(const Port& port, size_t bit)
{
    if constexpr (std::is_integral_v<Port>) {
        return (port >> bit) & 1;
    }
    else {
        return (port[bit / 32] >> (bit % 32)) & 1;
    }
}
#endif

template<size_t SIZE, size_t WIDTH, size_t DELAY>
class TestTransposer : public Module
{
    using Row = std::array<uint32_t, SIZE>;
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    Transposer<SIZE, WIDTH, DELAY> dut;
    array<SIZE, logic<WIDTH>> input;
    bool start = false;
    bool enable = false;
#endif
    // Independent matrix/queue oracle: no mesh shift registers or DUT state.
    std::vector<Row> rows;
    std::deque<Row> pending;
    std::deque<Row> output;
    std::deque<bool> resets = std::deque<bool>(DELAY, false);
    bool collecting = false;
    bool have_previous = false;
    Row previous{};
    uint32_t random = 0x5eeda11u;
    size_t cycle_count = 0;
    size_t checked_columns = 0;

    uint32_t next_random()
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    }

    Row random_row()
    {
        Row row{};
        for (auto& lane : row) {
            lane = next_random() & ((uint32_t(1) << WIDTH) - 1);
        }
        return row;
    }

    void fail(const std::string& message)
    {
        throw std::runtime_error("Transposer<" + std::to_string(SIZE) + ","
            + std::to_string(WIDTH) + ","
            + std::to_string(DELAY) + "> cycle " + std::to_string(cycle_count)
            + ": " + message);
    }

    void cycle(bool start_value, bool enabled, const Row& data, bool reset = false)
    {
        const bool resetting = resets.front();
        const bool copying = !pending.empty();
        bool expected_valid = false;
        Row expected{};
        Row actual{};
        bool actual_valid;
        resets.pop_front();
        resets.push_back(reset);
        ++cycle_count;
        ++_system_clock;

        if (enabled) {
            if (!pending.empty()) {
                if (!output.empty()) {
                    fail("test stimulus overwrote unread columns");
                }
                output.swap(pending);
            }
            if (!output.empty()) {
                expected_valid = true;
                expected = output.front();
                output.pop_front();
            }
            if (collecting) {
                rows.push_back(data);
                if (rows.size() == SIZE) {
                    for (size_t column = 0; column < SIZE; ++column) {
                        Row transposed{};
                        for (size_t row = 0; row < SIZE; ++row) {
                            transposed[row] = rows[row][column];
                        }
                        pending.push_back(transposed);
                    }
                    rows.clear();
                    collecting = false;
                }
            }
            if (start_value) {
                rows.clear();
                collecting = true;
            }
        }
        if (resetting) {
            rows.clear();
            pending.clear();
            output.clear();
            collecting = false;
            expected_valid = false;
        }

#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.start_in = start_value;
        dut.data_valid_in = enabled;
        for (size_t lane = 0; lane < SIZE; ++lane) {
            for (size_t bit = 0; bit < WIDTH; ++bit) {
                write_bit(dut.data_in, lane * WIDTH + bit, (data[lane] >> bit) & 1);
            }
        }
        dut.eval();
        dut.clk = 1;
        dut.eval();
        dut.clk = 0;
        dut.eval();
        actual_valid = dut.data_valid_out;
        for (size_t lane = 0; lane < SIZE; ++lane) {
            for (size_t bit = 0; bit < WIDTH; ++bit) {
                actual[lane] |= uint32_t(read_bit(dut.data_out, lane * WIDTH + bit)) << bit;
            }
        }
#else
        start = start_value;
        enable = enabled;
        for (size_t lane = 0; lane < SIZE; ++lane) {
            input[lane] = data[lane];
        }
        dut._work(reset);
        _strobe();
        actual_valid = dut.data_valid_out();
        for (size_t lane = 0; lane < SIZE; ++lane) {
            actual[lane] = uint64_t(dut.data_out()[lane]);
        }
#endif
        if (actual_valid != expected_valid) {
            fail("valid expected " + std::to_string(expected_valid)
                + " got " + std::to_string(actual_valid));
        }
        if (expected_valid) {
            ++checked_columns;
            for (size_t lane = 0; lane < SIZE; ++lane) {
                if (actual[lane] != expected[lane]) {
                    fail("lane " + std::to_string(lane) + " expected "
                        + std::to_string(expected[lane]) + " got " + std::to_string(actual[lane]));
                }
            }
        }
        if (have_previous && !enabled && !copying && !resetting && actual != previous) {
            fail("data_out changed during a stalled cycle");
        }
        previous = actual;
        have_previous = true;
    }

    void tick(bool start_value = false, bool enabled = true, bool reset = false)
    {
        cycle(start_value, enabled, random_row(), reset);
    }

    void drain(bool stalls = false)
    {
        for (size_t i = 0; i < SIZE + 2; ++i) {
            if (stalls) {
                tick(true, false);
                tick(false, false);
            }
            tick();
        }
    }

    void reset_pipeline()
    {
        // Exercise the delayed reset without relying on Verilator power-on state.
        tick(false, false, true);
        for (size_t i = 0; i <= DELAY; ++i) {
            tick(false, false);
        }
    }

    void matrix(bool stalls)
    {
        tick(true);
        for (size_t row = 0; row < SIZE; ++row) {
            if (stalls) {
                for (size_t i = 0, count = next_random() % 4; i < count; ++i) {
                    tick(true, false);
                }
            }
            tick();
        }
        drain(stalls);
    }

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.start_in = _ASSIGN_REG(start);
        dut.data_valid_in = _ASSIGN_REG(enable);
        dut.data_in = _ASSIGN_REG(input);
        dut._assign();
#endif
    }

    void _strobe()
    {
#ifndef VERILATOR
        dut._strobe();
#endif
    }

    void run()
    {
        _assign();
        reset_pipeline();
        // Unarmed input, and start asserted only while disabled, produce nothing.
        tick(true, false);
        drain();
        if (checked_columns != 0) {
            fail("disabled start armed a matrix");
        }
        // Asymmetric coordinate pattern catches row/column reversals.
        tick(true);
        for (size_t row = 0; row < SIZE; ++row) {
            Row data{};
            for (size_t lane = 0; lane < SIZE; ++lane) {
                data[lane] = (row * SIZE + lane + 1) & ((uint32_t(1) << WIDTH) - 1);
            }
            cycle(false, true, data);
        }
        drain(true);
        for (size_t i = 0; i < 32; ++i) {
            matrix(i % 2 != 0);
        }
        // Start on the previous matrix's final row: continuous output and input.
        tick(true);
        for (size_t matrix_index = 0; matrix_index < 16; ++matrix_index) {
            for (size_t row = 0; row < SIZE; ++row) {
                tick(row == SIZE - 1 && matrix_index != 15);
            }
        }
        drain();
        if (checked_columns != 49 * SIZE) {
            fail("wrong number of completed matrix columns");
        }
        // Restart discards a partial matrix after accepting the coincident row.
        tick(true);
        tick(true);
        for (size_t row = 0; row < SIZE; ++row) {
            tick();
        }
        drain();
        // Reset during capture, pending swap, and active output, then recover.
        tick(true);
        tick();
        reset_pipeline();
        drain();
        matrix(true);
        tick(true);
        for (size_t row = 0; row < SIZE; ++row) {
            tick();
        }
        reset_pipeline();
        drain();
        matrix(false);
        tick(true);
        for (size_t row = 0; row < SIZE; ++row) {
            tick();
        }
        tick();
        // Keep streaming for the full reset latency to verify the exact edge.
        tick(false, true, true);
        for (size_t i = 0; i <= DELAY; ++i) {
            tick();
        }
        drain();
        matrix(true);
        std::cout << "Transposer<" << SIZE << ',' << WIDTH << ',' << DELAY
                  << "> passed: " << cycle_count << " cycles, " << checked_columns
                  << " checked columns\n";
    }
};

int main(int argc, char** argv)
{
    bool native = true;
    bool rtl = true;
    std::vector<std::string> positional;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--noveril") rtl = false;
            else if (arg == "--verilator-only") native = false;
            else positional.push_back(arg);
        }
#ifdef VERILATOR
        Verilated::commandArgs(argc, argv);
        if (positional == std::vector<std::string>{"2", "7", "1"})
            TestTransposer<2, 7, 1>().run();
        else if (positional == std::vector<std::string>{"3", "9", "3"})
            TestTransposer<3, 9, 3>().run();
        else if (positional == std::vector<std::string>{"4", "16", "1"})
            TestTransposer<4, 16, 1>().run();
        else if (positional == std::vector<std::string>{"8", "16", "3"})
            TestTransposer<8, 16, 3>().run();
        else throw std::runtime_error("unsupported Transposer test configuration");
#else
        if (!positional.empty()) throw std::runtime_error("usage: math_Transposer [--noveril|--verilator-only]");
        if (native) {
            TestTransposer<2, 7, 1>().run();
            TestTransposer<3, 9, 3>().run();
            TestTransposer<4, 16, 1>().run();
            TestTransposer<8, 16, 3>().run();
        }
        if (rtl) {
            const std::string include = (CpphdlSourceRootFrom(__FILE__) / "include").string();
            for (const auto& config : std::vector<std::array<size_t, 3>>{
                    {2, 7, 1}, {3, 9, 3}, {4, 16, 1}, {8, 16, 3}}) {
                if (!VerilatorCompile(__FILE__, "Transposer", {"Predef_pkg"}, {include},
                        config[0], config[1], config[2])) {
                    throw std::runtime_error("Transposer Verilator build failed");
                }
                std::string folder = "Transposer";
                std::string args;
                for (auto parameter : config) {
                    folder += "_" + std::to_string(parameter);
                    args += " " + std::to_string(parameter);
                }
                if (SystemEcho((folder + "/obj_dir/VTransposer" + args).c_str()) != 0) {
                    throw std::runtime_error("Transposer RTL simulation failed");
                }
            }
        }
#endif
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

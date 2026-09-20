#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

// One asynchronous-read lane. Use a direct clocked write in RTL: wrapping the
// memory write in the converter's task prevents Vivado from inferring RAM.
// Only this storage primitive is replaced; C++ and RTL share the controller
// and routing, and the same tests check both memory implementations.
template<size_t SIZE, size_t WIDTH>
class [[clang::annotate(R"(CPPHDL_REPLACEMENT=
`default_nettype none
module TransposerLane #(
    parameter SIZE = 4,
    parameter WIDTH = 16
) (
    input wire clk,
    input wire reset,
    input wire write_enable_in,
    input wire [$clog2(2*SIZE)-1:0] write_address_in,
    input wire [$clog2(2*SIZE)-1:0] read_address_in,
    input wire [WIDTH-1:0] write_data_in,
    output wire [WIDTH-1:0] read_data_out
);
    // Quartus and Vivado use different attribute names for LUT-based RAM.
    // MLAB flow-through reads cannot promise the RTL's collision result.
    // The bank controller never publishes a read of the address written at
    // that edge; unused collision data may therefore be arbitrary. Keep this
    // contract covered by the collision-poisoning Transposer regression.
`ifdef ALTERA_RESERVED_QIS
    (* ramstyle = "MLAB, no_rw_check" *)
`else
    (* ram_style = "distributed" *)
`endif
    reg [WIDTH-1:0] storage [0:2*SIZE-1];
    always @(posedge clk) begin
        if (write_enable_in)
            storage[write_address_in] <= write_data_in;
    end
    assign read_data_out = storage[read_address_in];
endmodule
;)")]] TransposerLane : public Module
{
public:
    _PORT(bool) write_enable_in;
    _PORT(u<clog2(2 * SIZE)>) write_address_in;
    _PORT(u<clog2(2 * SIZE)>) read_address_in;
    _PORT(logic<WIDTH>) write_data_in;
    _PORT(logic<WIDTH>) read_data_out;

private:
    memory<logic<WIDTH>, 1, 2 * SIZE> storage;
    logic<WIDTH> read_data_comb;

    logic<WIDTH>& read_data_comb_func()
    {
        return read_data_comb = storage[read_address_in()];
    }

public:
    void _assign()
    {
        read_data_out = _ASSIGN_COMB(read_data_comb_func());
    }

    void _work(bool reset)
    {
        if (write_enable_in()) {
            storage[write_address_in()] = write_data_in();
        }
    }

    void _strobe()
    {
        storage.apply();
    }
};

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
    // Skew the matrix across SIZE single-write/single-read memory lanes.
    // Cell (row, column) lives in lane (row + column) modulo SIZE, so each
    // column and each row touches every lane exactly once. The address selects
    // column and matrix bank, so stored data never shifts or copies at handoff.
    TransposerLane<SIZE, WIDTH> storage[SIZE];
    reg<u1> write_bank;
    reg<u1> read_bank;
    reg<u<clog2(SIZE)>> write_column;
    reg<u<clog2(SIZE)>> read_row;
    reg<u1> collecting;
    reg<u1> reading;
    reg<u1> pending;
    reg<u1> output_valid;
    reg<logic<RESET_DELAY>> reset1;
    array<SIZE, logic<WIDTH>> data_out_comb;
    array<SIZE, logic<WIDTH>> write_data_comb;
    logic<2 * SIZE * WIDTH> write_bus_comb;
    logic<2 * SIZE * WIDTH> read_bus_comb;
    array<SIZE, u<clog2(2 * SIZE)>> read_address_comb;
    u<clog2(2 * SIZE)> write_address_comb;

    u<clog2(2 * SIZE)>& write_address_comb_func()
    {
        size_t column;
        column = write_column;
        return write_address_comb = size_t(write_bank) * SIZE + column;
    }

    array<SIZE, logic<WIDTH>>& write_data_comb_func()
    {
        size_t i, column;
        column = write_column;
        // Pack explicitly: native array elements may have byte padding, but
        // the routing bus must contain exactly WIDTH bits per lane.
        write_bus_comb = 0;
        for (i = 0; i < SIZE; ++i) {
            write_bus_comb.bits((i + 1) * WIDTH - 1, i * WIDTH) = data_in()[i];
        }
        // Combinational rotation routes a vector to the memory lanes; no
        // stored word moves. Duplicate wires and select one window, avoiding
        // two opposing shifts and an OR for each barrel rotation.
        write_bus_comb = (write_bus_comb << (SIZE * WIDTH)) | write_bus_comb;
        write_bus_comb = (write_bus_comb << (column * WIDTH)) >> (SIZE * WIDTH);
        for (i = 0; i < SIZE; ++i) {
            write_data_comb[i] = write_bus_comb.bits((i + 1) * WIDTH - 1, i * WIDTH);
        }
        return write_data_comb;
    }

    array<SIZE, u<clog2(2 * SIZE)>>& read_address_comb_func()
    {
        size_t i, column, row;
        row = read_row;
        for (i = 0; i < SIZE; ++i) {
            column = (i + SIZE - row) % SIZE;
            read_address_comb[i] = size_t(read_bank) * SIZE + column;
        }
        return read_address_comb;
    }

    array<SIZE, logic<WIDTH>>& data_out_comb_func()
    {
        size_t i, row;
        row = read_row;
        read_bus_comb = 0;
        for (i = 0; i < SIZE; ++i) {
            read_bus_comb.bits((i + 1) * WIDTH - 1, i * WIDTH) = storage[i].read_data_out();
        }
        read_bus_comb = (read_bus_comb << (SIZE * WIDTH)) | read_bus_comb;
        read_bus_comb = read_bus_comb >> (row * WIDTH);
        data_out_comb = 0;
        if (reading) {
            for (i = 0; i < SIZE; ++i) {
                data_out_comb[i] = read_bus_comb.bits((i + 1) * WIDTH - 1, i * WIDTH);
            }
        }
        return data_out_comb;
    }

public:
    void _assign()
    {
        size_t i;
        data_out = _ASSIGN_COMB(data_out_comb_func());
        data_valid_out = _ASSIGN_REG(output_valid);
        for (i = 0; i < SIZE; ++i) {
            storage[i].write_enable_in = _ASSIGN(data_valid_in() && collecting && !(reset1 & 1));
            storage[i].write_address_in = _ASSIGN_COMB(write_address_comb_func());
            storage[i].read_address_in = _ASSIGN_COMB_I(read_address_comb_func()[i]);
            storage[i].write_data_in = _ASSIGN_COMB_I(write_data_comb_func()[i]);
            storage[i]._assign();
        }
    }

    void _work(bool reset)
    {
        size_t i;
        output_valid._next = 0;
        if (data_valid_in() && !(reset1 & 1)) {
            // Present row zero on the first enabled cycle after completion.
            // The writer has already moved to the opposite bank. Stalls hold
            // the read selector, including after the last valid output row.
            if (pending) {
                read_bank._next = !write_bank;
                read_row._next = 0;
                reading._next = 1;
                output_valid._next = 1;
                pending._next = 0;
            }
            else if (reading) {
                if (read_row == SIZE - 1) {
                    reading._next = 0;
                }
                else {
                    read_row._next = read_row + 1;
                    output_valid._next = 1;
                }
            }
            if (collecting) {
                if (write_column == SIZE - 1) {
                    collecting._next = 0;
                    write_column._next = 0;
                    write_bank._next = !write_bank;
                    pending._next = 1;
                }
                else {
                    write_column._next = write_column + 1;
                }
            }
            if (start_in()) {
                collecting._next = 1;
                write_column._next = 0;
            }
        }
        reset1._next = (reset1 >> 1) | (logic<RESET_DELAY>(reset) << (RESET_DELAY - 1));
        if (reset1 & 1) {
            collecting._next = 0;
            reading._next = 0;
            pending._next = 0;
            write_bank._next = 0;
            read_bank._next = 0;
            write_column._next = 0;
            read_row._next = 0;
            output_valid._next = 0;
            // Unread data is masked by reading. A complete matrix overwrites
            // every cell before publication, so the RAM contents need no reset.
        }
        for (i = 0; i < SIZE; ++i) {
            storage[i]._work(reset);
        }
    }

    void _strobe()
    {
        size_t i;
        reset1.strobe();
        for (i = 0; i < SIZE; ++i) {
            storage[i]._strobe();
        }
        write_bank.strobe();
        read_bank.strobe();
        write_column.strobe();
        read_row.strobe();
        collecting.strobe();
        reading.strobe();
        pending.strobe();
        output_valid.strobe();
    }
};

#if defined(SYNTHESIS)
// Elaborate dependent memory-lane ports; numeric parameters remain symbolic
// in the generated Transposer and TransposerLane modules.
template class Transposer<>;
#endif

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
    static_assert(WIDTH > 0 && WIDTH <= 32);
    static constexpr uint32_t lane_mask = UINT32_MAX >> (32 - WIDTH);
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
            lane = next_random() & lane_mask;
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
        if (resetting && actual != Row{}) {
            fail("reset did not mask the stored matrix");
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
        if (have_previous && !enabled && !resetting && actual != previous) {
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
                data[lane] = (row * SIZE + lane + 1) & lane_mask;
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
        // Reuse both banks under simultaneous capture/drain, stalling at
        // every selector position and at the bank handoff. Disabled starts
        // must not disturb either selector or the published output vector.
        tick(true);
        for (size_t matrix_index = 0; matrix_index < 17; ++matrix_index) {
            for (size_t row = 0; row < SIZE; ++row) {
                tick(true, false);
                tick(false, false);
                tick(row == SIZE - 1 && matrix_index != 16);
            }
        }
        drain(true);
        if (checked_columns != 66 * SIZE) {
            fail("wrong number of bank-handoff columns");
        }
        // Restart discards a partial matrix after accepting the coincident row.
        tick(true);
        tick(true);
        for (size_t row = 0; row < SIZE; ++row) {
            tick();
        }
        drain();
        // Restart capture in the other bank while a completed matrix drains.
        // Its data must remain intact while the partial write column wraps.
        tick(true);
        for (size_t row = 0; row < SIZE; ++row) {
            tick(row == SIZE - 1);
        }
        tick(true);
        for (size_t row = 0; row < SIZE; ++row) {
            tick(true, false);
            tick();
        }
        drain(true);
        // Reset during capture, pending bank handoff, and active output, then
        // recover. Neither stale nor partially overwritten banks may escape.
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
        else if (positional == std::vector<std::string>{"32", "32", "1"})
            TestTransposer<32, 32, 1>().run();
        else throw std::runtime_error("unsupported Transposer test configuration");
#else
        if (!positional.empty()) throw std::runtime_error("usage: math_Transposer [--noveril|--verilator-only]");
        if (native) {
            TestTransposer<2, 7, 1>().run();
            TestTransposer<3, 9, 3>().run();
            TestTransposer<4, 16, 1>().run();
            TestTransposer<8, 16, 3>().run();
            TestTransposer<32, 32, 1>().run();
        }
        if (rtl) {
            const std::string include = (CpphdlSourceRootFrom(__FILE__) / "include").string();
            for (const auto& config : std::vector<std::array<size_t, 3>>{
                    {2, 7, 1}, {3, 9, 3}, {4, 16, 1}, {8, 16, 3}, {32, 32, 1}}) {
                if (!VerilatorCompile(__FILE__, "Transposer", {"Predef_pkg", "TransposerLane"}, {include},
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

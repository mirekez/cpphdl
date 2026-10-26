#include "../MatrixTransfer.h"

#ifndef SYNTHESIS
#include <array>
#include <cstdio>
#include <deque>
#include <random>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#include "Vcpphdl_hls_MatrixTransfer.h"
#endif

long _system_clock = 0;

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static uint32_t from_half(uint16_t value)
{
    uint32_t sign = uint32_t(value & 0x8000) << 16;
    uint32_t exponent = (value >> 10) & 31;
    uint32_t fraction = value & 1023;
    if (exponent == 31) return sign | 0x7f800000u | (fraction << 13);
    if (exponent) return sign | ((exponent + 112) << 23) | (fraction << 13);
    if (!fraction) return sign;
    exponent = 113;
    while (!(fraction & 1024)) { fraction <<= 1; --exponent; }
    return sign | (exponent << 23) | ((fraction & 1023) << 13);
}

struct Request {
    uint32_t address;
    uint64_t mask;
    std::array<uint32_t, 16> data;
    bool read, write;
    bool operator==(const Request& other) const {
        return address == other.address && read == other.read && write == other.write &&
               (!write || (mask == other.mask && data == other.data));
    }
};

class Test {
#ifdef VERILATOR
    Vcpphdl_hls_MatrixTransfer dut;
#else
    cpphdl::hls::MatrixTransfer dut;
#endif
    bool command_valid = false, response_ready = false, half = false;
    uint32_t source = 128, destination = 131072, rows = 1, columns = 1;
    uint32_t sr = 4, sc = 4, dr = 4, dc = 4;
    bool wait = false, valid = false, read_error = false;
    cpphdl::hls::MatrixTransfer::Beat data{};
    std::mt19937 random{321};
    std::vector<uint8_t> memory = std::vector<uint8_t>(262144, 0xa5);
    struct Pending { unsigned due; uint32_t address; };
    std::deque<Pending> pending;
    unsigned time = 0;
    bool held = false;
    Request previous{};

    void drive(bool reset) {
#ifdef VERILATOR
        dut.clk = 0; dut.reset = reset;
        dut.command_valid_in = command_valid; dut.response_ready_in = response_ready;
        dut.source_in = source; dut.destination_in = destination;
        dut.rows_in = rows; dut.columns_in = columns;
        dut.source_row_stride_in = sr; dut.source_column_stride_in = sc;
        dut.destination_row_stride_in = dr; dut.destination_column_stride_in = dc;
        dut.half_in = half; dut.m_waitrequest_in = wait;
        dut.m_readdatavalid_in = valid; dut.m_readdataerror_in = read_error;
        for (unsigned i = 0; i < 16; ++i) dut.m_readdata_in[i] = uint32_t(data[i]);
        dut.eval();
#endif
    }

    Request request() {
        Request result{};
#ifdef VERILATOR
        result.address = dut.m_address_out; result.mask = dut.m_byteenable_out;
        result.read = dut.m_read_out; result.write = dut.m_write_out;
        for (unsigned i = 0; i < 16; ++i) result.data[i] = dut.m_writedata_out[i];
#else
        result.address = dut.m_address_out(); result.mask = uint64_t(dut.m_byteenable_out());
        result.read = dut.m_read_out(); result.write = dut.m_write_out();
        for (unsigned i = 0; i < 16; ++i) result.data[i] = uint32_t(dut.m_writedata_out()[i]);
#endif
        return result;
    }

    bool response() {
#ifdef VERILATOR
        return dut.response_valid_out;
#else
        return dut.response_valid_out();
#endif
    }

    uint32_t fault() {
#ifdef VERILATOR
        return dut.fault_out;
#else
        return dut.fault_out();
#endif
    }

    void edge(bool reset) {
#ifdef VERILATOR
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#else
        dut._work(reset); dut._strobe();
#endif
        ++_system_clock; ++time;
    }

public:
    Test() {
#ifndef VERILATOR
        using namespace cpphdl;
        dut.command_valid_in = _ASSIGN(command_valid); dut.response_ready_in = _ASSIGN(response_ready);
        dut.source_in = _ASSIGN(source); dut.destination_in = _ASSIGN(destination);
        dut.rows_in = _ASSIGN(rows); dut.columns_in = _ASSIGN(columns);
        dut.source_row_stride_in = _ASSIGN(sr); dut.source_column_stride_in = _ASSIGN(sc);
        dut.destination_row_stride_in = _ASSIGN(dr); dut.destination_column_stride_in = _ASSIGN(dc);
        dut.half_in = _ASSIGN(half); dut.m_waitrequest_in = _ASSIGN(wait);
        dut.m_readdatavalid_in = _ASSIGN(valid); dut.m_readdataerror_in = _ASSIGN(read_error);
        dut.m_readdata_in = _ASSIGN(data); dut._assign();
#endif
        drive(true); edge(true);
    }

    void run(unsigned height, unsigned width, bool input_half, bool transpose_source,
             bool transpose_destination, bool strided, unsigned latency, bool inject_error = false,
             bool invalid = false) {
        rows = height; columns = width; half = input_half;
        unsigned element = half ? 2 : 4;
        source = 128 + element; destination = invalid ? source : 131076;
        sc = element * (strided ? 3 : 1); sr = (columns + 5) * sc;
        dc = strided ? 12 : 4; dr = (columns + 7) * dc;
        if (transpose_source) { sr = sc; sc = (rows + 5) * sr; }
        if (transpose_destination) { dr = dc; dc = (rows + 7) * dr; }
        std::fill(memory.begin(), memory.end(), 0xa5);
        for (unsigned r = 0; r < rows; ++r) for (unsigned c = 0; c < columns; ++c) {
            uint32_t raw = random();
            for (unsigned b = 0; b < element; ++b) memory.at(source + r * sr + c * sc + b) = raw >> (8 * b);
        }
        auto expected = memory;
        if (!invalid && !inject_error) {
            for (unsigned r = 0; r < rows; ++r) for (unsigned c = 0; c < columns; ++c) {
                uint32_t raw = 0;
                for (unsigned b = 0; b < element; ++b) raw |= uint32_t(memory.at(source + r * sr + c * sc + b)) << (8 * b);
                if (half) raw = from_half(uint16_t(raw));
                for (unsigned b = 0; b < 4; ++b) expected.at(destination + r * dr + c * dc + b) = raw >> (8 * b);
            }
        }
        command_valid = true; response_ready = false; valid = false; read_error = false;
        wait = false; drive(false); edge(false); command_valid = false;
        unsigned responses = 0, peak = 0, response_hold = 0;
        bool finished = false;
        for (unsigned cycle = 0; cycle < 30000 && !finished; ++cycle) {
            wait = (random() % 4 == 0);
            valid = !pending.empty() && pending.front().due <= time;
            uint32_t response_address = valid ? pending.front().address : 0;
            drive(false);
            Request req = request();
            bool immediate = !latency && pending.empty() && req.read && !wait;
            if (immediate) { valid = true; response_address = req.address; }
            data = 0;
            if (valid) for (unsigned i = 0; i < 64; ++i) {
                unsigned lane = i / 4;
                data[lane] = uint32_t(data[lane]) | (uint32_t(memory.at(response_address + i)) << ((i % 4) * 8));
            }
            read_error = inject_error && valid && responses == 2;
            drive(false); req = request();
            if (held) require(req == previous, "request changed while stalled");
            held = (req.read || req.write) && wait; previous = req;
            if (valid) { ++responses; if (!immediate) pending.pop_front(); }
            if (req.read && !wait && !immediate) pending.push_back({time + latency, req.address});
            peak = std::max(peak, unsigned(pending.size()));
            require(pending.size() <= 64, "read credits exceeded");
            if (req.write && !wait) {
                require(!invalid && !inject_error, "write after rejected command/read error");
                for (unsigned i = 0; i < 64; ++i) if ((req.mask >> i) & 1)
                    memory.at(req.address + i) = req.data[i / 4] >> ((i % 4) * 8);
            }
            if (response()) {
                require(pending.empty(), "response before reads drained");
                require(fault() == (invalid ? 1u : inject_error ? 2u : 0u), "wrong fault");
                require(!req.read && !req.write, "memory request during response");
                if (++response_hold == 7) { response_ready = true; finished = true; }
            } else require(response_hold == 0, "response dropped under backpressure");
            drive(false); edge(false);
        }
        require(finished, "transfer timed out");
        require(memory == expected, "matrix data or destination gap mismatch");
        if (latency > 100 && !invalid && !inject_error) require(peak == 64, "credit exhaustion not exercised");
        response_ready = false;
    }
};

int main()
{
    Test test;
    for (bool half : {false, true}) for (bool src : {false, true}) for (bool dst : {false, true}) {
        test.run(35, 37, half, src, dst, false, 11);
        test.run(9, 13, half, src, dst, true, 0);
    }
    test.run(32, 32, false, false, true, true, 120);
    test.run(32, 32, true, true, false, false, 17, true);
    test.run(3, 7, false, false, false, false, 3, false, true);
    test.run(1, 1, true, false, false, false, 0);
    std::puts("Matrix transfer: transpose, FP16, partial tiles, strides, stalls, credits, zero-latency reads and errors passed");
}
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

struct PayloadItem
{
    unsigned lo:4;
    unsigned hi:4;
} __PACKED;

union PayloadChoice
{
    struct {
        unsigned tag:3;
        unsigned value:5;
    } __PACKED s;
    u8 raw;
} __PACKED;

union PayloadBusData
{
    u8 bytes[2];
    array<PayloadItem, 2> values;
} __PACKED;

struct ArrayPayload
{
    unsigned prefix:4;
    array<u8, 3> bytes;
    array<array<u8, 2>, 3> byte_matrix;
    array<u8, 2> byte_rows[3];
    array<array<u8, 2>, 3> byte_tables[4];
    array<array<u8, 2>, 3> byte_grid[4][5];
    array<PayloadItem, 2> items;
    array<array<PayloadItem, 2>, 3> item_matrix;
    array<PayloadItem, 2> item_rows[3];
    array<array<PayloadItem, 2>, 3> item_tables[4];
    array<array<PayloadItem, 2>, 3> item_grid[4][5];
    unsigned mid:3;
    array<u16, 1> halfs;
    array<PayloadChoice, 2> choices;
    PayloadBusData bus_data;
    unsigned tail:5;
} __PACKED;

class ArrayInStruct : public Module
{
public:
    __PORT(u<8>) seed_in;
    __PORT(ArrayPayload) payload_in;
    __PORT(ArrayPayload) direct_out = __VAR(direct_comb_func());
    __PORT(ArrayPayload) state_out = __VAR(state_reg);

private:
    ArrayPayload direct_comb;
    reg<ArrayPayload> state_reg;

    static ArrayPayload make_payload(uint32_t seed)
    {
        ArrayPayload payload;
        payload.prefix = seed & 0xf;
        payload.bytes[0] = u8(seed + 0x11);
        payload.bytes[1] = u8(seed + 0x23);
        payload.bytes[2] = u8(seed + 0x35);
        payload.items[0].lo = (seed + 1) & 0xf;
        payload.items[0].hi = (seed + 2) & 0xf;
        payload.items[1].lo = (seed + 3) & 0xf;
        payload.items[1].hi = (seed + 4) & 0xf;
        payload.mid = (seed >> 2) & 0x7;
        payload.halfs[0] = u16((seed << 8) ^ 0x5aa5);
        payload.choices[0].s.tag = (seed + 5) & 0x7;
        payload.choices[0].s.value = (seed + 6) & 0x1f;
        payload.choices[1].s.tag = (seed + 7) & 0x7;
        payload.choices[1].s.value = (seed + 8) & 0x1f;
        payload.bus_data.values[0].lo = (seed + 9) & 0xf;
        payload.bus_data.values[0].hi = (seed + 10) & 0xf;
        payload.bus_data.values[1].lo = (seed + 11) & 0xf;
        payload.bus_data.values[1].hi = (seed + 12) & 0xf;
        payload.tail = (seed + 0x17) & 0x1f;
        return payload;
    }

    ArrayPayload& direct_comb_func()
    {
        ArrayPayload in_payload = payload_in();
        direct_comb = make_payload(seed_in());
        direct_comb.prefix ^= in_payload.prefix;
        direct_comb.bytes[0] = direct_comb.bytes[0] ^ in_payload.bytes[2];
        direct_comb.bytes[1] = direct_comb.bytes[1] ^ in_payload.bytes[1];
        direct_comb.bytes[2] = direct_comb.bytes[2] ^ in_payload.bytes[0];
        direct_comb.items[0].lo ^= in_payload.items[1].hi;
        direct_comb.items[0].hi ^= in_payload.items[1].lo;
        direct_comb.items[1].lo ^= in_payload.items[0].hi;
        direct_comb.items[1].hi ^= in_payload.items[0].lo;
        direct_comb.mid ^= in_payload.mid;
        direct_comb.halfs[0] = direct_comb.halfs[0] ^ in_payload.halfs[0];
        direct_comb.choices[0].s.tag ^= in_payload.choices[1].s.tag;
        direct_comb.choices[0].s.value ^= in_payload.choices[1].s.value;
        direct_comb.choices[1].s.tag ^= in_payload.choices[0].s.tag;
        direct_comb.choices[1].s.value ^= in_payload.choices[0].s.value;
        direct_comb.bus_data.values[0].lo ^= in_payload.bus_data.values[1].hi;
        direct_comb.bus_data.values[0].hi ^= in_payload.bus_data.values[1].lo;
        direct_comb.bus_data.values[1].lo ^= in_payload.bus_data.values[0].hi;
        direct_comb.bus_data.values[1].hi ^= in_payload.bus_data.values[0].lo;
        direct_comb.tail ^= in_payload.tail;
        return direct_comb;
    }

public:
    void _work(bool reset)
    {
        if (reset) {
            state_reg._next = make_payload(0);
        } else {
            state_reg._next = direct_comb_func();
        }
    }

    void _strobe()
    {
        state_reg.strobe();
    }

    void _assign() {}
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

static ArrayPayload expected_payload(uint32_t seed)
{
    ArrayPayload payload{};
    payload.prefix = seed & 0xf;
    payload.bytes[0] = u8(seed + 0x11);
    payload.bytes[1] = u8(seed + 0x23);
    payload.bytes[2] = u8(seed + 0x35);
    payload.items[0].lo = (seed + 1) & 0xf;
    payload.items[0].hi = (seed + 2) & 0xf;
    payload.items[1].lo = (seed + 3) & 0xf;
    payload.items[1].hi = (seed + 4) & 0xf;
    payload.mid = (seed >> 2) & 0x7;
    payload.halfs[0] = u16((seed << 8) ^ 0x5aa5);
    payload.choices[0].s.tag = (seed + 5) & 0x7;
    payload.choices[0].s.value = (seed + 6) & 0x1f;
    payload.choices[1].s.tag = (seed + 7) & 0x7;
    payload.choices[1].s.value = (seed + 8) & 0x1f;
    payload.bus_data.values[0].lo = (seed + 9) & 0xf;
    payload.bus_data.values[0].hi = (seed + 10) & 0xf;
    payload.bus_data.values[1].lo = (seed + 11) & 0xf;
    payload.bus_data.values[1].hi = (seed + 12) & 0xf;
    payload.tail = (seed + 0x17) & 0x1f;
    return payload;
}

static ArrayPayload expected_direct(uint32_t seed, const ArrayPayload& input)
{
    ArrayPayload payload = expected_payload(seed);
    payload.prefix ^= input.prefix;
    payload.bytes[0] = payload.bytes[0] ^ input.bytes[2];
    payload.bytes[1] = payload.bytes[1] ^ input.bytes[1];
    payload.bytes[2] = payload.bytes[2] ^ input.bytes[0];
    payload.items[0].lo ^= input.items[1].hi;
    payload.items[0].hi ^= input.items[1].lo;
    payload.items[1].lo ^= input.items[0].hi;
    payload.items[1].hi ^= input.items[0].lo;
    payload.mid ^= input.mid;
    payload.halfs[0] = payload.halfs[0] ^ input.halfs[0];
    payload.choices[0].s.tag ^= input.choices[1].s.tag;
    payload.choices[0].s.value ^= input.choices[1].s.value;
    payload.choices[1].s.tag ^= input.choices[0].s.tag;
    payload.choices[1].s.value ^= input.choices[0].s.value;
    payload.bus_data.values[0].lo ^= input.bus_data.values[1].hi;
    payload.bus_data.values[0].hi ^= input.bus_data.values[1].lo;
    payload.bus_data.values[1].lo ^= input.bus_data.values[0].hi;
    payload.bus_data.values[1].hi ^= input.bus_data.values[0].lo;
    payload.tail ^= input.tail;
    return payload;
}

static bool same_fields(const ArrayPayload& got, const ArrayPayload& exp)
{
    return got.prefix == exp.prefix &&
        got.bytes[0] == exp.bytes[0] &&
        got.bytes[1] == exp.bytes[1] &&
        got.bytes[2] == exp.bytes[2] &&
        got.items[0].lo == exp.items[0].lo &&
        got.items[0].hi == exp.items[0].hi &&
        got.items[1].lo == exp.items[1].lo &&
        got.items[1].hi == exp.items[1].hi &&
        got.mid == exp.mid &&
        got.halfs[0] == exp.halfs[0] &&
        got.choices[0].s.tag == exp.choices[0].s.tag &&
        got.choices[0].s.value == exp.choices[0].s.value &&
        got.choices[1].s.tag == exp.choices[1].s.tag &&
        got.choices[1].s.value == exp.choices[1].s.value &&
        got.bus_data.values[0].lo == exp.bus_data.values[0].lo &&
        got.bus_data.values[0].hi == exp.bus_data.values[0].hi &&
        got.bus_data.values[1].lo == exp.bus_data.values[1].lo &&
        got.bus_data.values[1].hi == exp.bus_data.values[1].hi &&
        got.tail == exp.tail;
}

template<typename T>
static void print_bytes(const char* name, const T& sample)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&sample);
    std::print("{}:", name);
    for (size_t i = 0; i < sizeof(sample); ++i) {
        std::print(" {:02x}", bytes[i]);
    }
    std::print("\n");
}

static std::filesystem::path generated_sv_path(const std::string& file)
{
    std::filesystem::path copied = std::filesystem::path("ArrayInStruct_1") / file;
    std::filesystem::path generated = std::filesystem::path("generated") / file;
    if (std::filesystem::exists(copied) && (!std::filesystem::exists(generated) ||
            std::filesystem::last_write_time(copied) >= std::filesystem::last_write_time(generated))) {
        return copied;
    }
    return generated;
}

static bool generated_sv_has_unpacked_struct_arrays()
{
    auto read_file = [](const std::filesystem::path& path, std::string& text) {
        std::ifstream in(path);
        if (!in) {
            std::print("\nERROR: can't open generated SystemVerilog file {}\n", path.string());
            return false;
        }
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    };

    const auto payload_path = generated_sv_path("ArrayPayload_pkg.sv");
    std::string payload_text;
    if (!read_file(payload_path, payload_text)) {
        return false;
    }

    std::vector<std::string> required = {
        "import PayloadItem_pkg::*;",
        "import PayloadChoice_pkg::*;",
        "import PayloadBusData_pkg::*;",
        "logic[3-1:0][8-1:0] bytes;",
        "logic[3-1:0][2-1:0][8-1:0] byte_matrix;",
        "logic[2-1:0][8-1:0] byte_rows[3];",
        "logic[3-1:0][2-1:0][8-1:0] byte_tables[4];",
        "logic[3-1:0][2-1:0][8-1:0] byte_grid[4][5];",
        "PayloadItem items[2];",
        "PayloadItem item_matrix[3][2];",
        "PayloadItem item_rows[3][2];",
        "PayloadItem item_tables[4][3][2];",
        "PayloadItem item_grid[4][5][3][2];",
        "PayloadChoice choices[2];",
        "PayloadBusData bus_data;",
        "typedef struct {"
    };

    bool ok = true;
    for (const auto& needle : required) {
        if (payload_text.find(needle) == std::string::npos) {
            std::print("\nERROR: {} does not contain '{}'\n", payload_path.string(), needle);
            ok = false;
        }
    }

    const auto bus_path = generated_sv_path("PayloadBusData_pkg.sv");
    std::string bus_text;
    if (!read_file(bus_path, bus_text)) {
        return false;
    }
    std::vector<std::string> bus_required = {
        "import PayloadItem_pkg::*;",
        "typedef union packed {",
        "PayloadItem[2-1:0] values;",
        "logic[2-1:0][8-1:0] bytes;"
    };
    for (const auto& needle : bus_required) {
        if (bus_text.find(needle) == std::string::npos) {
            std::print("\nERROR: {} does not contain '{}'\n", bus_path.string(), needle);
            ok = false;
        }
    }
    return ok;
}

#ifdef VERILATOR
template<typename VerilatedPayload>
static void write_verilated_payload(VerilatedPayload& dst, const ArrayPayload& src)
{
    dst.__PVT___align0 = 0;
    dst.__PVT__tail = src.tail;
    dst.__PVT__choices[0] = (uint8_t(src.choices[0].s.value) << 3) | uint8_t(src.choices[0].s.tag);
    dst.__PVT__choices[1] = (uint8_t(src.choices[1].s.value) << 3) | uint8_t(src.choices[1].s.tag);
    dst.__PVT__bus_data = ((uint8_t(src.bus_data.values[1].hi) << 4) | uint8_t(src.bus_data.values[1].lo)) << 8 |
        ((uint8_t(src.bus_data.values[0].hi) << 4) | uint8_t(src.bus_data.values[0].lo));
    dst.__PVT__halfs = uint16_t(src.halfs[0]);
    dst.__PVT___align2 = 0;
    dst.__PVT__mid = src.mid;
    dst.__PVT__items[0] = (uint8_t(src.items[0].hi) << 4) | uint8_t(src.items[0].lo);
    dst.__PVT__items[1] = (uint8_t(src.items[1].hi) << 4) | uint8_t(src.items[1].lo);
    dst.__PVT__bytes = uint32_t(src.bytes[0]) | (uint32_t(src.bytes[1]) << 8) | (uint32_t(src.bytes[2]) << 16);
    dst.__PVT___align1 = 0;
    dst.__PVT__prefix = src.prefix;
}

template<typename VerilatedPayload>
static ArrayPayload read_verilated_payload(const VerilatedPayload& src)
{
    ArrayPayload ret{};
    ret.prefix = src.__PVT__prefix;
    ret.bytes[0] = u8(src.__PVT__bytes);
    ret.bytes[1] = u8(src.__PVT__bytes >> 8);
    ret.bytes[2] = u8(src.__PVT__bytes >> 16);
    ret.items[0].lo = src.__PVT__items[0] & 0xf;
    ret.items[0].hi = (src.__PVT__items[0] >> 4) & 0xf;
    ret.items[1].lo = src.__PVT__items[1] & 0xf;
    ret.items[1].hi = (src.__PVT__items[1] >> 4) & 0xf;
    ret.mid = src.__PVT__mid;
    ret.halfs[0] = u16(src.__PVT__halfs);
    ret.choices[0].s.tag = src.__PVT__choices[0] & 0x7;
    ret.choices[0].s.value = (src.__PVT__choices[0] >> 3) & 0x1f;
    ret.choices[1].s.tag = src.__PVT__choices[1] & 0x7;
    ret.choices[1].s.value = (src.__PVT__choices[1] >> 3) & 0x1f;
    ret.bus_data.values[0].lo = src.__PVT__bus_data & 0xf;
    ret.bus_data.values[0].hi = (src.__PVT__bus_data >> 4) & 0xf;
    ret.bus_data.values[1].lo = (src.__PVT__bus_data >> 8) & 0xf;
    ret.bus_data.values[1].hi = (src.__PVT__bus_data >> 12) & 0xf;
    ret.tail = src.__PVT__tail;
    return ret;
}
#endif

class TestArrayInStruct : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    ArrayInStruct dut;
#endif

    u<8> seed;
    ArrayPayload payload;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = __VAR(seed);
        dut.payload_in = __VAR(payload);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.seed_in = seed;
        write_verilated_payload(dut.payload_in, payload);
        dut.clk = 1;
        dut.reset = reset;
        dut.eval();
        dut.eval();
#else
        dut._work(reset);
#endif
    }

    void neg(bool reset)
    {
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
        dut.eval();
#else
        (void)reset;
        dut._strobe();
#endif
    }

    ArrayPayload direct()
    {
#ifdef VERILATOR
        return read_verilated_payload(dut.direct_out);
#else
        return dut.direct_out();
#endif
    }

    ArrayPayload state()
    {
#ifdef VERILATOR
        return read_verilated_payload(dut.state_out);
#else
        return dut.state_out();
#endif
    }

    void check_fields(const char* name, uint32_t value, const ArrayPayload& got, const ArrayPayload& exp)
    {
        if (!same_fields(got, exp)) {
            std::print("\nseed {} {} field mismatch\n", value, name);
            print_bytes("got     ", got);
            print_bytes("expected", exp);
            error = true;
        }
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestArrayInStruct...");
#else
        std::print("CppHDL TestArrayInStruct...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "array_in_struct_test";
        _assign();

        seed = 0;
        payload = {};
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = true;
        dut.eval();
        eval(true);
        neg(true);
#endif
        eval(true);
        neg(true);
        check_fields("reset_state", 0, state(), expected_payload(0));
        ++sys_clock;

        for (uint32_t i = 0; i < 256 && !error; ++i) {
            seed = i;
            payload = expected_payload(i ^ 0x5a);
            ArrayPayload exp = expected_direct(i, payload);
            eval(false);
            check_fields("direct", i, direct(), exp);
            neg(false);
            check_fields("state", i, state(), exp);
            ++sys_clock;
        }

        std::print(" {} ({} us, size={})\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count(),
            sizeof(ArrayPayload));
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "ArrayInStruct", {
            "Predef_pkg",
            "PayloadItem_pkg",
            "PayloadChoice_pkg",
            "PayloadBusData_pkg",
            "ArrayPayload_pkg"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("ArrayInStruct_1/obj_dir/VArrayInStruct") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
    ok &= generated_sv_has_unpacked_struct_arrays();
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestArrayInStruct().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

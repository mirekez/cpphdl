#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <print>

using namespace cpphdl;

struct PackedStruct
{
    logic<3> lo;
    logic<5> hi;

    constexpr static size_t _size_bits()
    {
        return 8;
    }

    explicit operator uint64_t() const
    {
        logic<8> out = 0;
        out.bits(2, 0) = lo;
        out.bits(7, 3) = hi;
        return (uint64_t)out;
    }
};

// A 71-bit element exposes truncation that a uint64_t-only proxy path conceals.
// Its high data bit crosses the host scalar boundary used by the old conversion.
// Model explicit pack and unpack behavior for packed-array proxy regressions.
struct WidePackedStruct
{
    logic<2> err;
    logic<4> id;
    logic<64> data;
    logic<1> last;

    constexpr static size_t _size_bits()
    {
        return 71;
    }

    template<size_t W>
    WidePackedStruct& operator=(const logic<W>& value)
    {
        logic<71> packed = value;
        last = logic<1>(packed.bits(0, 0));
        data = logic<64>(packed.bits(64, 1));
        id = logic<4>(packed.bits(68, 65));
        err = logic<2>(packed.bits(70, 69));
        return *this;
    }

    logic<71> pack() const
    {
        logic<71> packed = 0;
        packed.bits(0, 0) = last;
        packed.bits(64, 1) = data;
        packed.bits(68, 65) = id;
        packed.bits(70, 69) = err;
        return packed;
    }
};

class ArrayPacked : public Module
{
public:
    _PORT(u<8>) seed_in;
    _PORT(logic<27>) dense_logic_out = _ASSIGN_COMB(dense_logic_comb_func());
    _PORT(logic<9>) dense_logic_index_out = _ASSIGN_COMB(dense_logic_index_comb_func());
    _PORT(logic<15>) dense_u_out = _ASSIGN_COMB(dense_u_comb_func());
    _PORT(u<3>) dense_u_index_out = _ASSIGN_COMB(dense_u_index_comb_func());
    _PORT(u<8>) array2d_out = _ASSIGN_COMB(array2d_comb_func());
    _PORT(u<8>) array3d_out = _ASSIGN_COMB(array3d_comb_func());

private:
    logic<27> dense_logic_comb;
    logic<9> dense_logic_index_comb;
    logic<15> dense_u_comb;
    u<3> dense_u_index_comb;
    u<8> array2d_comb;
    u<8> array3d_comb;

    logic<27>& dense_logic_comb_func()
    {
        array<3,logic<9>, true> dense_logic;
        logic<9> word0;
        logic<9> word1;
        logic<9> word2;
        dense_logic = 0;
        dense_logic[0] = logic<9>(0x101);
        dense_logic[1] = logic<9>(0x040 | ((uint64_t)seed_in() & 0x1f));
        dense_logic[2] = logic<9>(0x1aa);
        word0 = dense_logic[0];
        word1 = dense_logic[1];
        word2 = dense_logic[2];
        dense_logic_comb = (logic<27>(word2) << 18) | (logic<27>(word1) << 9) | logic<27>(word0);
        return dense_logic_comb;
    }

    logic<9>& dense_logic_index_comb_func()
    {
        array<3,logic<9>, true> dense_logic;
        dense_logic = 0;
        dense_logic[0] = logic<9>(0x101);
        dense_logic[1] = logic<9>(0x040 | ((uint64_t)seed_in() & 0x1f));
        dense_logic[2] = logic<9>(0x1aa);
        dense_logic_index_comb = dense_logic[1];
        return dense_logic_index_comb;
    }

    logic<15>& dense_u_comb_func()
    {
        array<5,u<3>, true> dense_u;
        logic<3> word0;
        logic<3> word1;
        logic<3> word2;
        logic<3> word3;
        logic<3> word4;
        dense_u = 0;
        dense_u[0] = 1;
        dense_u[1] = 2;
        dense_u[2] = 3;
        dense_u[3] = 4;
        dense_u[4] = (uint64_t)seed_in() & 0x7;
        word0 = dense_u[0];
        word1 = dense_u[1];
        word2 = dense_u[2];
        word3 = dense_u[3];
        word4 = dense_u[4];
        dense_u_comb = (logic<15>(word4) << 12) | (logic<15>(word3) << 9) | (logic<15>(word2) << 6) | (logic<15>(word1) << 3) | logic<15>(word0);
        return dense_u_comb;
    }

    u<3>& dense_u_index_comb_func()
    {
        array<5,u<3>, true> dense_u;
        dense_u = 0;
        dense_u[0] = 1;
        dense_u[1] = 2;
        dense_u[2] = 3;
        dense_u[3] = 4;
        dense_u[4] = (uint64_t)seed_in() & 0x7;
        dense_u_index_comb = (uint64_t)dense_u[2];
        return dense_u_index_comb;
    }

    u<8>& array2d_comb_func()
    {
        array2D<2, 3, u<8>, true> values;
        values = 0;
        values[1][2] = (uint8_t)seed_in() + 0x20u;
        array2d_comb = (u<8>)values[1][2];
        return array2d_comb;
    }

    u<8>& array3d_comb_func()
    {
        array3D<2, 2, 3, u<8>, true> values;
        values = 0;
        values[1][1][2] = (uint8_t)seed_in() + 0x40u;
        array3d_comb = (u<8>)values[1][1][2];
        return array3d_comb;
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

template<typename T>
static T verilator_read(const void* ptr)
{
    T value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

static bool check(bool condition, const char* text)
{
    if (!condition) {
        std::print("\nERROR: {}\n", text);
        return false;
    }
    return true;
}

static bool check_direct_arrays()
{
    bool ok = true;

    static_assert(std::is_same_v<array2D<2, 3, u<2>, true>, array<2, array<3, u<2>, true>, true>>);
    static_assert(std::is_same_v<array3D<2, 3, 4, u<2>, true>, array<2, array2D<3, 4, u<2>, true>, true>>);
    static_assert(std::is_same_v<array4D<2, 3, 4, 5, u<2>, true>, array<2, array3D<3, 4, 5, u<2>, true>, true>>);
    ok &= check(array2D<2, 3, u<2>, true>::_size_bits() == 12, "array2D packed alias width");
    ok &= check(array3D<2, 3, 4, u<2>, true>::_size_bits() == 48, "array3D packed alias width");
    ok &= check(array4D<2, 3, 4, 5, u<2>, true>::_size_bits() == 240, "array4D packed alias width");

    array2D<2, 3, u<2>, true> alias2d;
    alias2d = 0;
    alias2d[1][2] = 3;
    ok &= check((uint64_t)(u<2>)alias2d[1][2] == 3, "array2D packed alias nested index");
    ok &= check((uint64_t)(u<2>)alias2d[1][1] == 0, "array2D packed alias preserves adjacent element");

    array3D<2, 3, 4, u<2>, true> alias3d;
    alias3d = 0;
    alias3d[1][2][3] = 2;
    ok &= check((uint64_t)(u<2>)alias3d[1][2][3] == 2, "array3D packed alias nested index");
    ok &= check((uint64_t)(u<2>)alias3d[1][2][2] == 0, "array3D packed alias preserves adjacent element");

    array<3,logic<9>,true> dense_logic;
    dense_logic = 0;
    dense_logic[0] = logic<9>(0x101);
    dense_logic[1] = logic<9>(0x055);
    dense_logic[2] = logic<9>(0x1aa);

    ok &= check(array<3,logic<9>, true>::_size_bits() == 27, "packed logic array width");
    ok &= check((uint64_t)static_cast<logic<9>>(dense_logic[0]) == 0x101, "packed logic index 0");
    ok &= check((uint64_t)static_cast<logic<9>>(dense_logic[1]) == 0x055, "packed logic index 1");
    ok &= check((uint64_t)static_cast<logic<9>>(dense_logic[2]) == 0x1aa, "packed logic index 2");
    ok &= check((uint64_t)logic<9>(dense_logic.bits(8, 0)) == 0x101, "packed logic bits first word");
    ok &= check((uint64_t)logic<9>(dense_logic.bits(17, 9)) == 0x055, "packed logic bits second word");
    ok &= check((uint64_t)logic<9>(dense_logic.bits(26, 18)) == 0x1aa, "packed logic bits third word");

    dense_logic.bits(13, 9) = 0x1f;
    ok &= check((uint64_t)static_cast<logic<9>>(dense_logic[1]) == 0x05f, "packed bits write through");

    array<5,u<3>, true> dense_u;
    dense_u = 0;
    dense_u[0] = 1;
    dense_u[1] = 2;
    dense_u[2] = 3;
    dense_u[3] = 4;
    dense_u[4] = 5;

    ok &= check(array<5,u<3>, true>::_size_bits() == 15, "packed u array width");
    ok &= check((uint64_t)(u<3>)dense_u[2] == 3, "packed u direct index conversion");
    ok &= check((uint64_t)static_cast<logic<3>>(dense_u[0]) == 1, "packed u index 0");
    ok &= check((uint64_t)static_cast<logic<3>>(dense_u[4]) == 5, "packed u index 4");
    ok &= check((uint64_t)static_cast<const logic<15>&>(dense_u) == ((5u << 12) | (4u << 9) | (3u << 6) | (2u << 3) | 1u), "packed u full bits");

    PackedStruct item{};
    item.lo = 0x5;
    item.hi = 0x12;

    array<2,PackedStruct,true> dense_struct;
    dense_struct = 0;
    dense_struct[1] = item;
    ok &= check(array<2,PackedStruct, true>::_size_bits() == 16, "packed struct width");
    ok &= check((uint64_t)logic<8>(dense_struct.bits(15, 8)) == (uint64_t)item, "packed struct write through");

    // Exercise both proxy-to-value packing and proxy-to-proxy assignment above 64 bits.
    // Either old path silently cleared packed bit 64 and the data field's top bit.
    // Check the field value and its exact packed position after both conversions.
    WidePackedStruct wide{};
    wide.data = logic<64>(0xfc02721301320213ull);
    wide.id = logic<4>(8);
    wide.last = logic<1>(0);
    wide.err = logic<2>(0);

    array<2, logic<71>, true> wide_logic_array{};
    wide_logic_array[0] = wide.pack();
    WidePackedStruct unpacked = cpphdl::unpack_value<WidePackedStruct>(cpphdl::pack_value<71>(wide_logic_array[0]));
    ok &= check((uint64_t)unpacked.data == 0xfc02721301320213ull, "wide packed array ref pack_value keeps bit 63 of data field");
    ok &= check((bool)unpacked.pack()[64], "wide packed array ref pack_value keeps packed bit 64");

    array<2, WidePackedStruct, true> wide_struct_array{};
    wide_struct_array[0] = wide_logic_array[0];
    WidePackedStruct assigned = wide_struct_array[0];
    ok &= check((uint64_t)assigned.data == 0xfc02721301320213ull, "wide packed ref to packed struct ref assignment keeps bit 63 of data field");
    ok &= check((bool)assigned.pack()[64], "wide packed ref to packed struct ref assignment keeps packed bit 64");

    return ok;
}

class TestArrayPacked : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    ArrayPacked dut;
#endif

    u<8> seed;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = _ASSIGN_REG(seed);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.seed_in = seed;
        dut.clk = 1;
        dut.reset = reset;
        dut.eval();
#else
        (void)reset;
        dut._work(reset);
#endif
    }

    void neg(bool reset)
    {
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
#else
        (void)reset;
#endif
    }

    logic<27> dense_logic()
    {
#ifdef VERILATOR
        return verilator_read<logic<27>>(&dut.dense_logic_out);
#else
        return dut.dense_logic_out();
#endif
    }

    logic<9> dense_logic_index()
    {
#ifdef VERILATOR
        return verilator_read<logic<9>>(&dut.dense_logic_index_out);
#else
        return dut.dense_logic_index_out();
#endif
    }

    logic<15> dense_u()
    {
#ifdef VERILATOR
        return verilator_read<logic<15>>(&dut.dense_u_out);
#else
        return dut.dense_u_out();
#endif
    }

    u<3> dense_u_index()
    {
#ifdef VERILATOR
        return u<3>(verilator_read<uint8_t>(&dut.dense_u_index_out));
#else
        return dut.dense_u_index_out();
#endif
    }

    u<8> array2d()
    {
#ifdef VERILATOR
        return u<8>(verilator_read<uint8_t>(&dut.array2d_out));
#else
        return dut.array2d_out();
#endif
    }

    u<8> array3d()
    {
#ifdef VERILATOR
        return u<8>(verilator_read<uint8_t>(&dut.array3d_out));
#else
        return dut.array3d_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestArrayPacked...");
#else
        std::print("CppHDL TestArrayPacked...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "array_packed_test";
        _assign();

#ifndef VERILATOR
        error |= !check_direct_arrays();
#endif

        for (uint32_t i = 0; i < 8; ++i) {
            seed = i;
            eval(false);

            const uint64_t expected_logic1 = 0x040 | (i & 0x1f);
            const uint64_t expected_logic = (0x1aaull << 18) | (expected_logic1 << 9) | 0x101ull;
            const uint64_t expected_u = ((i & 0x7) << 12) | (4u << 9) | (3u << 6) | (2u << 3) | 1u;
            error |= !check((uint64_t)dense_logic() == expected_logic, "module packed logic full bits");
            error |= !check((uint64_t)dense_logic_index() == expected_logic1, "module packed logic index");
            error |= !check((uint64_t)dense_u() == expected_u, "module packed u full bits");
            error |= !check((uint64_t)dense_u_index() == 3, "module packed u index");
            error |= !check((uint64_t)array2d() == ((i + 0x20u) & 0xffu), "module packed array2D index");
            error |= !check((uint64_t)array3d() == ((i + 0x40u) & 0xffu), "module packed array3D index");

            neg(false);
            ++_system_clock;
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "ArrayPacked", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("ArrayPacked_1/obj_dir/VArrayPacked") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestArrayPacked().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

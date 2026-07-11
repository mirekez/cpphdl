#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <print>

using namespace cpphdl;

class ArrayUnpacked : public Module
{
public:
    _PORT(u<8>) seed_in;
    _PORT(logic<9>) unpacked_logic0_out = _ASSIGN_COMB(unpacked_logic0_comb_func());
    _PORT(logic<9>) unpacked_logic1_out = _ASSIGN_COMB(unpacked_logic1_comb_func());
    _PORT(logic<9>) unpacked_logic2_out = _ASSIGN_COMB(unpacked_logic2_comb_func());
    _PORT(u<3>) unpacked_u_index_out = _ASSIGN_COMB(unpacked_u_index_comb_func());
    _PORT(u<3>) unpacked_u_last_out = _ASSIGN_COMB(unpacked_u_last_comb_func());
    _PORT(u<8>) array2d_out = _ASSIGN_COMB(array2d_comb_func());
    _PORT(u<8>) array3d_out = _ASSIGN_COMB(array3d_comb_func());

private:
    logic<9> unpacked_logic0_comb;
    logic<9> unpacked_logic1_comb;
    logic<9> unpacked_logic2_comb;
    u<3> unpacked_u_index_comb;
    u<3> unpacked_u_last_comb;
    u<8> array2d_comb;
    u<8> array3d_comb;

    logic<9>& unpacked_logic0_comb_func()
    {
        array<3,logic<9>> unpacked_logic;
        unpacked_logic = 0;
        unpacked_logic[0] = 0x101;
        unpacked_logic[1] = logic<9>(0x040 | ((uint64_t)seed_in() & 0x1f));
        unpacked_logic[2] = 0x1aa;
        unpacked_logic0_comb = unpacked_logic[0];
        return unpacked_logic0_comb;
    }

    logic<9>& unpacked_logic1_comb_func()
    {
        array<3,logic<9>> unpacked_logic;
        unpacked_logic = 0;
        unpacked_logic[0] = 0x101;
        unpacked_logic[1] = logic<9>(0x040 | ((uint64_t)seed_in() & 0x1f));
        unpacked_logic[2] = 0x1aa;
        unpacked_logic1_comb = unpacked_logic[1];
        return unpacked_logic1_comb;
    }

    logic<9>& unpacked_logic2_comb_func()
    {
        array<3,logic<9>> unpacked_logic;
        unpacked_logic = 0;
        unpacked_logic[0] = 0x101;
        unpacked_logic[1] = logic<9>(0x040 | ((uint64_t)seed_in() & 0x1f));
        unpacked_logic[2] = 0x1aa;
        unpacked_logic2_comb = unpacked_logic[2];
        return unpacked_logic2_comb;
    }

    u<3>& unpacked_u_index_comb_func()
    {
        array<5,u<3>> unpacked_u;
        unpacked_u = 0;
        unpacked_u[0] = 1;
        unpacked_u[1] = 2;
        unpacked_u[2] = 3;
        unpacked_u[3] = 4;
        unpacked_u[4] = (uint64_t)seed_in() & 0x7;
        unpacked_u_index_comb = unpacked_u[2];
        return unpacked_u_index_comb;
    }

    u<3>& unpacked_u_last_comb_func()
    {
        array<5,u<3>> unpacked_u;
        unpacked_u = 0;
        unpacked_u[0] = 1;
        unpacked_u[1] = 2;
        unpacked_u[2] = 3;
        unpacked_u[3] = 4;
        unpacked_u[4] = (uint64_t)seed_in() & 0x7;
        unpacked_u_last_comb = unpacked_u[4];
        return unpacked_u_last_comb;
    }

    u<8>& array2d_comb_func()
    {
        array2D<2, 3, u<8>> values;
        values = 0;
        values[1][2] = (uint8_t)seed_in() + 0x20u;
        array2d_comb = values[1][2];
        return array2d_comb;
    }

    u<8>& array3d_comb_func()
    {
        array3D<2, 2, 3, u<8>> values;
        values = 0;
        values[1][1][2] = (uint8_t)seed_in() + 0x40u;
        array3d_comb = values[1][1][2];
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

struct ArrayStringElement
{
    const char* text;

    std::string to_string() const
    {
        return text;
    }
};

struct PackedArrayStringElement
{
    uint8_t raw = 0;

    PackedArrayStringElement() = default;

    explicit PackedArrayStringElement(uint64_t value)
        : raw((uint8_t)value)
    {
    }

    static constexpr size_t _size_bits()
    {
        return 8;
    }

    explicit operator uint64_t() const
    {
        return raw;
    }

    std::string to_string() const
    {
        char text[3] = {};
        std::snprintf(text, sizeof(text), "%02x", raw);
        return text;
    }
};

static bool check_direct_arrays()
{
    bool ok = true;

    static_assert(std::is_same_v<array2D<2, 3, u8>, array<2, array<3, u8, false>, false>>);
    static_assert(std::is_same_v<array3D<2, 3, 4, u8>, array<2, array2D<3, 4, u8, false>, false>>);
    static_assert(std::is_same_v<array4D<2, 3, 4, 5, u8>, array<2, array3D<3, 4, 5, u8, false>, false>>);

    array2D<2, 3, u8> alias2d{};
    alias2d[1][2] = u8(0x5a);
    ok &= check((uint8_t)alias2d[1][2] == 0x5a, "array2D unpacked alias index");

    array3D<2, 3, 4, u8> alias3d{};
    alias3d[1][2][3] = u8(0x6b);
    ok &= check((uint8_t)alias3d[1][2][3] == 0x6b, "array3D unpacked alias index");

    array4D<2, 3, 4, 5, u8> alias4d{};
    alias4d[1][2][3][4] = u8(0x7c);
    ok &= check((uint8_t)alias4d[1][2][3][4] == 0x7c, "array4D unpacked alias index");

    array<3, ArrayStringElement> string_elements;
    string_elements[0].text = "low";
    string_elements[1].text = "mid";
    string_elements[2].text = "high";
    ok &= check(string_elements.to_string() == "highmidlow",
        "unpacked array delegates formatting to element to_string");

    array<3, PackedArrayStringElement, true> packed_string_elements;
    packed_string_elements[0] = PackedArrayStringElement(0x0a);
    packed_string_elements[1] = PackedArrayStringElement(0x0b);
    packed_string_elements[2] = PackedArrayStringElement(0x0c);
    ok &= check(packed_string_elements.to_string() == "0c0b0a",
        "packed array delegates formatting to reconstructed element to_string");

    array<3,logic<9>> unpacked_logic;
    unpacked_logic = 0;
    unpacked_logic[0] = 0x101;
    unpacked_logic[1] = 0x055;
    unpacked_logic[2] = 0x1aa;

    ok &= check(array<3,logic<9>>::_size_bits() == sizeof(logic<9>) * 8 * 3, "unpacked logic array width");
    ok &= check(array<3,logic<9>>::_size_bits() == 48, "unpacked logic uses byte object width");
    ok &= check((uint64_t)unpacked_logic[0] == 0x101, "unpacked logic index 0");
    ok &= check((uint64_t)unpacked_logic[1] == 0x055, "unpacked logic index 1");
    ok &= check((uint64_t)unpacked_logic[2] == 0x1aa, "unpacked logic index 2");

    ok &= check((uint64_t)logic<16>(unpacked_logic.bits(15, 0)) == 0x0101, "unpacked bits first byte-aligned object");
    ok &= check((uint64_t)logic<16>(unpacked_logic.bits(31, 16)) == 0x0055, "unpacked bits second byte-aligned object");
    ok &= check((uint64_t)logic<16>(unpacked_logic.bits(47, 32)) == 0x01aa, "unpacked bits third byte-aligned object");

    auto packed_logic = unpacked_logic.pack();
    ok &= check(decltype(packed_logic)::PACKED, "unpacked logic pack returns packed array");
    ok &= check(decltype(packed_logic)::_size_bits() == 27, "unpacked logic pack uses compact width");
    ok &= check((uint64_t)packed_logic[0] == 0x101, "unpacked logic pack index 0");
    ok &= check((uint64_t)packed_logic[1] == 0x055, "unpacked logic pack index 1");
    ok &= check((uint64_t)packed_logic[2] == 0x1aa, "unpacked logic pack index 2");
    ok &= check((uint64_t)logic<9>(packed_logic.bits(8, 0)) == 0x101, "unpacked logic pack bits first compact object");
    ok &= check((uint64_t)logic<9>(packed_logic.bits(17, 9)) == 0x055, "unpacked logic pack bits second compact object");
    ok &= check((uint64_t)logic<9>(packed_logic.bits(26, 18)) == 0x1aa, "unpacked logic pack bits third compact object");

    unpacked_logic.bits(20, 16) = 0x1f;
    ok &= check((uint64_t)unpacked_logic[1] == 0x05f, "unpacked bits write through");
    ok &= check((uint64_t)unpacked_logic[0] == 0x101, "unpacked bits did not disturb previous object");
    ok &= check((uint64_t)unpacked_logic[2] == 0x1aa, "unpacked bits did not disturb next object");

    array<5,u<3>> unpacked_u;
    unpacked_u = 0;
    unpacked_u[0] = 1;
    unpacked_u[1] = 2;
    unpacked_u[2] = 3;
    unpacked_u[3] = 4;
    unpacked_u[4] = 5;

    ok &= check(array<5,u<3>>::_size_bits() == sizeof(u<3>) * 8 * 5, "unpacked u array width");
    ok &= check(array<5,u<3>>::_size_bits() == 40, "unpacked u uses byte object width");
    ok &= check((uint64_t)unpacked_u[0] == 1, "unpacked u index 0");
    ok &= check((uint64_t)unpacked_u[4] == 5, "unpacked u index 4");
    ok &= check((uint64_t)logic<8>(unpacked_u.bits(39, 32)) == 5, "unpacked u byte-aligned final object");

    auto packed_u = unpacked_u.pack();
    ok &= check(decltype(packed_u)::PACKED, "unpacked u pack returns packed array");
    ok &= check(decltype(packed_u)::_size_bits() == 15, "unpacked u pack uses compact width");
    ok &= check((uint64_t)packed_u[0] == 1, "unpacked u pack index 0");
    ok &= check((uint64_t)packed_u[4] == 5, "unpacked u pack index 4");
    ok &= check((uint64_t)logic<3>(packed_u.bits(14, 12)) == 5, "unpacked u pack compact final object");

    return ok;
}

class TestArrayUnpacked : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    ArrayUnpacked dut;
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

    logic<9> unpacked_logic0()
    {
#ifdef VERILATOR
        return verilator_read<logic<9>>(&dut.unpacked_logic0_out);
#else
        return dut.unpacked_logic0_out();
#endif
    }

    logic<9> unpacked_logic1()
    {
#ifdef VERILATOR
        return verilator_read<logic<9>>(&dut.unpacked_logic1_out);
#else
        return dut.unpacked_logic1_out();
#endif
    }

    logic<9> unpacked_logic2()
    {
#ifdef VERILATOR
        return verilator_read<logic<9>>(&dut.unpacked_logic2_out);
#else
        return dut.unpacked_logic2_out();
#endif
    }

    u<3> unpacked_u_index()
    {
#ifdef VERILATOR
        return u<3>(verilator_read<uint8_t>(&dut.unpacked_u_index_out));
#else
        return dut.unpacked_u_index_out();
#endif
    }

    u<3> unpacked_u_last()
    {
#ifdef VERILATOR
        return u<3>(verilator_read<uint8_t>(&dut.unpacked_u_last_out));
#else
        return dut.unpacked_u_last_out();
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
        std::print("VERILATOR TestArrayUnpacked...");
#else
        std::print("CppHDL TestArrayUnpacked...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "array_unpacked_test";
        _assign();

#ifndef VERILATOR
        error |= !check_direct_arrays();
#endif

        for (uint32_t i = 0; i < 8; ++i) {
            seed = i;
            eval(false);

            const uint64_t expected_logic1 = 0x040 | (i & 0x1f);

            error |= !check((uint64_t)unpacked_logic0() == 0x101, "module unpacked logic index 0");
            error |= !check((uint64_t)unpacked_logic1() == expected_logic1, "module unpacked logic index 1");
            error |= !check((uint64_t)unpacked_logic2() == 0x1aa, "module unpacked logic index 2");
            error |= !check((uint64_t)unpacked_u_index() == 3, "module unpacked u index");
            error |= !check((uint64_t)unpacked_u_last() == (i & 0x7), "module unpacked u last index");
            error |= !check((uint64_t)array2d() == ((i + 0x20u) & 0xffu), "module unpacked array2D index");
            error |= !check((uint64_t)array3d() == ((i + 0x40u) & 0xffu), "module unpacked array3D index");

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
        ok &= VerilatorCompile(__FILE__, "ArrayUnpacked", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("ArrayUnpacked_1/obj_dir/VArrayUnpacked") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestArrayUnpacked().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

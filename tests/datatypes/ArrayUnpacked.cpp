#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
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

private:
    logic<9> unpacked_logic0_comb;
    logic<9> unpacked_logic1_comb;
    logic<9> unpacked_logic2_comb;
    u<3> unpacked_u_index_comb;
    u<3> unpacked_u_last_comb;

    logic<9>& unpacked_logic0_comb_func()
    {
        array<logic<9>, 3> unpacked_logic;
        unpacked_logic = 0;
        unpacked_logic[0] = 0x101;
        unpacked_logic[1] = logic<9>(0x040 | ((uint64_t)seed_in() & 0x1f));
        unpacked_logic[2] = 0x1aa;
        unpacked_logic0_comb = unpacked_logic[0];
        return unpacked_logic0_comb;
    }

    logic<9>& unpacked_logic1_comb_func()
    {
        array<logic<9>, 3> unpacked_logic;
        unpacked_logic = 0;
        unpacked_logic[0] = 0x101;
        unpacked_logic[1] = logic<9>(0x040 | ((uint64_t)seed_in() & 0x1f));
        unpacked_logic[2] = 0x1aa;
        unpacked_logic1_comb = unpacked_logic[1];
        return unpacked_logic1_comb;
    }

    logic<9>& unpacked_logic2_comb_func()
    {
        array<logic<9>, 3> unpacked_logic;
        unpacked_logic = 0;
        unpacked_logic[0] = 0x101;
        unpacked_logic[1] = logic<9>(0x040 | ((uint64_t)seed_in() & 0x1f));
        unpacked_logic[2] = 0x1aa;
        unpacked_logic2_comb = unpacked_logic[2];
        return unpacked_logic2_comb;
    }

    u<3>& unpacked_u_index_comb_func()
    {
        array<u<3>, 5> unpacked_u;
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
        array<u<3>, 5> unpacked_u;
        unpacked_u = 0;
        unpacked_u[0] = 1;
        unpacked_u[1] = 2;
        unpacked_u[2] = 3;
        unpacked_u[3] = 4;
        unpacked_u[4] = (uint64_t)seed_in() & 0x7;
        unpacked_u_last_comb = unpacked_u[4];
        return unpacked_u_last_comb;
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

    array<logic<9>, 3> unpacked_logic;
    unpacked_logic = 0;
    unpacked_logic[0] = 0x101;
    unpacked_logic[1] = 0x055;
    unpacked_logic[2] = 0x1aa;

    ok &= check(array<logic<9>, 3>::_size_bits() == sizeof(logic<9>) * 8 * 3, "unpacked logic array width");
    ok &= check(array<logic<9>, 3>::_size_bits() == 48, "unpacked logic uses byte object width");
    ok &= check((uint64_t)unpacked_logic[0] == 0x101, "unpacked logic index 0");
    ok &= check((uint64_t)unpacked_logic[1] == 0x055, "unpacked logic index 1");
    ok &= check((uint64_t)unpacked_logic[2] == 0x1aa, "unpacked logic index 2");

    ok &= check((uint64_t)logic<16>(unpacked_logic.bits(15, 0)) == 0x0101, "unpacked bits first byte-aligned object");
    ok &= check((uint64_t)logic<16>(unpacked_logic.bits(31, 16)) == 0x0055, "unpacked bits second byte-aligned object");
    ok &= check((uint64_t)logic<16>(unpacked_logic.bits(47, 32)) == 0x01aa, "unpacked bits third byte-aligned object");

    unpacked_logic.bits(20, 16) = 0x1f;
    ok &= check((uint64_t)unpacked_logic[1] == 0x05f, "unpacked bits write through");
    ok &= check((uint64_t)unpacked_logic[0] == 0x101, "unpacked bits did not disturb previous object");
    ok &= check((uint64_t)unpacked_logic[2] == 0x1aa, "unpacked bits did not disturb next object");

    array<u<3>, 5> unpacked_u;
    unpacked_u = 0;
    unpacked_u[0] = 1;
    unpacked_u[1] = 2;
    unpacked_u[2] = 3;
    unpacked_u[3] = 4;
    unpacked_u[4] = 5;

    ok &= check(array<u<3>, 5>::_size_bits() == sizeof(u<3>) * 8 * 5, "unpacked u array width");
    ok &= check(array<u<3>, 5>::_size_bits() == 40, "unpacked u uses byte object width");
    ok &= check((uint64_t)unpacked_u[0] == 1, "unpacked u index 0");
    ok &= check((uint64_t)unpacked_u[4] == 5, "unpacked u index 4");
    ok &= check((uint64_t)logic<8>(unpacked_u.bits(39, 32)) == 5, "unpacked u byte-aligned final object");

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

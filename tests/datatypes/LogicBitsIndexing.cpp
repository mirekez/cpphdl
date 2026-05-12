#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

class LogicBitsIndexing : public Module
{
public:
    _PORT(u<3>)       word_in;
    _PORT(u<32>)      seed_in;
    _PORT(logic<16>)  direct_out = _BIND_VAR(direct_comb_func());
    _PORT(logic<16>)  next_out = _BIND_VAR(next_comb_func());
    _PORT(logic<8>)   byte_out = _BIND_VAR(byte_comb_func());
    _PORT(logic<128>) edited_out = _BIND_VAR(edited_comb_func());

private:
    logic<128> source_comb;
    logic<128> edited_comb;
    logic<16> direct_comb;
    logic<16> next_comb;
    logic<8> byte_comb;

    void build_source()
    {
        uint32_t seed = seed_in();
        source_comb = 0;
        source_comb.bits(15, 0) = logic<16>((uint64_t)seed + 0x1000);
        source_comb.bits(31, 16) = logic<16>((uint64_t)seed + 0x2111);
        source_comb.bits(47, 32) = logic<16>((uint64_t)seed + 0x3222);
        source_comb.bits(63, 48) = logic<16>((uint64_t)seed + 0x4333);
        source_comb.bits(79, 64) = logic<16>((uint64_t)seed + 0x5444);
        source_comb.bits(95, 80) = logic<16>((uint64_t)seed + 0x6555);
        source_comb.bits(111, 96) = logic<16>((uint64_t)seed + 0x7666);
        source_comb.bits(127, 112) = logic<16>((uint64_t)seed + 0x8777);
    }

    logic<16>& direct_comb_func()
    {
        uint32_t word = (uint32_t)word_in();
        build_source();
        direct_comb = source_comb.bits(word * 16 + 15, word * 16);
        return direct_comb;
    }

    logic<16>& next_comb_func()
    {
        uint32_t word = (uint32_t)word_in();
        build_source();
        next_comb = source_comb.bits((word + 1) * 16 + 15, (word + 1) * 16);
        return next_comb;
    }

    logic<8>& byte_comb_func()
    {
        uint32_t word = (uint32_t)word_in();
        build_source();
        byte_comb = source_comb.bits(word * 8 + 7, word * 8);
        return byte_comb;
    }

    logic<128>& edited_comb_func()
    {
        uint32_t word = (uint32_t)word_in();
        build_source();
        edited_comb = source_comb;
        edited_comb.bits(word * 16 + 15, word * 16) = logic<16>((uint64_t)seed_in() ^ 0x55aa);
        edited_comb.bits((word + 1) * 16 + 15, (word + 1) * 16) = logic<16>((uint64_t)seed_in() ^ 0xaa55);
        edited_comb.bits(word * 8 + 7, word * 8) = logic<8>((uint64_t)seed_in() ^ 0x5a);
        return edited_comb;
    }

public:
    void _work(bool reset)
    {
    }

    void _strobe() {}
    void _assign() {}
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

template<typename T>
static T verilator_read(const void* ptr)
{
    T value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

static logic<128> expected_source(uint32_t seed)
{
    logic<128> ret;
    ret = 0;
    ret.bits(15, 0) = seed + 0x1000;
    ret.bits(31, 16) = seed + 0x2111;
    ret.bits(47, 32) = seed + 0x3222;
    ret.bits(63, 48) = seed + 0x4333;
    ret.bits(79, 64) = seed + 0x5444;
    ret.bits(95, 80) = seed + 0x6555;
    ret.bits(111, 96) = seed + 0x7666;
    ret.bits(127, 112) = seed + 0x8777;
    return ret;
}

static logic<128> expected_edited(uint32_t seed, uint32_t word)
{
    logic<128> ret = expected_source(seed);
    ret.bits(word * 16 + 15, word * 16) = logic<16>(seed ^ 0x55aa);
    ret.bits((word + 1) * 16 + 15, (word + 1) * 16) = logic<16>(seed ^ 0xaa55);
    ret.bits(word * 8 + 7, word * 8) = logic<8>(seed ^ 0x5a);
    return ret;
}

class TestLogicBitsIndexing : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    LogicBitsIndexing dut;
#endif

    u<3> word;
    u<32> seed;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.word_in = _BIND_VAR(word);
        dut.seed_in = _BIND_VAR(seed);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.word_in = word;
        dut.seed_in = seed;
        dut.clk = 1;
        dut.reset = reset;
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
#else
        (void)reset;
#endif
    }

    logic<16> direct()
    {
#ifdef VERILATOR
        return verilator_read<logic<16>>(&dut.direct_out);
#else
        return dut.direct_out();
#endif
    }

    logic<16> next()
    {
#ifdef VERILATOR
        return verilator_read<logic<16>>(&dut.next_out);
#else
        return dut.next_out();
#endif
    }

    logic<8> byte()
    {
#ifdef VERILATOR
        return verilator_read<logic<8>>(&dut.byte_out);
#else
        return dut.byte_out();
#endif
    }

    logic<128> edited()
    {
#ifdef VERILATOR
        return verilator_read<logic<128>>(&dut.edited_out);
#else
        return dut.edited_out();
#endif
    }

    template<typename T>
    void check(const char* name, const T& got, const T& expected)
    {
        if (got != expected) {
            std::print("\n{} ERROR: got {}, expected {}\n", name, got, expected);
            error = true;
        }
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestLogicBitsIndexing...");
#else
        std::print("CppHDL TestLogicBitsIndexing...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "logic_bits_indexing_test";
        _assign();

        word = 0;
        seed = 0;
        eval(true);
        neg(true);
        ++sys_clock;

        for (uint32_t s = 0; s < 32 && !error; ++s) {
            seed = 0x12340000u + s * 0x1021u;
            for (uint32_t w = 0; w < 7 && !error; ++w) {
                word = w;
                eval(false);
                logic<128> src = expected_source((uint32_t)seed);
                check("direct", direct(), (logic<16>)src.bits(w * 16 + 15, w * 16));
                check("next", next(), (logic<16>)src.bits((w + 1) * 16 + 15, (w + 1) * 16));
                check("byte", byte(), (logic<8>)src.bits(w * 8 + 7, w * 8));
                check("edited", edited(), expected_edited((uint32_t)seed, w));
                neg(false);
                ++sys_clock;
            }
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
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "LogicBitsIndexing", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("LogicBitsIndexing_1/obj_dir/VLogicBitsIndexing") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestLogicBitsIndexing().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

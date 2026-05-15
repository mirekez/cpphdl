#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

class CombFunc : public Module
{
public:
    _PORT(bool) early_in;
    _PORT(u<32>) seed_in;
    _PORT(u<32>) plain_out = _ASSIGN_COMB(plain_comb_func());
    _PORT(u<32>) array_out = _ASSIGN_COMB(array_comb_func());
    _PORT(u<32>) early_out = _ASSIGN_COMB(early_comb_func());

private:
    u<32> plain_comb;
    u<32> array_comb;
    u<32> early_comb;

    u<32> c_line[4];
    u<32> c_grid[2][3];
    array<u<32>, 4> cpp_line;
    array<array<u<32>, 3>, 2> cpp_grid;

    u<32>& plain_comb_func()
    {
        plain_comb = seed_in() + u<32>(1);
        plain_comb = plain_comb + u<32>(2);
        return plain_comb;
    }

    u<32>& array_comb_func()
    {
        size_t i;
        size_t y;
        size_t x;
        array_comb = 0;
        for (i = 0; i < 4; ++i) {
            c_line[i] = seed_in() + u<32>(i + 10);
            cpp_line[i] = c_line[i] + u<32>(i + 20);
            array_comb = array_comb + cpp_line[i];
        }
        for (y = 0; y < 2; ++y) {
            for (x = 0; x < 3; ++x) {
                c_grid[y][x] = array_comb + u<32>(y * 10 + x);
                cpp_grid[y][x] = c_grid[y][x] + u<32>(100 + y * 10 + x);
                array_comb = array_comb + cpp_grid[y][x];
            }
        }
        return array_comb;
    }

    u<32>& early_comb_func()
    {
        early_comb = seed_in() + u<32>(1000);
        if (early_in()) {
            return early_comb = seed_in() + u<32>(2000);
        }
        early_comb = seed_in() + u<32>(3000);
        return early_comb;
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
#include <fstream>
#include <iostream>
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

static uint32_t expected_plain(uint32_t seed)
{
    return seed + 3u;
}

static uint32_t expected_array(uint32_t seed)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        result += seed + i + 10u + i + 20u;
    }
    for (uint32_t y = 0; y < 2; ++y) {
        for (uint32_t x = 0; x < 3; ++x) {
            uint32_t c = result + y * 10u + x;
            result += c + 100u + y * 10u + x;
        }
    }
    return result;
}

static uint32_t expected_early(bool early, uint32_t seed)
{
    return seed + (early ? 2000u : 3000u);
}

static size_t count_substr(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

static bool check_generated_sv()
{
#ifdef VERILATOR
    const std::filesystem::path sv_path = "CombFunc_1/CombFunc.sv";
#else
    const std::filesystem::path sv_path = "generated/CombFunc.sv";
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool ok = true;
    ok &= text.find("always_comb begin : plain_comb_func") != std::string::npos;
    ok &= text.find("always_comb begin : array_comb_func") != std::string::npos;
    ok &= text.find("always_comb begin : early_comb_func") != std::string::npos;
    ok &= text.find("disable plain_comb_func") == std::string::npos;
    ok &= text.find("disable array_comb_func") == std::string::npos;
    ok &= count_substr(text, "disable early_comb_func") == 1;

    if (!ok) {
        std::print("\nERROR: comb function disable generation check failed\n");
        std::print("       plain disables: {}, array disables: {}, early disables: {}\n",
            count_substr(text, "disable plain_comb_func"),
            count_substr(text, "disable array_comb_func"),
            count_substr(text, "disable early_comb_func"));
    }
    return ok;
}

class TestCombFunc : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    CombFunc dut;
#endif

    bool early = false;
    u<32> seed = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.early_in = _ASSIGN_REG(early);
        dut.seed_in = _ASSIGN_REG(seed);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.early_in = early;
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

    u<32> plain()
    {
#ifdef VERILATOR
        return verilator_read<u<32>>(&dut.plain_out);
#else
        return dut.plain_out();
#endif
    }

    u<32> array_value()
    {
#ifdef VERILATOR
        return verilator_read<u<32>>(&dut.array_out);
#else
        return dut.array_out();
#endif
    }

    u<32> early_value()
    {
#ifdef VERILATOR
        return verilator_read<u<32>>(&dut.early_out);
#else
        return dut.early_out();
#endif
    }

    bool check(bool early_value_in, uint32_t seed_value)
    {
        early = early_value_in;
        seed = seed_value;
        eval(false);
        neg(false);
        ++sys_clock;

        if (plain() != expected_plain(seed_value)) {
            std::print("\nplain ERROR: got {:08x}, expected {:08x}\n",
                (uint32_t)plain(), expected_plain(seed_value));
            return false;
        }
        if (array_value() != expected_array(seed_value)) {
            std::print("\narray ERROR: got {:08x}, expected {:08x}\n",
                (uint32_t)array_value(), expected_array(seed_value));
            return false;
        }
        if (early_value() != expected_early(early_value_in, seed_value)) {
            std::print("\nearly ERROR: got {:08x}, expected {:08x}\n",
                (uint32_t)early_value(), expected_early(early_value_in, seed_value));
            return false;
        }
        return true;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestCombFunc...");
#else
        std::print("CppHDL TestCombFunc...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "comb_func_test";
        _assign();

        error |= !check_generated_sv();
        for (uint32_t i = 0; i < 64 && !error; ++i) {
            error |= !check(false, 0x1000u + i * 3u);
            error |= !check(true, 0x2000u + i * 5u);
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
    ok &= check_generated_sv();
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "CombFunc", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("CombFunc_1/obj_dir/VCombFunc") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestCombFunc().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

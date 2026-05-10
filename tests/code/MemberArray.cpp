#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

class MemberArrayLeaf : public Module
{
public:
    __PORT(u<16>) base_in;
    __PORT(u<16>) add_in;
    __PORT(u<16>) value_out = __VAR(value_comb_func());

private:
    u<16> value_comb;

    u<16>& value_comb_func()
    {
        return value_comb = base_in() + add_in();
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

class MemberArray : public Module
{
public:
    __PORT(u<16>) seed_in;
    __PORT(u<16>) result_out = __VAR(result_comb_func());

private:
    MemberArrayLeaf line[3];
    MemberArrayLeaf grid[2][3];
    MemberArrayLeaf cube[2][2][2];

    u<16> result_comb;

    u<16>& result_comb_func()
    {
        return result_comb = line[2].value_out()
            + grid[1][2].value_out()
            + cube[1][1][1].value_out();
    }

public:
    void _work(bool reset) {}
    void _strobe() {}

    void _assign()
    {
        line[0].base_in = seed_in;
        line[0].add_in = __EXPR(u<16>(1));
        line[0]._assign();
        line[1].base_in = line[0].value_out;
        line[1].add_in = __EXPR(u<16>(2));
        line[1]._assign();
        line[2].base_in = line[1].value_out;
        line[2].add_in = __EXPR(u<16>(3));
        line[2]._assign();

        grid[0][0].base_in = line[1].value_out;
        grid[0][0].add_in = __EXPR(u<16>(10));
        grid[0][0]._assign();
        grid[0][1].base_in = grid[0][0].value_out;
        grid[0][1].add_in = __EXPR(u<16>(11));
        grid[0][1]._assign();
        grid[0][2].base_in = grid[0][1].value_out;
        grid[0][2].add_in = __EXPR(u<16>(12));
        grid[0][2]._assign();

        grid[1][0].base_in = line[2].value_out;
        grid[1][0].add_in = __EXPR(u<16>(20));
        grid[1][0]._assign();
        grid[1][1].base_in = grid[1][0].value_out;
        grid[1][1].add_in = __EXPR(u<16>(21));
        grid[1][1]._assign();
        grid[1][2].base_in = grid[1][1].value_out;
        grid[1][2].add_in = __EXPR(u<16>(22));
        grid[1][2]._assign();

        cube[0][0][0].base_in = grid[0][1].value_out;
        cube[0][0][0].add_in = __EXPR(u<16>(100));
        cube[0][0][0]._assign();
        cube[0][0][1].base_in = cube[0][0][0].value_out;
        cube[0][0][1].add_in = __EXPR(u<16>(101));
        cube[0][0][1]._assign();
        cube[0][1][0].base_in = grid[0][2].value_out;
        cube[0][1][0].add_in = __EXPR(u<16>(110));
        cube[0][1][0]._assign();
        cube[0][1][1].base_in = cube[0][1][0].value_out;
        cube[0][1][1].add_in = __EXPR(u<16>(111));
        cube[0][1][1]._assign();

        cube[1][0][0].base_in = grid[1][1].value_out;
        cube[1][0][0].add_in = __EXPR(u<16>(200));
        cube[1][0][0]._assign();
        cube[1][0][1].base_in = cube[1][0][0].value_out;
        cube[1][0][1].add_in = __EXPR(u<16>(201));
        cube[1][0][1]._assign();
        cube[1][1][0].base_in = grid[1][2].value_out;
        cube[1][1][0].add_in = __EXPR(u<16>(210));
        cube[1][1][0]._assign();
        cube[1][1][1].base_in = cube[1][1][0].value_out;
        cube[1][1][1].add_in = __EXPR(u<16>(211));
        cube[1][1][1]._assign();
    }
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

static uint16_t expected(uint16_t seed)
{
    uint16_t line[3] = {};
    for (size_t i = 0; i < 3; ++i) {
        line[i] = (i == 0 ? seed : line[i - 1]) + uint16_t(i + 1);
    }

    uint16_t grid[2][3] = {};
    for (size_t y = 0; y < 2; ++y) {
        for (size_t x = 0; x < 3; ++x) {
            grid[y][x] = (x == 0 ? line[y + 1] : grid[y][x - 1]) + uint16_t((y + 1) * 10 + x);
        }
    }

    uint16_t cube[2][2][2] = {};
    for (size_t z = 0; z < 2; ++z) {
        for (size_t y = 0; y < 2; ++y) {
            for (size_t x = 0; x < 2; ++x) {
                cube[z][y][x] = (x == 0 ? grid[z][y + 1] : cube[z][y][x - 1]) + uint16_t((z + 1) * 100 + y * 10 + x);
            }
        }
    }

    return uint16_t(line[2] + grid[1][2] + cube[1][1][1]);
}

static bool generated_sv_has_member_arrays()
{
#ifdef VERILATOR
    const std::filesystem::path sv_path = "MemberArray_1/MemberArray.sv";
#else
    const std::filesystem::path sv_path = "generated/MemberArray.sv";
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool has_1d = text.find("line__base_in[3]") != std::string::npos;
    bool has_2d = text.find("grid__base_in[2][3]") != std::string::npos;
    bool has_3d = text.find("cube__base_in[2][2][2]") != std::string::npos;
    bool has_3d_index = text.find(".base_in(cube__base_in[__i][__j][__k])") != std::string::npos;

    if (!has_1d || !has_2d || !has_3d || !has_3d_index) {
        std::print("\nERROR: generated SV member arrays are incomplete: 1d={}, 2d={}, 3d={}, 3d_index={}\n",
            has_1d, has_2d, has_3d, has_3d_index);
        return false;
    }
    return true;
}

class TestMemberArray : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    MemberArray dut;
#endif

    u<16> seed;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = __VAR(seed);
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

    uint16_t result()
    {
#ifdef VERILATOR
        return dut.result_out;
#else
        return static_cast<uint16_t>(dut.result_out());
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestMemberArray...");
#else
        std::print("CppHDL TestMemberArray...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "member_array_test";
        _assign();

        error |= !generated_sv_has_member_arrays();

        for (uint32_t i = 0; i < 128 && !error; ++i) {
            seed = u<16>(i * 17 + 3);
            eval(false);
            uint16_t got = result();
            uint16_t ref = expected(static_cast<uint16_t>(seed));
            if (got != ref) {
                std::print("\nresult ERROR at {}: got {}, expected {}\n", i, got, ref);
                error = true;
            }
            neg(false);
            ++sys_clock;
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
        ok &= VerilatorCompile(__FILE__, "MemberArray", {"Predef_pkg", "MemberArrayLeaf"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("MemberArray_1/obj_dir/VMemberArray") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestMemberArray().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

class ImplicitWorkLeaf : public Module
{
public:
    _PORT(u<16>) value_in;
    _PORT(u<16>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    u<16> value_comb;

    u<16>& value_comb_func()
    {
        value_comb = value_in() + u<16>(0x1234);
        return value_comb;
    }

public:
    void _strobe() {}
    void _assign() {}
};

class ImplicitWork : public Module
{
public:
    _PORT(u<16>) value_in;
    _PORT(u<16>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    ImplicitWorkLeaf leaf;
    u<16> value_comb;

    u<16>& value_comb_func()
    {
        value_comb = leaf.value_out() ^ u<16>(0x00ff);
        return value_comb;
    }

public:
    void _work(bool) {}

    void _strobe() {}

    void _assign()
    {
        leaf.value_in = value_in;
        leaf._assign();
    }
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static uint16_t expected_implicit_work(uint16_t value)
{
    return (uint16_t)((value + 0x1234u) ^ 0x00ffu);
}

static bool check_generated_sv()
{
    std::filesystem::path leaf_path = "generated/ImplicitWorkLeaf.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(leaf_path)) {
        leaf_path = "ImplicitWork_1/ImplicitWorkLeaf.sv";
    }
#endif

    std::ifstream in(leaf_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", leaf_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool ok = true;
    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    require(text.find("task _work (input logic reset);") != std::string::npos,
        "module without C++ _work() did not get an implicit SV _work task");
    require(text.find("begin: _work") != std::string::npos,
        "implicit SV _work task has no empty body");
    require(text.find("_work(reset);") != std::string::npos,
        "module clock block does not call _work(reset)");
    require(text.find("unknown(") == std::string::npos,
        "generated SV contains an unknown call");
    if (!ok) {
        std::print("\nERROR: implicit _work generation check failed in {}\n", leaf_path.string());
    }
    return ok;
}

class TestImplicitWork : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    ImplicitWork dut;
#endif

    u<16> value = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.value_in = _ASSIGN_REG(value);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.value_in = (uint16_t)value;
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

    uint16_t output()
    {
#ifdef VERILATOR
        return (uint16_t)dut.value_out;
#else
        return (uint16_t)dut.value_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestImplicitWork...");
#else
        std::print("CppHDL TestImplicitWork...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "implicit_work_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 1024 && !error; ++i) {
            uint16_t sample = (uint16_t)((i * 211u) ^ (i << 3) ^ 0x55aau);
            value = u<16>(sample);
            eval(false);
            uint16_t got = output();
            uint16_t expected = expected_implicit_work(sample);
            if (got != expected) {
                std::print("\nimplicit _work ERROR sample=0x{:04x}: got=0x{:04x} expected=0x{:04x}\n",
                    sample, got, expected);
                error = true;
            }
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
        ok &= VerilatorCompile(__FILE__, "ImplicitWork", {"Predef_pkg", "ImplicitWorkLeaf"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("ImplicitWork_1/obj_dir/VImplicitWork") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestImplicitWork().run();
    return ok ? 0 : 1;
}

#endif

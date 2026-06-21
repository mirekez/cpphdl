#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

template<int WIDTH = 4>
class ConstexprPortsizeChild : public Module
{
public:
    static constexpr int AAA = WIDTH;

    _PORT(u1) bit_in[AAA];
    _PORT(u<8>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    u<8> value_comb;
    u<8> reset_seen_reg;

    u<8>& value_comb_func()
    {
        return value_comb = u<8>(AAA) + reset_seen_reg;
    }

public:
    void _work(bool reset)
    {
        if (reset) {
            reset_seen_reg = 0;
        }
    }
    void _strobe() {}
    void _assign() {}
};

class ConstexprPortsize : public Module
{
public:
    _PORT(u<8>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    ConstexprPortsizeChild<4> child;
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = child.value_out();
    }

public:
    void _work(bool reset)
    {
        child._work(reset);
    }
    void _strobe() {}

    void _assign()
    {
        child._assign();
    }
};

/////////////////////////////////////////////////////////////////////////

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

long _system_clock = -1;

static bool check_generated_sv()
{
#ifdef VERILATOR
    const std::filesystem::path parent_path = "ConstexprPortsize_1/ConstexprPortsize.sv";
    const std::filesystem::path child_path = "ConstexprPortsize_1/ConstexprPortsizeChild.sv";
#else
    const std::filesystem::path parent_path = "generated/ConstexprPortsize.sv";
    const std::filesystem::path child_path = "generated/ConstexprPortsizeChild.sv";
#endif

    std::ifstream parent_in(parent_path);
    std::ifstream child_in(child_path);
    if (!parent_in || !child_in) {
        std::print("\nERROR: can't open generated SystemVerilog files {} and {}\n",
            parent_path.string(), child_path.string());
        return false;
    }

    const std::string parent((std::istreambuf_iterator<char>(parent_in)), std::istreambuf_iterator<char>());
    const std::string child((std::istreambuf_iterator<char>(child_in)), std::istreambuf_iterator<char>());

    bool ok = true;
    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    require(child.find("parameter WIDTH") != std::string::npos
            || child.find("parameter  WIDTH") != std::string::npos,
        "child template parameter WIDTH was not emitted");
    require(child.find("parameter  AAA") != std::string::npos, "child static constexpr AAA was not emitted");
    require(parent.find("[AAA]") == std::string::npos, "parent leaked child constexpr AAA as an undefined local name");
    require(parent.find("child__bit_in[4]") != std::string::npos,
        "parent did not emit a resolved child bit_in wire array size");
    require(parent.find(".bit_in(child__bit_in)") != std::string::npos,
        "parent did not connect the child bit_in array");

    return ok;
}

class TestConstexprPortsize : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    ConstexprPortsize dut;
#endif

    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
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

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestConstexprPortsize...");
#else
        std::print("CppHDL TestConstexprPortsize...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "constexpr_portsize_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 2 && !error; ++i) {
            eval(false);
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
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "ConstexprPortsize", {"Predef_pkg", "ConstexprPortsizeChild"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("ConstexprPortsize_1/obj_dir/VConstexprPortsize") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestConstexprPortsize().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

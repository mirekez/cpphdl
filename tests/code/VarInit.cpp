#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <print>

using namespace cpphdl;

class VarInit : public Module
{
public:
    _PORT(logic<1>) ready_out = _ASSIGN_COMB(ready_comb_func());
    _PORT(logic<1>) ready1_out = _ASSIGN_COMB(ready_comb1_func());

private:
    _LAZY_COMB(ready_comb, logic<1>)
        ready_comb = {};
        return ready_comb;
    }

    _LAZY_COMB(ready_comb1, logic<1>)
        ready_comb1 = {0};
        return ready_comb1;
    }

public:
    void _assign() {}
    void _work(bool reset) {}
    void _strobe() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cctype>
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

template<typename T>
static T verilator_read(const void* ptr)
{
    T value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

static bool check_generated_sv()
{
    std::filesystem::path sv_path = "generated/VarInit.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(sv_path)) {
        sv_path = "VarInit/VarInit.sv";
    }
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string compact;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            compact += c;
        }
    }

    bool ok = true;
    ok &= text.find("always_comb begin : ready_comb_func") != std::string::npos;
    ok &= text.find("always_comb begin : ready_comb1_func") != std::string::npos;
    ok &= compact.find("ready_comb=0;") != std::string::npos ||
          compact.find("ready_comb='0;") != std::string::npos ||
          compact.find("ready_comb=1'h0;") != std::string::npos ||
          compact.find("ready_comb='h0;") != std::string::npos;
    ok &= compact.find("ready_comb1=0;") != std::string::npos ||
          compact.find("ready_comb1='h0;") != std::string::npos ||
          compact.find("ready_comb1=1'h0;") != std::string::npos ||
          compact.find("ready_comb1='0;") != std::string::npos;
    ok &= compact.find("assignready_out=ready_comb;") != std::string::npos;
    ok &= compact.find("assignready1_out=ready_comb1;") != std::string::npos;
    if (!ok) {
        std::print("\nERROR: lazy comb aggregate-init generation check failed in {}\n", sv_path.string());
    }
    return ok;
}

class TestVarInit : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    VarInit dut;
#endif

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
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
#else
        dut._work(reset);
#endif
    }

    logic<1> ready()
    {
#ifdef VERILATOR
        return logic<1>(verilator_read<uint8_t>(&dut.ready_out));
#else
        return dut.ready_out();
#endif
    }

    logic<1> ready1()
    {
#ifdef VERILATOR
        return logic<1>(verilator_read<uint8_t>(&dut.ready1_out));
#else
        return dut.ready1_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestVarInit...");
#else
        std::print("CppHDL TestVarInit...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        bool ok = true;
        __inst_name = "var_init_test";
        _assign();

        for (size_t i = 0; i < 4; ++i) {
            eval(false);
            ok &= ready() == logic<1>(0);
            ok &= ready1() == logic<1>(0);
            ++_system_clock;
        }

        std::print(" {} ({} us)\n", ok ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return ok;
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
    ok &= check_generated_sv();
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "VarInit", {"Predef_pkg"}, {"../../../../include"});
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("VarInit/obj_dir/VVarInit") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestVarInit().run());
}

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

struct TemplateModuleNameConv
{
    static constexpr uint16_t MASK = 0x1234;
};

template<typename CONV_TYPE = TemplateModuleNameConv>
class Arithmetic
{
public:
    static constexpr uint16_t mask()
    {
        return CONV_TYPE::MASK;
    }
};

template<int PARAM1 = 0, int PARAM2 = 0>
class [[clang::annotate(
    "CPPHDL_REPLACEMENT="
    "`default_nettype none\n"
    "\n"
    "import Predef_pkg::*;\n"
    "\n"
    "module TemplateModuleName_Arithmetic #(parameter int PARAM1 = $(PARAM1), parameter int PARAM2 = $(PARAM2))\n"
    " (\n"
    "    input wire clk,\n"
    "    input wire reset,\n"
    "    input wire[16-1:0] value_in,\n"
    "    output wire[16-1:0] value_out\n"
    ");\n"
    "    assign value_out = value_in ^ 16'(PARAM1 + PARAM2 + 16'h1234);\n"
    "endmodule\n"
    ";"
)]] TemplateModuleName_Arithmetic : public Module
{
public:
    _PORT(logic<16>) value_in;
    _PORT(logic<16>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    logic<16> value_comb;

    logic<16>& value_comb_func()
    {
        value_comb = value_in() ^ logic<16>((uint16_t)(PARAM1 + PARAM2 + Arithmetic<>::mask()));
        return value_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class TemplateModuleName : public Module
{
public:
    _PORT(logic<16>) value_in;
    _PORT(logic<16>) value_out = _ASSIGN(arithmetic.value_out());

private:
    TemplateModuleName_Arithmetic<0, 0> arithmetic;

public:
    void _assign()
    {
        arithmetic.value_in = value_in;
        arithmetic._assign();
    }

    void _work(bool reset)
    {
        arithmetic._work(reset);
    }

    void _strobe()
    {
        arithmetic._strobe();
    }
};

template class TemplateModuleName_Arithmetic<0, 0>;

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

static bool check_generated_sv()
{
    std::filesystem::path sv_path = "generated/TemplateModuleName_Arithmetic.sv";
    std::filesystem::path top_path = "generated/TemplateModuleName.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(sv_path)) {
        sv_path = "TemplateModuleName_1/TemplateModuleName_Arithmetic.sv";
        top_path = "TemplateModuleName_1/TemplateModuleName.sv";
    }
#endif

    std::ifstream sv_in(sv_path);
    std::ifstream top_in(top_path);
    if (!sv_in || !top_in) {
        std::print("\nERROR: can't open generated SystemVerilog files {} and {}\n",
            sv_path.string(), top_path.string());
        return false;
    }

    std::string sv((std::istreambuf_iterator<char>(sv_in)), std::istreambuf_iterator<char>());
    std::string top((std::istreambuf_iterator<char>(top_in)), std::istreambuf_iterator<char>());
    bool ok = true;
    ok &= sv.find("module TemplateModuleName_Arithmetic #(") != std::string::npos;
    ok &= sv.find("module TemplateModuleNameArithmetic") == std::string::npos;
    ok &= top.find("TemplateModuleName_Arithmetic #(") != std::string::npos;
    ok &= top.find("TemplateModuleNameArithmetic #(") == std::string::npos;
    if (!ok) {
        std::print("\nERROR: template module name underscore was not preserved\n");
    }
    return ok;
}

class TestTemplateModuleName : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateModuleName dut;
#endif

    logic<16> value = 0;
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
        std::print("VERILATOR TestTemplateModuleName...");
#else
        std::print("CppHDL TestTemplateModuleName...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "template_module_name_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 1024 && !error; ++i) {
            value = logic<16>((uint16_t)((i * 109u) ^ 0x4b31u));
            eval(false);
            uint16_t got = output();
            uint16_t expected = (uint16_t)value ^ 0x1234u;
            if (got != expected) {
                std::print("\ntemplate module name ERROR sample={}: got=0x{:04x} expected=0x{:04x}\n",
                    i, got, expected);
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
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "TemplateModuleName", {
            "Predef_pkg",
            "TemplateModuleName_Arithmetic"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("TemplateModuleName_1/obj_dir/VTemplateModuleName") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestTemplateModuleName().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

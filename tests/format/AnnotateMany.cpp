#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"

using namespace cpphdl;

class [[clang::annotate(
    "CPPHDL_REPLACEMENT="
    "`default_nettype none\n"
    "\n"
    "module AnnotateManyFirstHelper (\n"
    "    input wire[8-1:0] value_in\n"
    ",   output wire[8-1:0] value_out\n"
    ");\n"
    "    assign value_out = value_in ^ 8'h11;\n"
    "endmodule\n"
    "\n"
    "module AnnotateManyFirst (\n"
    "    input wire clk\n"
    ",   input wire reset\n"
    ",   input wire[8-1:0] value_in\n"
    ",   output wire[8-1:0] value_out\n"
    ");\n"
    "    AnnotateManyFirstHelper helper(.value_in(value_in), .value_out(value_out));\n"
    "endmodule\n"
    ";"
)]] AnnotateManyFirst : public Module
{
public:
    _PORT(u<8>) value_in;
    _PORT(u<8>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0x11);
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class [[clang::annotate(
    "CPPHDL_REPLACEMENT="
    "`default_nettype none\n"
    "\n"
    "module AnnotateManySecondHelper (\n"
    "    input wire[8-1:0] value_in\n"
    ",   output wire[8-1:0] value_out\n"
    ");\n"
    "    assign value_out = value_in ^ 8'h22;\n"
    "endmodule\n"
    "\n"
    "module AnnotateManySecond (\n"
    "    input wire clk\n"
    ",   input wire reset\n"
    ",   input wire[8-1:0] value_in\n"
    ",   output wire[8-1:0] value_out\n"
    ");\n"
    "    AnnotateManySecondHelper helper(.value_in(value_in), .value_out(value_out));\n"
    "endmodule\n"
    ";"
)]] AnnotateManySecond : public Module
{
public:
    _PORT(u<8>) value_in;
    _PORT(u<8>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0x22);
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class AnnotateManyParent : public Module
{
public:
    _PORT(u<8>) value_in;
    _PORT(u<8>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    AnnotateManyFirst first;
    AnnotateManySecond second;
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = first.value_out() ^ second.value_out();
    }

public:
    void _assign()
    {
        first.value_in = value_in;
        first._assign();
        second.value_in = value_in;
        second._assign();
    }

    void _work(bool reset)
    {
        first._work(reset);
        second._work(reset);
    }

    void _strobe()
    {
        first._strobe();
        second._strobe();
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

static bool file_contains(const std::filesystem::path& path, const std::string& text)
{
    std::ifstream in(path);
    if (!in) {
        std::print("\nERROR: can't open {}\n", path.string());
        return false;
    }
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (body.find(text) == std::string::npos) {
        std::print("\nERROR: {} does not contain '{}'\n", path.string(), text);
        return false;
    }
    return true;
}

static bool check_generated_sv()
{
    std::filesystem::path first = "generated/AnnotateManyFirst.sv";
    std::filesystem::path second = "generated/AnnotateManySecond.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(first)) {
        first = "AnnotateManyParent_1/AnnotateManyFirst.sv";
        second = "AnnotateManyParent_1/AnnotateManySecond.sv";
    }
#endif

    bool ok = true;
    ok &= file_contains(first, "module AnnotateManyFirstHelper");
    ok &= file_contains(first, "module AnnotateManyFirst (");
    ok &= file_contains(second, "module AnnotateManySecondHelper");
    ok &= file_contains(second, "module AnnotateManySecond (");
    return ok;
}

class TestAnnotateMany : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    AnnotateManyParent dut;
#endif

    u<8> value = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.value_in = _ASSIGN_REG(value);
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.value_in = (uint8_t)value;
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

    uint8_t output()
    {
#ifdef VERILATOR
        return (uint8_t)dut.value_out;
#else
        return (uint8_t)dut.value_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestAnnotateMany...");
#else
        std::print("CppHDL TestAnnotateMany...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        _assign();

        error |= !check_generated_sv();

        for (unsigned i = 0; i < 256 && !error; ++i) {
            value = u<8>(i);
            eval(false);
            uint8_t got = output();
            uint8_t expected = 0x33;
            if (got != expected) {
                std::print("\nannotate many ERROR sample={}: got=0x{:02x} expected=0x{:02x}\n",
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
        ok &= VerilatorCompile(__FILE__, "AnnotateManyParent", {
            "Predef_pkg",
            "AnnotateManyFirst",
            "AnnotateManySecond"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("AnnotateManyParent_1/obj_dir/VAnnotateManyParent") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestAnnotateMany().run();
    return !ok;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

class [[clang::annotate(
    "CPPHDL_REPLACEMENT="
    "`default_nettype none\n"
    "\n"
    "module AnnotateReplacement (\n"
    "    input wire clk\n"
    ",   input wire reset\n"
    ",   input wire[8-1:0] value_in\n"
    ",   output wire[8-1:0] value_out\n"
    ");\n"
    "    // CPPHDL_ANNOTATE_REPLACEMENT_MARKER\n"
    "    assign value_out = value_in ^ 8'hA5;\n"
    "endmodule\n"
    ";"
)]] AnnotateReplacement : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0xa5);
    }

public:
    void _work(bool reset) {}
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
#include <sstream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

#ifdef VERILATOR
static bool generated_sv_is_replacement()
{
    const std::filesystem::path sv_path = "AnnotateReplacement_1/AnnotateReplacement.sv";
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool has_marker = text.find("CPPHDL_ANNOTATE_REPLACEMENT_MARKER") != std::string::npos;
    bool has_normal_import = text.find("import Predef_pkg::*;") != std::string::npos;
    bool has_cpphdl_regs_comment = text.find("// regs and combs") != std::string::npos;

    if (!has_marker || has_normal_import || has_cpphdl_regs_comment) {
        std::print("\nERROR: generated SV was not replaced: marker={}, import={}, regs_comment={}\n",
            has_marker, has_normal_import, has_cpphdl_regs_comment);
        return false;
    }
    return true;
}
#endif

class TestAnnotateReplacement : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    AnnotateReplacement dut;
#endif

    u<8> value;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.value_in = __VAR(value);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.value_in = value;
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

    u<8> value_out()
    {
#ifdef VERILATOR
        return u<8>(dut.value_out);
#else
        return dut.value_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestAnnotateReplacement...");
#else
        std::print("CppHDL TestAnnotateReplacement...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "annotate_replacement_test";
        _assign();

#ifdef VERILATOR
        error |= !generated_sv_is_replacement();
#endif

        for (uint32_t i = 0; i < 256 && !error; ++i) {
            value = u<8>(i);
            eval(false);
            u<8> expected = u<8>(i ^ 0xa5);
            if (value_out() != expected) {
                std::print("\nvalue ERROR at {}: got {}, expected {}\n", i, value_out(), expected);
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
        ok &= VerilatorCompile(__FILE__, "AnnotateReplacement", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("AnnotateReplacement_1/obj_dir/VAnnotateReplacement") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestAnnotateReplacement().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

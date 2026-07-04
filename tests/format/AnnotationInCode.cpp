#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include "../../examples/tools.h"

using namespace cpphdl;

template <size_t WIDTH = 8>
class AnnotationInCode : public Module
{
public:
    _PORT(logic<WIDTH>) value_in;
    _PORT(logic<WIDTH>) value_out = _ASSIGN_REG(q_out_reg);

private:
    // (* preserve, dont_merge *)
    // (* altera_attribute = "-name PRESERVE_REGISTER ON; -name DONT_MERGE_REGISTER ON" *)
    reg<logic<WIDTH>> q_out_reg;

    // This comment must not be emitted as an SV annotation.
    reg<logic<WIDTH>> unannotated_reg;

public:
    void _work(bool reset)
    {
        q_out_reg._next = value_in();
        unannotated_reg._next = value_in();

        if (reset) {
            q_out_reg._next = 0;
            unannotated_reg._next = 0;
        }
    }

    void _strobe()
    {
        q_out_reg.strobe();
        unannotated_reg.strobe();
    }

    void _assign() {}
};

template class AnnotationInCode<13>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static std::filesystem::path generated_sv_path()
{
    std::filesystem::path path = "generated/AnnotationInCode.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(path)) {
        path = "AnnotationInCode_13/AnnotationInCode.sv";
    }
#endif
    return path;
}

static bool check_generated_sv()
{
    std::ifstream in(generated_sv_path());
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", generated_sv_path().string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string expected =
        "    (* preserve, dont_merge *)\n"
        "    (* altera_attribute = \"-name PRESERVE_REGISTER ON; -name DONT_MERGE_REGISTER ON\" *)\n";
    const size_t attr_pos = text.find(expected);
    const size_t reg_pos = text.find("q_out_reg", attr_pos == std::string::npos ? 0 : attr_pos);
    const size_t decl_pos = text.find("logic[WIDTH-1:0] q_out_reg;", attr_pos == std::string::npos ? 0 : attr_pos);
    const size_t duplicate_attr_pos = attr_pos == std::string::npos
        ? std::string::npos
        : text.find("(* preserve, dont_merge *)", attr_pos + expected.size());
    const size_t plain_pos = text.find("This comment must not be emitted");

    bool ok = attr_pos != std::string::npos && reg_pos != std::string::npos && decl_pos != std::string::npos
        && attr_pos < reg_pos && duplicate_attr_pos == std::string::npos
        && plain_pos == std::string::npos;
    if (!ok) {
        std::print("\nERROR: in-code annotation was not emitted before q_out_reg in {}\n", generated_sv_path().string());
    }
    return ok;
}

class TestAnnotationInCode : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    AnnotationInCode<13> dut;
#endif

    logic<13> value = 0;
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

    void cycle(bool reset = false)
    {
#ifdef VERILATOR
        dut.value_in = (uint16_t)value;
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
        dut.clk = 1;
        dut.eval();
        dut.clk = 0;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
#endif
        ++_system_clock;
    }

    logic<13> output()
    {
#ifdef VERILATOR
        return logic<13>((uint16_t)dut.value_out);
#else
        return dut.value_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestAnnotationInCode...");
#else
        std::print("CppHDL TestAnnotationInCode...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "annotation_in_code_test";
        _assign();

        error |= !check_generated_sv();
        cycle(true);

        for (uint32_t i = 0; i < 64 && !error; ++i) {
            uint16_t sample = (uint16_t)(((i * 37u) ^ 0x1555u) & 0x1fffu);
            value = logic<13>(sample);
            cycle(false);
            uint16_t got = (uint16_t)output();
            if (got != sample) {
                std::print("\nannotation register ERROR sample=0x{:04x}: got=0x{:04x}\n", sample, got);
                error = true;
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
        ok &= VerilatorCompile(__FILE__, "AnnotationInCode", {"Predef_pkg"}, {"../../../../include"}, 13);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("AnnotationInCode_13/obj_dir/VAnnotationInCode") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestAnnotationInCode().run();
    return !ok;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

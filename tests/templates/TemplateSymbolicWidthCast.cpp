#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

template<size_t WIDTH = 3>
class TemplateSymbolicWidthCast : public Module
{
public:
    _PORT(bool) zero_in;
    _PORT(u<WIDTH>) data_in;
    _PORT(u<WIDTH>) data_out = _ASSIGN_COMB(data_comb_func());

private:
    u<WIDTH> data_comb;

    u<WIDTH>& data_comb_func()
    {
        data_comb = zero_in() ? u<WIDTH>(0) : u<WIDTH>(data_in() + 1);
        return data_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template class TemplateSymbolicWidthCast<3>;

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
    std::filesystem::path path = "generated/TemplateSymbolicWidthCast.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(path)) {
        path = "TemplateSymbolicWidthCast/TemplateSymbolicWidthCast.sv";
    }
#endif

    std::ifstream in(path);
    if (!in) {
        std::print("\nERROR: can't open {}\n", path.string());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("WIDTH'h0") != std::string::npos
        || text.find("WIDTH'('h0)") == std::string::npos) {
        std::print("\nERROR: symbolic-width zero cast is invalid in {}\n", path.string());
        return false;
    }
    return true;
}

class TestTemplateSymbolicWidthCast : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateSymbolicWidthCast<3> dut;
#endif

    bool zero = false;
    u<3> data;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.zero_in = _ASSIGN_REG(zero);
        dut.data_in = _ASSIGN_REG(data);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    uint8_t output()
    {
#ifdef VERILATOR
        return (uint8_t)dut.data_out;
#else
        return (uint8_t)dut.data_out();
#endif
    }

    bool run()
    {
        __inst_name = "template_symbolic_width_cast_test";
        _assign();
        error |= !check_generated_sv();

        data = 5;
        zero = false;
        ++_system_clock;
#ifdef VERILATOR
        dut.data_in = (uint8_t)data;
        dut.zero_in = zero;
        dut.eval();
#endif
        error |= output() != 6;

        zero = true;
        ++_system_clock;
#ifdef VERILATOR
        dut.zero_in = zero;
        dut.eval();
#endif
        error |= output() != 0;
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
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "TemplateSymbolicWidthCast", {"Predef_pkg"}, {"../../../../include"});
        auto compile_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        ok = ok && std::system("TemplateSymbolicWidthCast/obj_dir/VTemplateSymbolicWidthCast") == 0;
        std::print("Verilator compilation time: {} microseconds\n", compile_us);
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestTemplateSymbolicWidthCast().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

template<size_t WIDTH>
struct TemplateParameterizedIf : public Interface
{
    _PORT(logic<WIDTH>) data_out;
};

template<size_t WIDTH = 32>
class TemplateInterfaceLeaf : public Module
{
public:
    TemplateParameterizedIf<WIDTH> mem_out;
    _PORT(logic<WIDTH>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    logic<WIDTH> value_comb;

    logic<WIDTH>& value_comb_func()
    {
        value_comb = mem_out.data_out();
        return value_comb;
    }

public:

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class TemplateInterfaceParameter : public Module
{
public:
    _PORT(logic<64>) value_in;
    _PORT(logic<64>) value_out = _ASSIGN(leaf.value_out());

private:
    TemplateInterfaceLeaf<64> leaf;

public:
    void _work(bool reset)
    {
        leaf._work(reset);
    }

    void _strobe()
    {
        leaf._strobe();
    }

    void _assign()
    {
        leaf.mem_out.data_out = value_in;
        leaf._assign();
    }
};

template class TemplateInterfaceLeaf<64>;

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
    std::filesystem::path leaf_path = "generated/TemplateInterfaceLeaf.sv";
    std::filesystem::path top_path = "generated/TemplateInterfaceParameter.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(top_path)) {
        leaf_path = "TemplateInterfaceParameter/TemplateInterfaceLeaf.sv";
        top_path = "TemplateInterfaceParameter/TemplateInterfaceParameter.sv";
    }
#endif

    std::ifstream leaf_in(leaf_path);
    std::ifstream top_in(top_path);
    if (!leaf_in || !top_in) {
        std::print("\nERROR: can't open generated parameterized Interface files\n");
        return false;
    }
    std::string leaf((std::istreambuf_iterator<char>(leaf_in)), std::istreambuf_iterator<char>());
    std::string top((std::istreambuf_iterator<char>(top_in)), std::istreambuf_iterator<char>());
    bool ok = true;
    if (leaf.find("input wire[WIDTH-1:0] mem_out__data_in") == std::string::npos
        || leaf.find("input wire[32-1:0] mem_out__data_in") != std::string::npos) {
        std::print("\nERROR: Interface sub-port did not retain symbolic module width\n");
        ok = false;
    }
    if (top.find("wire[64-1:0] leaf__mem_out__data_in;") == std::string::npos) {
        std::print("\nERROR: parent Interface wire did not resolve child width to 64\n");
        ok = false;
    }
    return ok;
}

class TestTemplateInterfaceParameter : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateInterfaceParameter dut;
#endif

    logic<64> value;
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

    uint64_t output()
    {
#ifdef VERILATOR
        return (uint64_t)dut.value_out;
#else
        return (uint64_t)dut.value_out();
#endif
    }

    bool run()
    {
        __inst_name = "template_interface_parameter_test";
        _assign();
        error |= !check_generated_sv();
        value = logic<64>(0x123456789abcdef0ull);
        ++_system_clock;
#ifdef VERILATOR
        dut.value_in = (uint64_t)value;
        dut.eval();
#endif
        error |= output() != 0x123456789abcdef0ull;
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
        ok &= VerilatorCompile(__FILE__, "TemplateInterfaceParameter",
            {"Predef_pkg", "TemplateInterfaceLeaf"}, {"../../../../include"});
        auto compile_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        ok = ok && std::system("TemplateInterfaceParameter/obj_dir/VTemplateInterfaceParameter") == 0;
        std::print("Verilator compilation time: {} microseconds\n", compile_us);
    }
#else
    Verilated::commandArgs(argc, argv);
#endif
    return !(ok && TestTemplateInterfaceParameter().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

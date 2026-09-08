#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

class BoolConversion : public Module
{
public:
    _PORT(uint8_t) value_in;
    _PORT(bool) implicit_out = _ASSIGN(value_in());
    _PORT(bool) explicit_out = _ASSIGN((bool)value_in());
    _PORT(bool) assigned_out;
    _PORT(bool) comb_out = _ASSIGN_COMB(converted_comb_func());
    _PORT(bool) registered_out = _ASSIGN_REG(registered_value);

private:
    bool converted_comb = false;
    bool registered_value = false;

    bool& converted_comb_func()
    {
        return converted_comb = value_in();
    }

public:
    void _work(bool reset)
    {
        registered_value = value_in();
        if (reset) {
            registered_value = false;
        }
    }

    void _strobe() {}
    void _assign()
    {
        assigned_out = _ASSIGN(value_in());
    }
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstdio>
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
    const std::filesystem::path path =
        VerilatorGeneratedDir(__FILE__, "BoolConversion") / "BoolConversion.sv";
    std::ifstream input(path);
    if (!input) {
        std::printf("ERROR: can't open generated SystemVerilog file %s\n", path.c_str());
        return false;
    }

    const std::string text((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const bool ok =
        text.find("converted_comb=((value_in) != '0);") != std::string::npos
        && text.find("registered_value=((value_in) != '0);") != std::string::npos
        && text.find("assign implicit_out = ((value_in) != '0);") != std::string::npos
        && text.find("assign explicit_out = ((value_in) != '0);") != std::string::npos
        && text.find("assign assigned_out = ((value_in) != '0);") != std::string::npos;
    if (!ok) {
        std::printf("ERROR: generated SystemVerilog does not preserve C++ bool conversion in %s\n",
            path.c_str());
    }
    return ok;
}

class TestBoolConversion
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    BoolConversion dut;
    uint8_t input_value = 0;
#endif
    bool error = false;

    void check_comb(uint8_t value)
    {
        bool expected = value != 0;
#ifdef VERILATOR
        bool implicit_value = dut.implicit_out;
        bool explicit_value = dut.explicit_out;
        bool assigned_value = dut.assigned_out;
        bool comb_value = dut.comb_out;
#else
        bool implicit_value = dut.implicit_out();
        bool explicit_value = dut.explicit_out();
        bool assigned_value = dut.assigned_out();
        bool comb_value = dut.comb_out();
#endif
        if (implicit_value != expected || explicit_value != expected
            || assigned_value != expected || comb_value != expected) {
            std::printf("\nvalue=%02x comb conversion ERROR: implicit=%u explicit=%u assigned=%u comb=%u expected=%u\n",
                (unsigned)value, (unsigned)implicit_value, (unsigned)explicit_value,
                (unsigned)assigned_value, (unsigned)comb_value, (unsigned)expected);
            error = true;
        }
    }

    void check_registered(uint8_t value, bool reset)
    {
        bool expected = !reset && value != 0;
#ifdef VERILATOR
        bool actual = dut.registered_out;
#else
        bool actual = dut.registered_out();
#endif
        if (actual != expected) {
            std::printf("\nvalue=%02x registered conversion ERROR: actual=%u expected=%u\n",
                (unsigned)value, (unsigned)actual, (unsigned)expected);
            error = true;
        }
    }

public:
    TestBoolConversion()
    {
#ifndef VERILATOR
        dut.value_in = _ASSIGN(input_value);
        dut._assign();
#else
        dut.clk = 0;
#endif
    }

    bool run()
    {
        static constexpr uint8_t values[] = {0, 1, 2, 3, 0x80, 0xff};
        auto start = std::chrono::high_resolution_clock::now();

#ifdef VERILATOR
        std::printf("VERILATOR BoolConversion...");
#else
        std::printf("CppHDL BoolConversion...");
#endif
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            bool reset = i == 0;
#ifdef VERILATOR
            dut.value_in = values[i];
            dut.reset = reset;
            dut.clk = 0;
            dut.eval();
#else
            input_value = values[i];
#endif
            ++_system_clock;
            check_comb(values[i]);
#ifdef VERILATOR
            dut.clk = 1;
            dut.eval();
#else
            dut._work(reset);
            dut._strobe();
#endif
            check_registered(values[i], reset);
#ifdef VERILATOR
            dut.clk = 0;
            dut.eval();
#endif
        }

        std::printf(" %s (%lld microseconds)\n", error ? "FAILED" : "PASSED",
            (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    bool ok = true;

    for (int i = 1; i < argc; ++i) {
        noveril |= std::strcmp(argv[i], "--noveril") == 0;
    }

#ifndef VERILATOR
    if (!noveril) {
        ok &= VerilatorCompile(__FILE__, "BoolConversion", {"Predef_pkg"}, {"../../../../include"});
        if (ok) {
            ok &= std::system("BoolConversion/obj_dir/VBoolConversion --noveril") == 0;
        }
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok &= check_generated_sv();
    return !(ok && TestBoolConversion().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

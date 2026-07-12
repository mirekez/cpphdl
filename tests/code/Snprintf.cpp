#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <cstdio>
#include <string>

using namespace cpphdl;

struct SnprintfFirstText
{
    uint8_t unused;

    std::string format()
    {
        return "first";
    }
};

struct SnprintfSecondText
{
    uint8_t unused;

    std::string format()
    {
        return "second";
    }
};

class Snprintf : public Module
{
public:
    _PORT(bool) passed_out = _ASSIGN_REG(passed_reg);

private:
    bool passed_reg = false;
    uint8_t cmd = 7;
    SnprintfFirstText str1;
    SnprintfSecondText str2;

    std::string format()
    {
        char buf[384];
        std::string str2_c = str2.format();
        std::snprintf(buf, sizeof(buf), "cmd: %2u, str1: %s, str2: %s\n",
            cmd, str1.format().c_str(), str2_c.c_str());
        return buf;
    }

    std::string format_sprintf()
    {
        char buf[384];
        std::string str2_c = str2.format();
        std::sprintf(buf, "cmd: %2u, str1: %s, str2: %s\n",
            cmd, str1.format().c_str(), str2_c.c_str());
        return buf;
    }

public:
    void _assign() {}

    void _work(bool reset)
    {
        cmd = 7;
        passed_reg = format() == "cmd:  7, str1: first, str2: second\n" &&
            format_sprintf() == "cmd:  7, str1: first, str2: second\n";
        if (reset) {
            passed_reg = false;
        }
    }

    void _strobe() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static bool generated_sv_is_string_safe()
{
    std::filesystem::path path = "generated/Snprintf.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(path)) {
        path = "Snprintf/Snprintf.sv";
    }
#endif
    std::ifstream input(path);
    if (!input) {
        std::print("ERROR: cannot open {}\n", path.string());
        return false;
    }
    const std::string sv((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    // `buf` is an SV primitive keyword, so the emitter correctly escapes the local as `_buf`.
    const bool string_buffer = sv.find("string _buf;") != std::string::npos;
    const bool intermediate = sv.find("string str2_c;") != std::string::npos &&
        sv.find("str2_c = SnprintfSecondText___format(str2)") != std::string::npos;
    const bool formatted = sv.find("_buf = $sformatf(\"cmd: %2d, str1: %s, str2: %s\\n\"") != std::string::npos;
    const bool direct_member_format = sv.find("SnprintfFirstText___format(str1)") != std::string::npos;
    const size_t second_format = sv.find("_buf = $sformatf(\"cmd: %2d, str1: %s, str2: %s\\n\"",
        sv.find("function string format_sprintf"));
    const bool sprintf_formatted = second_format != std::string::npos;
    const bool no_c_string_calls = sv.find("\n        snprintf(") == std::string::npos &&
        sv.find("\n        sprintf(") == std::string::npos && sv.find("c_str") == std::string::npos &&
        sv.find("sizeof") == std::string::npos;
    if (!string_buffer || !intermediate || !formatted || !direct_member_format ||
        !sprintf_formatted || !no_c_string_calls) {
        std::print("ERROR: sprintf-family string conversion is invalid in {}\n", path.string());
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        noveril |= std::strcmp(argv[i], "--noveril") == 0;
    }

    bool ok = generated_sv_is_string_safe();
#ifdef VERILATOR
    Verilated::commandArgs(argc, argv);
    VERILATOR_MODEL dut;
    dut.clk = 0;
    dut.reset = 1;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    dut.clk = 0;
    dut.reset = 0;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    ok &= dut.passed_out;
#else
    Snprintf dut;
    dut._work(true);
    dut._work(false);
    ok &= dut.passed_out();
    if (ok && !noveril) {
        ok &= VerilatorCompile(__FILE__, "Snprintf", {"Predef_pkg"}, {"../../../../include"});
        ok &= std::system("Snprintf/obj_dir/VSnprintf --noveril") == 0;
    }
#endif
    return ok ? 0 : 1;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

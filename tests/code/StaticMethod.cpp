#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

struct StaticMethodPlainHelper {
    static constexpr uint16_t OFFSET = 0x31u;

    static uint16_t mix(uint16_t value)
    {
        uint16_t rotated;

        rotated = (uint16_t)((value << 3) | (value >> 13));
        return (uint16_t)(rotated ^ OFFSET);
    }
};

struct StaticMethodConfig {
    static constexpr uint8_t WIDTH = 7;
    static constexpr uint16_t BIAS = 0x155u;
};

template<typename CFG>
struct StaticMethodTemplateHelper {
    static uint16_t fold(uint16_t value)
    {
        uint16_t masked;

        if (CFG::WIDTH > 4) {
            masked = (uint16_t)(value & ((1u << CFG::WIDTH) - 1u));
        }
        else {
            masked = value;
        }
        return (uint16_t)(StaticMethodPlainHelper::mix(masked) + CFG::BIAS);
    }
};

struct StaticMethodHolder {
    struct Nested {
        static uint16_t spread(uint16_t value)
        {
            uint32_t wide;

            wide = (uint32_t)value;
            return (uint16_t)((wide << 1) ^ (wide >> 2) ^ 0x4a5u);
        }
    };
};

class StaticMethod : public Module
{
public:
    _PORT(u<16>) value_in;
    _PORT(u<16>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    u<16> value_comb;

    u<16>& value_comb_func()
    {
        uint16_t a;
        uint16_t b;
        uint16_t c;

        a = StaticMethodPlainHelper::mix((uint16_t)value_in());
        b = StaticMethodTemplateHelper<StaticMethodConfig>::fold(a);
        c = StaticMethodHolder::Nested::spread(b);
        value_comb = u<16>(c);
        return value_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
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

static uint16_t expected_static_method(uint16_t value)
{
    uint16_t a = StaticMethodPlainHelper::mix(value);
    uint16_t b = StaticMethodTemplateHelper<StaticMethodConfig>::fold(a);
    return StaticMethodHolder::Nested::spread(b);
}

static bool check_generated_sv()
{
    std::filesystem::path top_path = "generated/StaticMethod.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(top_path)) {
        top_path = "StaticMethod_1/StaticMethod.sv";
    }
#endif

    std::ifstream top_in(top_path);
    if (!top_in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", top_path.string());
        return false;
    }

    std::string top((std::istreambuf_iterator<char>(top_in)), std::istreambuf_iterator<char>());
    bool ok = true;
    ok &= top.find("StaticMethodPlainHelper___mix") != std::string::npos;
    ok &= top.find("StaticMethodTemplateHelperStaticMethodConfig___fold") != std::string::npos;
    ok &= top.find("StaticMethodHolder_Nested___spread") != std::string::npos;
    ok &= top.find("assign value_out = value_comb;") != std::string::npos;
    ok &= top.find("function logic[15:0] StaticMethodPlainHelper___mix (input logic[15:0] value);") != std::string::npos;
    ok &= top.find("_this") == std::string::npos;
    ok &= top.find("unknown(") == std::string::npos;
    ok &= top.find("unknown:") == std::string::npos;
    ok &= top.find("DependentScope") == std::string::npos;
    if (!ok) {
        std::print("\nERROR: static method owner export or static-call emission is wrong in {}\n", top_path.string());
    }
    return ok;
}

class TestStaticMethod : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    StaticMethod dut;
#endif

    u<16> value = 0;
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
        std::print("VERILATOR TestStaticMethod...");
#else
        std::print("CppHDL TestStaticMethod...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "static_method_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 4096 && !error; ++i) {
            uint16_t sample = (uint16_t)((i * 97u) ^ (i << 5) ^ (i >> 2) ^ 0x9e37u);
            value = u<16>(sample);
            eval(false);
            uint16_t got = output();
            uint16_t expected = expected_static_method(sample);
            if (got != expected) {
                std::print("\nstatic method ERROR sample=0x{:04x}: got=0x{:04x} expected=0x{:04x}\n",
                    sample, got, expected);
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
        ok &= VerilatorCompile(__FILE__, "StaticMethod", {
            "Predef_pkg",
            "StaticMethodPlainHelper_pkg",
            "StaticMethodConfig_pkg",
            "StaticMethodTemplateHelperStaticMethodConfig_pkg",
            "StaticMethodHolder_Nested_pkg"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("StaticMethod_1/obj_dir/VStaticMethod") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestStaticMethod().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

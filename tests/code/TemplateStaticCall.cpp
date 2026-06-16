#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

struct Native16 {
    union {
        uint16_t raw;
        struct {
            uint16_t mantissa : 10;
            uint16_t exponent : 5;
            uint16_t sign : 1;
        } data;
    };
};

struct Conv16 {
    union {
        uint16_t raw;
        struct {
            uint16_t mantissa : 7;
            uint16_t exponent : 8;
            uint16_t sign : 1;
        } data;
    };
};

template<typename CONV_TYPE = Native16>
struct ArithmeticMini {
    static CONV_TYPE convert(Native16 val)
    {
        CONV_TYPE res;
        res.raw = 0;
        res.data.sign = val.data.sign;
        res.data.exponent = val.data.exponent;
        res.data.mantissa = val.data.mantissa;
        return res;
    }
};

template<typename CONV_TYPE>
class EncoderMini : public Module {
public:
    _PORT(logic<16>) value_in;
    _PORT(logic<16>) value_out = _ASSIGN_COMB(out_comb_func());

private:
    logic<16> out_comb;

public:
    logic<16>& out_comb_func()
    {
        Native16 native;
        CONV_TYPE conv;

        native.raw = (uint16_t)value_in();
        conv = ArithmeticMini<CONV_TYPE>::convert(native);
        out_comb = conv.raw;
        return out_comb;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class TemplateStaticCall : public Module
{
public:
    _PORT(logic<16>) value_in;
    _PORT(logic<16>) value_out = _ASSIGN(encoder.value_out());

private:
    EncoderMini<Conv16> encoder;

public:
    void _assign()
    {
        encoder.value_in = value_in;
        encoder._assign();
    }

    void _work(bool reset)
    {
        encoder._work(reset);
    }

    void _strobe()
    {
        encoder._strobe();
    }
};

template class EncoderMini<Conv16>;

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

static uint16_t expected_convert(uint16_t in)
{
    return (in & 0x8000u) | ((((uint32_t)in >> 10) & 0x1fu) << 7) | (in & 0x7fu);
}

static bool check_generated_sv()
{
    std::filesystem::path sv_path = "generated/EncoderMiniConv16.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(sv_path)) {
        sv_path = "TemplateStaticCall_1/EncoderMiniConv16.sv";
    }
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool ok = true;
    ok &= text.find("ArithmeticMini___convert") == std::string::npos;
    ok &= text.find("ArithmeticMiniConv16___convert") != std::string::npos;
    if (!ok) {
        std::print("\nERROR: templated static call was not specialized in {}\n", sv_path.string());
    }
    return ok;
}

class TestTemplateStaticCall : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateStaticCall dut;
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

    logic<16> output()
    {
#ifdef VERILATOR
        return logic<16>((uint16_t)dut.value_out);
#else
        return dut.value_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTemplateStaticCall...");
#else
        std::print("CppHDL TestTemplateStaticCall...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "template_static_call_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 4096 && !error; ++i) {
            uint16_t sample = (uint16_t)((i * 37u) ^ (i << 9) ^ 0xa5c3u);
            value = logic<16>(sample);
            eval(false);
            uint16_t got = (uint16_t)output();
            uint16_t expected = expected_convert(sample);
            if (got != expected) {
                std::print("\nconvert ERROR sample=0x{:04x}: got=0x{:04x} expected=0x{:04x}\n",
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
        ok &= VerilatorCompile(__FILE__, "TemplateStaticCall", {
            "Predef_pkg",
            "Native16_pkg",
            "Conv16_pkg",
            "ArithmeticMiniConv16_pkg",
            "EncoderMiniConv16"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("TemplateStaticCall_1/obj_dir/VTemplateStaticCall") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestTemplateStaticCall().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

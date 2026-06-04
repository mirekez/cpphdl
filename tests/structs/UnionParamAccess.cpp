#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

struct UnionParamNative16
{
    union {
        uint16_t raw;
        struct {
            uint16_t mantissa : 10;
            uint16_t exponent : 5;
            uint16_t sign : 1;
        } data;
    };
} __PACKED;

struct UnionParamBF16E8
{
    union {
        uint16_t raw;
        struct {
            uint16_t mantissa : 7;
            uint16_t exponent : 8;
            uint16_t sign : 1;
        } data;
    };
} __PACKED;

template<typename CONV_TYPE = UnionParamBF16E8>
class UnionParamAccess : public Module
{
public:
    _PORT(logic<16>) raw_in;
    _PORT(logic<16>) raw_out = _ASSIGN_REG(raw_comb);

private:
    logic<16> raw_comb;

    UnionParamNative16 convert_conv_to_default(CONV_TYPE val)
    {
        UnionParamNative16 res;
        uint16_t mantissa;
        uint16_t exponent;

        res.raw = 0;
        res.data.sign = val.data.sign;
        exponent = val.data.exponent;
        mantissa = val.data.mantissa;
        res.data.exponent = exponent >> 3;
        res.data.mantissa = mantissa << 3;
        return res;
    }

    logic<16>& raw_out_func()
    {
        CONV_TYPE conv;
        UnionParamNative16 native;

        conv.raw = (uint16_t)raw_in();
        native = convert_conv_to_default(conv);
        raw_comb = native.raw;
        return raw_comb;
    }

public:
    void _work(bool)
    {
        raw_out_func();
    }
    void _strobe() {}
    void _assign() {}
};

template class UnionParamAccess<UnionParamBF16E8>;

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
    std::filesystem::path sv_path = "generated/UnionParamAccessUnionParamBF16E8.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(sv_path)) {
        sv_path = "UnionParamAccessUnionParamBF16E8_1/UnionParamAccessUnionParamBF16E8.sv";
    }
#endif

    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string sv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool ok = true;
    ok &= sv.find("res._.data.sign=val._.data.sign;") != std::string::npos;
    ok &= sv.find("exponent=val._.data.exponent;") != std::string::npos;
    ok &= sv.find("mantissa=val._.data.mantissa;") != std::string::npos;
    ok &= sv.find("val.data.sign") == std::string::npos;
    ok &= sv.find("val.data.exponent") == std::string::npos;
    ok &= sv.find("val.data.mantissa") == std::string::npos;
    if (!ok) {
        std::print("\nERROR: anonymous union wrapper was not applied to method argument member access\n");
    }
    return ok;
}

static uint16_t expected(uint16_t raw)
{
    UnionParamBF16E8 conv{};
    UnionParamNative16 native{};
    conv.raw = raw;
    native.data.sign = conv.data.sign;
    native.data.exponent = conv.data.exponent >> 3;
    native.data.mantissa = conv.data.mantissa << 3;
    return native.raw;
}

class TestUnionParamAccess : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    UnionParamAccess<UnionParamBF16E8> dut;
#endif

    logic<16> raw = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.raw_in = _ASSIGN_REG(raw);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.raw_in = (uint16_t)raw;
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
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
        return (uint16_t)dut.raw_out;
#else
        return (uint16_t)dut.raw_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestUnionParamAccess...");
#else
        std::print("CppHDL TestUnionParamAccess...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "union_param_access_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 4096 && !error; ++i) {
            raw = logic<16>((uint16_t)((i * 40503u) ^ (i >> 3) ^ 0xa531u));
            eval(false);
            neg(false);
            uint16_t got = output();
            uint16_t exp = expected((uint16_t)raw);
            if (got != exp) {
                std::print("\nunion param access ERROR i={} raw=0x{:04x} got=0x{:04x} expected=0x{:04x}\n",
                    i, (uint16_t)raw, got, exp);
                error = true;
            }
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
        ok &= VerilatorCompile(__FILE__, "UnionParamAccessUnionParamBF16E8", {
            "Predef_pkg",
            "UnionParamNative16_pkg",
            "UnionParamBF16E8_pkg"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("UnionParamAccessUnionParamBF16E8_1/obj_dir/VUnionParamAccessUnionParamBF16E8") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestUnionParamAccess().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

struct TemplateMemberNative16 {
    static constexpr uint8_t MANT_WIDTH = 10;
    static constexpr uint8_t EXP_WIDTH = 5;

    union {
        uint16_t raw;
        struct {
            uint16_t mantissa : 10;
            uint16_t exponent : 5;
            uint16_t sign : 1;
        } data;
    };
};

struct TemplateMemberConv16 {
    static constexpr uint8_t MANT_WIDTH = 7;
    static constexpr uint8_t EXP_WIDTH = 8;

    union {
        uint16_t raw;
        struct {
            uint16_t mantissa : 7;
            uint16_t exponent : 8;
            uint16_t sign : 1;
        } data;
    };
};

template<typename CONV_TYPE = TemplateMemberNative16>
class TemplateMemberArithmetic : public Module
{
public:
    _PORT(logic<16>) value_in;
    _PORT(logic<16>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    logic<16> value_comb;

    logic<16>& value_comb_func()
    {
        TemplateMemberNative16 native;
        CONV_TYPE conv;

        native.raw = (uint16_t)value_in();
        conv.raw = 0;
        conv.data.sign = native.data.sign;
        conv.data.exponent = native.data.exponent & ((1u << CONV_TYPE::EXP_WIDTH) - 1u);
        conv.data.mantissa = native.data.mantissa & ((1u << CONV_TYPE::MANT_WIDTH) - 1u);
        value_comb = conv.raw;
        return value_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template<size_t DWIDTH_BYTES, typename CONV_TYPE = TemplateMemberConv16>
class TemplateMemberDecoder : public Module
{
    static_assert(DWIDTH_BYTES % sizeof(TemplateMemberNative16) == 0);
    static constexpr unsigned VALUE_BITS = sizeof(TemplateMemberNative16) * 8;
    static constexpr unsigned VALUES_IN_WORD = DWIDTH_BYTES / sizeof(TemplateMemberNative16);

public:
    _PORT(bool) en_in;
    _PORT(logic<DWIDTH_BYTES * 8>) data_in;
    _PORT(logic<DWIDTH_BYTES * 8>) data_out = _ASSIGN_COMB(data_comb_func());

private:
    TemplateMemberArithmetic<CONV_TYPE> arithm;
    logic<DWIDTH_BYTES * 8> data_comb;

    logic<DWIDTH_BYTES * 8>& data_comb_func()
    {
        unsigned i;

        data_comb = data_in();
        if (en_in()) {
            data_comb.bits(VALUE_BITS - 1, 0) = arithm.value_out();
            for (i = 1; i < VALUES_IN_WORD; ++i) {
                data_comb.bits(i * VALUE_BITS + VALUE_BITS - 1, i * VALUE_BITS) =
                    data_in().bits(i * VALUE_BITS + VALUE_BITS - 1, i * VALUE_BITS);
            }
        }
        return data_comb;
    }

public:
    void _assign()
    {
        arithm.value_in = _ASSIGN(logic<16>(data_in().bits(VALUE_BITS - 1, 0)));
        arithm._assign();
    }

    void _work(bool reset)
    {
        arithm._work(reset);
    }

    void _strobe()
    {
        arithm._strobe();
    }
};

class TemplateMemberTop : public Module
{
public:
    _PORT(bool) en_in;
    _PORT(logic<32>) data_in;
    _PORT(logic<32>) data_out = _ASSIGN(decoder.data_out());

private:
    TemplateMemberDecoder<4, TemplateMemberConv16> decoder;

public:
    void _assign()
    {
        decoder.en_in = en_in;
        decoder.data_in = data_in;
        decoder._assign();
    }

    void _work(bool reset)
    {
        decoder._work(reset);
    }

    void _strobe()
    {
        decoder._strobe();
    }
};

template class TemplateMemberDecoder<4, TemplateMemberConv16>;

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
    std::filesystem::path decoder_path = "generated/TemplateMemberDecoderTemplateMemberConv16.sv";
    std::filesystem::path arithmetic_path = "generated/TemplateMemberArithmeticTemplateMemberConv16.sv";
    std::filesystem::path top_path = "generated/TemplateMemberTop.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(decoder_path)) {
        decoder_path = "TemplateMemberTop_1/TemplateMemberDecoderTemplateMemberConv16.sv";
        arithmetic_path = "TemplateMemberTop_1/TemplateMemberArithmeticTemplateMemberConv16.sv";
        top_path = "TemplateMemberTop_1/TemplateMemberTop.sv";
    }
#endif

    std::ifstream decoder_in(decoder_path);
    std::ifstream arithmetic_in(arithmetic_path);
    std::ifstream top_in(top_path);
    if (!decoder_in || !arithmetic_in || !top_in) {
        std::print("\nERROR: can't open generated TemplateMember SystemVerilog files\n");
        return false;
    }

    std::string decoder((std::istreambuf_iterator<char>(decoder_in)), std::istreambuf_iterator<char>());
    std::string arithmetic((std::istreambuf_iterator<char>(arithmetic_in)), std::istreambuf_iterator<char>());
    std::string top((std::istreambuf_iterator<char>(top_in)), std::istreambuf_iterator<char>());
    bool ok = true;
    ok &= decoder.find("TemplateMemberArithmeticCONV_TYPE") == std::string::npos;
    ok &= decoder.find("CONV_TYPE") == std::string::npos;
    ok &= decoder.find("parameter  VALUE_BITS") != std::string::npos;
    ok &= decoder.find("parameter  VALUES_IN_WORD") != std::string::npos;
    ok &= decoder.find("TemplateMemberArithmeticTemplateMemberConv16 #(") != std::string::npos;
    ok &= decoder.find(") arithm (") != std::string::npos;
    ok &= arithmetic.find("CONV_TYPE") == std::string::npos;
    ok &= arithmetic.find("TemplateMemberConv16_pkg::EXP_WIDTH") != std::string::npos;
    ok &= arithmetic.find("TemplateMemberConv16_pkg::MANT_WIDTH") != std::string::npos;
    ok &= top.find("TemplateMemberDecoderTemplateMemberConv16 #(") != std::string::npos;
    ok &= top.find(") decoder (") != std::string::npos;
    ok &= top.find("unknown(") == std::string::npos;
    ok &= top.find("DependentScope") == std::string::npos;
    if (!ok) {
        std::print("\nERROR: template member module type was not specialized correctly\n");
    }
    return ok;
}

class TestTemplateMember : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateMemberTop dut;
#endif

    bool enable = false;
    logic<32> value = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.en_in = _ASSIGN_REG(enable);
        dut.data_in = _ASSIGN_REG(value);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.en_in = enable;
        dut.data_in = (uint32_t)value;
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

    uint32_t output()
    {
#ifdef VERILATOR
        return (uint32_t)dut.data_out;
#else
        return (uint32_t)dut.data_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTemplateMember...");
#else
        std::print("CppHDL TestTemplateMember...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "template_member_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 4096 && !error; ++i) {
            uint16_t lo = (uint16_t)((i * 193u) ^ 0xa529u);
            uint16_t hi = (uint16_t)((i * 389u) ^ 0x3c71u);
            enable = (i & 3u) != 0;
            value = logic<32>(((uint32_t)hi << 16) | lo);
            eval(false);
            uint32_t got = output();
            uint32_t expected = enable ? (((uint32_t)hi << 16) | expected_convert(lo)) : (uint32_t)value;
            if (got != expected) {
                std::print("\ntemplate member ERROR sample={}: got=0x{:08x} expected=0x{:08x}\n",
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
        ok &= VerilatorCompile(__FILE__, "TemplateMemberTop", {
            "Predef_pkg",
            "TemplateMemberNative16_pkg",
            "TemplateMemberConv16_pkg",
            "TemplateMemberArithmeticTemplateMemberConv16",
            "TemplateMemberDecoderTemplateMemberConv16"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("TemplateMemberTop_1/obj_dir/VTemplateMemberTop") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestTemplateMember().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

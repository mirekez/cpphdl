#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

struct TypeTemplateNative16 {
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

struct TypeTemplateConv16 {
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

template<typename CONV_TYPE = TypeTemplateNative16>
struct TypeTemplateArithmetic {
    static CONV_TYPE convert_default_to_conv(TypeTemplateNative16 val)
    {
        CONV_TYPE res;
        res.raw = 0;
        res.data.sign = val.data.sign;
        if (TypeTemplateNative16::EXP_WIDTH < CONV_TYPE::EXP_WIDTH) {
            res.data.exponent = val.data.exponent & ((1u << CONV_TYPE::EXP_WIDTH) - 1u);
        }
        else {
            res.data.exponent = val.data.exponent & ((1u << TypeTemplateNative16::EXP_WIDTH) - 1u);
        }
        if (TypeTemplateNative16::MANT_WIDTH > CONV_TYPE::MANT_WIDTH) {
            res.data.mantissa = val.data.mantissa & ((1u << CONV_TYPE::MANT_WIDTH) - 1u);
        }
        else {
            res.data.mantissa = val.data.mantissa & ((1u << TypeTemplateNative16::MANT_WIDTH) - 1u);
        }
        return res;
    }
};

template<size_t LANES, typename CONV_TYPE = TypeTemplateNative16>
class TypeTemplateModuleLeaf : public Module
{
    static constexpr unsigned VALUE_BITS = sizeof(TypeTemplateNative16) * 8;

public:
    _PORT(logic<LANES * VALUE_BITS>) data_in;
    _PORT(logic<LANES * VALUE_BITS>) data_out = _ASSIGN_COMB(data_out_comb_func());

private:
    logic<LANES * VALUE_BITS> data_out_comb;

    logic<LANES * VALUE_BITS>& data_out_comb_func()
    {
        unsigned i;
        TypeTemplateNative16 in_value;
        CONV_TYPE out_value;

        data_out_comb = data_in();
        in_value.raw = 0;
        out_value.raw = 0;
        for (i = 0; i < LANES; ++i) {
            in_value.raw = (uint16_t)data_in().bits(i * VALUE_BITS + VALUE_BITS - 1, i * VALUE_BITS);
            out_value = TypeTemplateArithmetic<CONV_TYPE>::convert_default_to_conv(in_value);
            data_out_comb.bits(i * VALUE_BITS + VALUE_BITS - 1, i * VALUE_BITS) = out_value.raw;
        }
        return data_out_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template<size_t LANES>
class TypeTemplateModuleParent : public Module
{
    static constexpr unsigned VALUE_BITS = sizeof(TypeTemplateNative16) * 8;
    TypeTemplateModuleLeaf<LANES, TypeTemplateConv16> child;

public:
    _PORT(logic<LANES * VALUE_BITS>) data_in;
    _PORT(logic<LANES * VALUE_BITS>) data_out = _ASSIGN(child.data_out());

    void _assign()
    {
        child.data_in = data_in;
        child._assign();
    }

    void _work(bool reset)
    {
        child._work(reset);
    }

    void _strobe()
    {
        child._strobe();
    }
};

template class TypeTemplateModuleParent<2>;

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
    std::filesystem::path leaf_path = "generated/TypeTemplateModuleLeafTypeTemplateConv16.sv";
    std::filesystem::path parent_path = "generated/TypeTemplateModuleParent.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(leaf_path)) {
        leaf_path = "TypeTemplateModuleParent_2/TypeTemplateModuleLeafTypeTemplateConv16.sv";
        parent_path = "TypeTemplateModuleParent_2/TypeTemplateModuleParent.sv";
    }
#endif

    std::ifstream leaf_in(leaf_path);
    std::ifstream parent_in(parent_path);
    if (!leaf_in || !parent_in) {
        std::print("\nERROR: can't open generated SystemVerilog files {} and {}\n",
            leaf_path.string(), parent_path.string());
        return false;
    }

    std::string leaf((std::istreambuf_iterator<char>(leaf_in)), std::istreambuf_iterator<char>());
    std::string parent((std::istreambuf_iterator<char>(parent_in)), std::istreambuf_iterator<char>());
    bool ok = true;
    ok &= leaf.find("CONV_TYPE") == std::string::npos;
    ok &= leaf.find("unknown(") == std::string::npos;
    ok &= leaf.find("DependentScopeDeclRefExpr") == std::string::npos;
    ok &= leaf.find("unknown:") == std::string::npos;
    ok &= leaf.find("TypeTemplateNative16_pkg::MANT_WIDTH") != std::string::npos;
    ok &= leaf.find("TypeTemplateConv16_pkg::MANT_WIDTH") != std::string::npos;
    ok &= leaf.find("TypeTemplateNative16_pkg::EXP_WIDTH") != std::string::npos;
    ok &= leaf.find("TypeTemplateConv16_pkg::EXP_WIDTH") != std::string::npos;
    ok &= leaf.find("TypeTemplateArithmeticTypeTemplateConv16___convert_default_to_conv") != std::string::npos;
    ok &= leaf.find("TypeTemplateArithmetic___convert_default_to_conv") == std::string::npos;
    ok &= parent.find("TypeTemplateModuleLeafTypeTemplateConv16") != std::string::npos;
    ok &= parent.find(") child (") != std::string::npos;
    if (!ok) {
        std::print("\nERROR: type-template child module was not specialized correctly\n");
    }
    return ok;
}

class TestTypeTemplateModule : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TypeTemplateModuleParent<2> dut;
#endif

    logic<32> value = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.data_in = _ASSIGN_REG(value);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
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

    logic<32> output()
    {
#ifdef VERILATOR
        return logic<32>((uint32_t)dut.data_out);
#else
        return dut.data_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTypeTemplateModule...");
#else
        std::print("CppHDL TestTypeTemplateModule...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "type_template_module_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 4096 && !error; ++i) {
            uint16_t lo = (uint16_t)((i * 29u) ^ 0x8a53u);
            uint16_t hi = (uint16_t)((i * 47u) ^ 0x31f5u);
            value = logic<32>(((uint32_t)hi << 16) | lo);
            eval(false);
            uint32_t got = (uint32_t)output();
            uint32_t expected = ((uint32_t)expected_convert(hi) << 16) | expected_convert(lo);
            if (got != expected) {
                std::print("\ntype-template ERROR sample={}: got=0x{:08x} expected=0x{:08x}\n",
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
        ok &= VerilatorCompile(__FILE__, "TypeTemplateModuleParent", {
            "Predef_pkg",
            "TypeTemplateNative16_pkg",
            "TypeTemplateConv16_pkg",
            "TypeTemplateModuleLeafTypeTemplateConv16"
        }, {"../../../../include"}, 2);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("TypeTemplateModuleParent_2/obj_dir/VTypeTemplateModuleParent") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestTypeTemplateModule().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

template<size_t WIDTH_ = 12, size_t ADD_ = 5>
class TemplateDefaultLeaf : public Module
{
public:
    static constexpr size_t WIDTH = WIDTH_;
    static constexpr size_t ADD = ADD_;

    _PORT(logic<16>) value_in;
    _PORT(logic<16>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    logic<16> value_comb;

public:
    logic<16>& value_comb_func()
    {
        value_comb = logic<16>((uint16_t)value_in() + WIDTH + ADD);
        return value_comb;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class TemplateDefaultParameter : public Module
{
public:
    _PORT(logic<16>) value_in;
    _PORT(logic<16>) default_out = _ASSIGN(default_leaf.value_out());
    _PORT(logic<16>) override_out = _ASSIGN(override_leaf.value_out());

private:
    TemplateDefaultLeaf<> default_leaf;
    TemplateDefaultLeaf<9, 4> override_leaf;

public:
    void _assign()
    {
        default_leaf.value_in = value_in;
        override_leaf.value_in = value_in;
        default_leaf._assign();
        override_leaf._assign();
    }

    void _work(bool reset)
    {
        default_leaf._work(reset);
        override_leaf._work(reset);
    }

    void _strobe()
    {
        default_leaf._strobe();
        override_leaf._strobe();
    }
};

template class TemplateDefaultLeaf<>;
template class TemplateDefaultLeaf<9, 4>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
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

static std::filesystem::path generated_dir()
{
    std::filesystem::path copied = "TemplateDefaultParameter_1";
    if (std::filesystem::exists(copied)) {
        return copied;
    }
    return "generated";
}

static std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static bool has_parameter_default(const std::string& text, const std::string& name, const std::string& decimalValue, const std::string& hexValue)
{
    return text.find("parameter " + name + " = " + decimalValue) != std::string::npos
        || text.find("parameter " + name + " = 'h" + hexValue) != std::string::npos;
}

static bool check_generated_sv()
{
    const std::filesystem::path dir = generated_dir();
    const std::filesystem::path leaf_path = dir / "TemplateDefaultLeaf.sv";
    const std::filesystem::path top_path = dir / "TemplateDefaultParameter.sv";
    bool ok = true;

    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    require(std::filesystem::exists(leaf_path), "TemplateDefaultLeaf.sv was not generated");
    require(std::filesystem::exists(top_path), "TemplateDefaultParameter.sv was not generated");
    if (!std::filesystem::exists(leaf_path) || !std::filesystem::exists(top_path)) {
        return false;
    }

    const std::string leaf = read_file(leaf_path);
    const std::string top = read_file(top_path);

    require(leaf.find("module TemplateDefaultLeaf #(") != std::string::npos,
        "numeric template module was not emitted as a parameterized module");
    require(has_parameter_default(leaf, "WIDTH_", "12", "C"),
        "default numeric template parameter WIDTH_=12 was not emitted into standalone module header");
    require(has_parameter_default(leaf, "ADD_", "5", "5"),
        "default numeric template parameter ADD_=5 was not emitted into standalone module header");
    require(leaf.find("parameter WIDTH_") != std::string::npos
            && leaf.find("parameter ADD_") != std::string::npos,
        "numeric template parameters were not preserved as SV parameters");
    require(leaf.find("parameter  WIDTH = WIDTH_") != std::string::npos,
        "constexpr WIDTH re-export did not reference WIDTH_");
    require(leaf.find("parameter  ADD = ADD_") != std::string::npos,
        "constexpr ADD re-export did not reference ADD_");
    require(leaf.find("parameter  WIDTH = WIDTH;") == std::string::npos
            && leaf.find("parameter  ADD = ADD;") == std::string::npos,
        "constexpr re-export was emitted as a circular self-reference");
    require(top.find("TemplateDefaultLeaf") != std::string::npos,
        "parent did not instantiate TemplateDefaultLeaf");
    require(top.find("#(\n        9\n,       4\n    ) override_leaf") != std::string::npos
            || top.find("#(\n        9\n    ,   4\n    ) override_leaf") != std::string::npos,
        "parent did not preserve explicit override WIDTH_=9, ADD_=4");
    require(leaf.find("unknown") == std::string::npos
            && top.find("unknown") == std::string::npos,
        "generated SV contains unresolved expression");
    return ok;
}

class TestTemplateDefaultParameter : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateDefaultParameter dut;
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

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTemplateDefaultParameter...");
#else
        std::print("CppHDL TestTemplateDefaultParameter...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "template_default_parameter_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 256 && !error; ++i) {
            uint16_t sample = (uint16_t)((i * 37u) ^ 0x5311u);
            value = logic<16>(sample);
            eval(false);

#ifdef VERILATOR
            uint16_t got_default = (uint16_t)dut.default_out;
            uint16_t got_override = (uint16_t)dut.override_out;
#else
            uint16_t got_default = (uint16_t)dut.default_out();
            uint16_t got_override = (uint16_t)dut.override_out();
#endif
            uint16_t expected_default = (uint16_t)(sample + 12 + 5);
            uint16_t expected_override = (uint16_t)(sample + 9 + 4);

            if (got_default != expected_default || got_override != expected_override) {
                std::print("\ndefault parameter ERROR sample=0x{:04x}: got=0x{:04x}/0x{:04x}, expected=0x{:04x}/0x{:04x}\n",
                    sample, got_default, got_override, expected_default, expected_override);
                error = true;
            }
            ++_system_clock;
        }

        auto stop = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        std::print(" {} ({} microseconds)\n", error ? "FAILED" : "PASSED", us);
        return error;
    }
};

int main()
{
    TestTemplateDefaultParameter test;
    return test.run() ? 1 : 0;
}

#endif

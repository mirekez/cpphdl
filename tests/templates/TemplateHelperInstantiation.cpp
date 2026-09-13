#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

struct HelperInstantiationType
{
    static constexpr uint8_t BIAS = 7;
};

template<typename TYPE>
class TemplateInstantiationHelper
{
public:
    logic<8> method(logic<8> value)
    {
        return logic<8>((uint8_t)value + TYPE::BIAS);
    }
};

template<typename TYPE>
class TemplateHelperInstantiationLeaf : public Module
{
public:
    _PORT(logic<8>) value_in;
    _PORT(logic<8>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    TemplateInstantiationHelper<TYPE> helper;
    logic<8> value_comb;

    logic<8>& value_comb_func()
    {
        value_comb = helper.method(value_in());
        return value_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class TemplateHelperInstantiation : public Module
{
public:
    _PORT(logic<8>) value_in;
    _PORT(logic<8>) value_out = _ASSIGN(leaf.value_out());

private:
    TemplateHelperInstantiationLeaf<HelperInstantiationType> leaf;

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
        leaf.value_in = value_in;
        leaf._assign();
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

static std::filesystem::path generated_dir()
{
    const std::filesystem::path copied = "TemplateHelperInstantiation_1";
    return std::filesystem::exists(copied) ? copied : std::filesystem::path("generated");
}

static bool check_generated_sv(const std::filesystem::path& directory, const char* variant)
{
    const std::filesystem::path path = directory
        / "TemplateHelperInstantiationLeafHelperInstantiationType.sv";
    std::ifstream input(path);
    if (!input) {
        std::printf("ERROR: can't open generated SystemVerilog file %s\n", path.c_str());
        return false;
    }

    const std::string text((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    std::string compact;
    compact.reserve(text.size());
    for (char ch : text) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            compact += ch;
        }
    }

    const std::string method_name =
        "TemplateInstantiationHelperHelperInstantiationType___method";
    const bool ok = compact.find("functionlogic[8-1:0]" + method_name + "(") != std::string::npos
        && compact.find("value_comb=" + method_name + "(helper,value_in)") != std::string::npos
        && compact.find("(unknown:") == std::string::npos;
    if (!ok) {
        std::printf("ERROR: %s helper instantiation generated the wrong method call in %s\n",
            variant, path.c_str());
    }
    return ok;
}

class TestTemplateHelperInstantiation
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateHelperInstantiation dut;
    logic<8> value = 0;
#endif
    bool error = false;

public:
    TestTemplateHelperInstantiation()
    {
#ifndef VERILATOR
        dut.value_in = _ASSIGN_REG(value);
        dut._assign();
#endif
    }

    bool run()
    {
        error |= !check_generated_sv(generated_dir(), "default");
        for (uint16_t sample = 0; sample < 256; ++sample) {
#ifdef VERILATOR
            dut.value_in = sample;
            dut.clk = 0;
            dut.reset = 0;
            dut.eval();
            const uint8_t value_out = dut.value_out;
#else
            value = logic<8>(sample);
            const uint8_t value_out = (uint8_t)dut.value_out();
#endif
            const uint8_t expected = (uint8_t)(sample + HelperInstantiationType::BIAS);
            if (value_out != expected) {
                std::printf("value=%02x output=%02x expected=%02x\n",
                    sample, value_out, expected);
                error = true;
                break;
            }
            ++_system_clock;
        }
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
        ok &= VerilatorCompile(__FILE__, "TemplateHelperInstantiation", {
            "Predef_pkg",
            "HelperInstantiationType_pkg",
            "TemplateInstantiationHelperHelperInstantiationType_pkg",
            "TemplateHelperInstantiationLeafHelperInstantiationType"
        }, {"../../../../include"}, 1);
        if (ok) {
            ok &= std::system("TemplateHelperInstantiation_1/obj_dir/VTemplateHelperInstantiation --noveril") == 0;
        }
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestTemplateHelperInstantiation().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

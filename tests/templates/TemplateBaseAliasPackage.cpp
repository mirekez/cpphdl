#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

template<size_t WIDTH_>
class TemplateAliasGeometry
{
public:
    static constexpr size_t WIDTH = WIDTH_;
    static constexpr size_t EXTRA = 3;
    static constexpr size_t TOTAL = WIDTH + EXTRA;
};

template<typename GEOM>
class TemplateAliasBase : public GEOM
{
public:
    static constexpr size_t MIRROR_WIDTH = GEOM::WIDTH;
    static constexpr size_t OUTPUT_WIDTH = GEOM::TOTAL + 1;
};

template<size_t WIDTH_>
class TemplateBaseAliasPackage : public Module, public TemplateAliasBase<TemplateAliasGeometry<WIDTH_>>
{
public:
    using Base = TemplateAliasBase<TemplateAliasGeometry<WIDTH_>>;
    using Production = TemplateAliasGeometry<WIDTH_>;

    static constexpr size_t LOCAL_WIDTH = Base::MIRROR_WIDTH;
    static constexpr size_t LOCAL_OUTPUT_WIDTH = Base::OUTPUT_WIDTH;
    static constexpr size_t PRODUCTION_TOTAL = Production::TOTAL;
    static constexpr size_t WIDTH = Base::WIDTH;

    _PORT(logic<Base::MIRROR_WIDTH>) value_in;
    _PORT(logic<Base::OUTPUT_WIDTH>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    logic<Base::OUTPUT_WIDTH> value_comb;

public:
    logic<Base::OUTPUT_WIDTH>& value_comb_func()
    {
        value_comb = logic<Base::OUTPUT_WIDTH>((uint16_t)value_in() + LOCAL_WIDTH + Base::EXTRA);
        return value_comb;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template class TemplateBaseAliasPackage<9>;

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

static std::filesystem::path generated_sv_path()
{
    std::filesystem::path copied = "TemplateBaseAliasPackage_1/TemplateBaseAliasPackage.sv";
    if (std::filesystem::exists(copied)) {
        return copied;
    }
    return "generated/TemplateBaseAliasPackage.sv";
}

static bool check_generated_sv()
{
    const std::filesystem::path sv_path = generated_sv_path();
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool ok = true;
    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    require(text.find("Base_pkg::") == std::string::npos,
        "using Base alias leaked as Base_pkg reference");
    require(text.find("TemplateAliasBase") == std::string::npos || text.find("_pkg::") == std::string::npos,
        "helper base package leaked into generated module");
    require(text.find("typedef") == std::string::npos,
        "class-only C++ aliases leaked into generated SV typedefs");
    require(text.find("Production") == std::string::npos,
        "Production alias leaked into generated SV");
    require(text.find("parameter WIDTH_ = 'h9") != std::string::npos
            || text.find("parameter WIDTH_ = 9") != std::string::npos,
        "numeric template parameter WIDTH_ was not emitted with a standalone default");
    require(text.find("parameter  LOCAL_WIDTH") != std::string::npos,
        "local constexpr derived from Base alias was not emitted");
    require(text.find("parameter  WIDTH = WIDTH;") == std::string::npos,
        "re-exported constexpr was emitted as a circular self-reference");
    require(text.find("unknown") == std::string::npos,
        "generated SV contains unresolved expression");
    return ok;
}

class TestTemplateBaseAliasPackage : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateBaseAliasPackage<9> dut;
#endif

    logic<9> value = 0;
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

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTemplateBaseAliasPackage...");
#else
        std::print("CppHDL TestTemplateBaseAliasPackage...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "template_base_alias_package_test";
        _assign();

        error |= !check_generated_sv();

#ifdef VERILATOR
        dut.value_in = 0x155;
        dut.eval();
#else
        value = logic<9>(0x155);
        dut._work(false);
#endif
        ++_system_clock;

        auto stop = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        std::print(" {} ({} microseconds)\n", error ? "FAILED" : "PASSED", us);
        return error;
    }
};

int main()
{
    TestTemplateBaseAliasPackage test;
    return test.run() ? 1 : 0;
}

#endif

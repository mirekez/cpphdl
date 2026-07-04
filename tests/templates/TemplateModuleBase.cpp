#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

template<size_t BITS_>
class TemplateBaseGeometry
{
public:
    static constexpr size_t BASE_BITS = BITS_;
    static constexpr size_t BASE_MASK = (1u << BITS_) - 1u;

    static uint16_t trim(uint16_t value)
    {
        return (uint16_t)(value & BASE_MASK);
    }
};

template<typename GEOM>
class TemplateBaseOps : public GEOM
{
public:
    static constexpr size_t EXTRA_BITS = 4;
    static constexpr size_t TOTAL_BITS = GEOM::BASE_BITS + EXTRA_BITS;

    static uint16_t encode(uint16_t value)
    {
        return (uint16_t)((GEOM::trim(value) << EXTRA_BITS) | GEOM::BASE_BITS);
    }
};

class TemplateModuleBase : public Module, public TemplateBaseOps<TemplateBaseGeometry<6>>
{
public:
    using Base = TemplateBaseOps<TemplateBaseGeometry<6>>;

    static constexpr size_t MODULE_BITS = Base::TOTAL_BITS + 6;
    static constexpr size_t MODULE_MASK = Base::BASE_MASK;
    static constexpr size_t MODULE_EXTRA = Base::EXTRA_BITS;

    _PORT(u<16>) data_in;
    _PORT(logic<MODULE_BITS>) data_out = _ASSIGN_COMB(data_comb_func());

private:
    logic<MODULE_BITS> data_comb;

    logic<MODULE_BITS>& data_comb_func()
    {
        uint16_t encoded;

        encoded = encode((uint16_t)data_in());
        data_comb = logic<MODULE_BITS>((uint16_t)(encoded + MODULE_MASK + MODULE_EXTRA));
        return data_comb;
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

static uint32_t expected_template_module_base(uint16_t value)
{
    uint16_t encoded = TemplateModuleBase::encode(value);
    return (uint32_t)(uint16_t)(encoded + TemplateModuleBase::MODULE_MASK + TemplateModuleBase::MODULE_EXTRA);
}

static bool check_generated_sv()
{
    std::filesystem::path top_path = "generated/TemplateModuleBase.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(top_path)) {
        top_path = "TemplateModuleBase/TemplateModuleBase.sv";
    }
#endif

    std::ifstream top_in(top_path);
    if (!top_in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", top_path.string());
        return false;
    }

    std::string top((std::istreambuf_iterator<char>(top_in)), std::istreambuf_iterator<char>());
    bool ok = true;
    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    require(top.find("Base_pkg::") == std::string::npos, "template base alias produced Base_pkg reference");
    require(top.find("TemplateBaseGeometry6_pkg::") == std::string::npos, "template geometry base produced package reference");
    require(top.find("TemplateBaseOpsTemplateBaseGeometry6_pkg::") == std::string::npos, "template operation base produced package reference");
    require(top.find("typedef") == std::string::npos, "template class base produced an SV typedef");
    require(top.find("parameter  MODULE_BITS") != std::string::npos, "derived module constexpr was not emitted locally");
    require(top.find("parameter  TOTAL_BITS") != std::string::npos, "template base constexpr was not emitted locally");
    require(top.find("function logic[15:0] TemplateBaseOpsTemplateBaseGeometry6___encode") != std::string::npos,
        "template base method was not emitted into the module");
    require(top.find("unknown(") == std::string::npos, "generated SV contains an unknown call");
    require(top.find("unknown:") == std::string::npos, "generated SV contains an unresolved dependent expression");

    if (!ok) {
        std::print("\nERROR: template module base conversion is wrong in {}\n", top_path.string());
    }
    return ok;
}

class TestTemplateModuleBase : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateModuleBase dut;
#endif

    u<16> data = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.data_in = _ASSIGN_REG(data);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.data_in = (uint16_t)data;
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
        std::print("VERILATOR TestTemplateModuleBase...");
#else
        std::print("CppHDL TestTemplateModuleBase...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "template_module_base_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 1024 && !error; ++i) {
            uint16_t sample = (uint16_t)((i * 173u) ^ (i << 3) ^ 0x3377u);
            data = u<16>(sample);
            eval(false);
            uint32_t got = output();
            uint32_t expected = expected_template_module_base(sample);
            if (got != expected) {
                std::print("\ntemplate module base ERROR sample=0x{:04x}: got=0x{:04x} expected=0x{:04x}\n",
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
        ok &= VerilatorCompile(__FILE__, "TemplateModuleBase", {"Predef_pkg"}, {"../../../../include"});
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("TemplateModuleBase/obj_dir/VTemplateModuleBase") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestTemplateModuleBase().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

template<size_t WIDTH_>
class InheritedGeometry
{
public:
    static constexpr size_t WIDTH = WIDTH_;
    static constexpr size_t MASK = (1u << WIDTH_) - 1u;

    static uint16_t clip(uint16_t value)
    {
        return (uint16_t)(value & MASK);
    }
};

template<typename GEOM>
class InheritedByteOps : public GEOM
{
public:
    static constexpr size_t OUT_WIDTH = GEOM::WIDTH + 3;
    static constexpr size_t SHIFT = OUT_WIDTH - GEOM::WIDTH;

    static uint16_t fold(uint16_t value)
    {
        return (uint16_t)((GEOM::clip(value) << SHIFT) | GEOM::WIDTH);
    }
};

class InheritedHelperNoPackage : public Module, public InheritedByteOps<InheritedGeometry<5>>
{
public:
    using Base = InheritedByteOps<InheritedGeometry<5>>;

    static constexpr size_t HELP_WIDTH = Base::WIDTH;
    static constexpr size_t HELP_MASK = Base::MASK;
    static constexpr size_t HELP_SHIFT = Base::SHIFT;
    static constexpr size_t HELP_RESULT_BITS = HELP_WIDTH + HELP_SHIFT + 8;

    _PORT(u<16>) value_in;
    _PORT(logic<HELP_RESULT_BITS>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    logic<HELP_RESULT_BITS> value_comb;

    logic<HELP_RESULT_BITS>& value_comb_func()
    {
        uint16_t folded;

        folded = fold((uint16_t)value_in());
        value_comb = logic<HELP_RESULT_BITS>((uint16_t)(folded + HELP_MASK + HELP_SHIFT));
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

static uint32_t expected_inherited_helper(uint16_t value)
{
    uint16_t folded = InheritedHelperNoPackage::fold(value);
    return (uint32_t)(uint16_t)(folded + InheritedHelperNoPackage::HELP_MASK + InheritedHelperNoPackage::HELP_SHIFT);
}

static bool check_generated_sv()
{
    std::filesystem::path top_path = "generated/InheritedHelperNoPackage.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(top_path)) {
        top_path = "InheritedHelperNoPackage/InheritedHelperNoPackage.sv";
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
    require(top.find("Base_pkg::") == std::string::npos, "helper alias produced a Base package reference");
    require(top.find("InheritedGeometry5_pkg::") == std::string::npos, "geometry helper produced a package reference");
    require(top.find("InheritedByteOpsInheritedGeometry5_pkg::") == std::string::npos, "operation helper produced a package reference");
    require(top.find("typedef") == std::string::npos, "helper-only inherited class produced an SV typedef");
    require(top.find("parameter  HELP_WIDTH") != std::string::npos, "inherited HELP_WIDTH constexpr was not emitted as a local parameter");
    require(top.find("parameter  HELP_RESULT_BITS") != std::string::npos, "dependent HELP_RESULT_BITS constexpr was not emitted as a local parameter");
    require(top.find("function logic[15:0] InheritedByteOpsInheritedGeometry5___fold") != std::string::npos,
        "inherited helper method was not emitted into the module");
    require(top.find("unknown(") == std::string::npos, "generated SV contains an unknown call");
    require(top.find("unknown:") == std::string::npos, "generated SV contains an unresolved dependent expression");
    if (!ok) {
        std::print("\nERROR: inherited helper-only class generated package or unresolved owner in {}\n", top_path.string());
    }
    return ok;
}

class TestInheritedHelperNoPackage : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    InheritedHelperNoPackage dut;
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

    uint32_t output()
    {
#ifdef VERILATOR
        return (uint32_t)dut.value_out;
#else
        return (uint32_t)dut.value_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestInheritedHelperNoPackage...");
#else
        std::print("CppHDL TestInheritedHelperNoPackage...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "inherited_helper_no_package_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 1024 && !error; ++i) {
            uint16_t sample = (uint16_t)((i * 131u) ^ (i << 7) ^ 0x5a5au);
            value = u<16>(sample);
            eval(false);
            uint32_t got = output();
            uint32_t expected = expected_inherited_helper(sample);
            if (got != expected) {
                std::print("\ninherited helper ERROR sample=0x{:04x}: got=0x{:04x} expected=0x{:04x}\n",
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
        ok &= VerilatorCompile(__FILE__, "InheritedHelperNoPackage", {"Predef_pkg"}, {"../../../../include"});
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("InheritedHelperNoPackage/obj_dir/VInheritedHelperNoPackage") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestInheritedHelperNoPackage().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

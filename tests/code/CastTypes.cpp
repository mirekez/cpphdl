#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

template<size_t WIDTH_PARAM = 8>
class CastTypes : public Module
{
public:
    static constexpr size_t WIDTH = WIDTH_PARAM;
    static constexpr size_t CAST_BITS = WIDTH <= 1 ? 1 : clog2(WIDTH);

    _PORT(u<32>) value_in;
    _PORT(u<32>) cstyle_out = _ASSIGN_COMB(cstyle_comb_func());
    _PORT(u<32>) functional_out = _ASSIGN_COMB(functional_comb_func());
    _PORT(u<32>) direct_cstyle_out = _ASSIGN_COMB(direct_cstyle_comb_func());
    _PORT(u<32>) direct_functional_out = _ASSIGN_COMB(direct_functional_comb_func());

private:
    u<32> cstyle_comb;
    u<32> functional_comb;
    u<32> direct_cstyle_comb;
    u<32> direct_functional_comb;

    u<CAST_BITS> cstyle_narrow;
    u<CAST_BITS> functional_narrow;
    u<32> scaled_value()
    {
        return (((uint32_t)value_in() & (WIDTH * WIDTH - 1)) / WIDTH);
    }

    u<32>& cstyle_comb_func()
    {
        cstyle_narrow = (u<CAST_BITS>)scaled_value();
        return cstyle_comb = (uint32_t)cstyle_narrow;
    }

    u<32>& functional_comb_func()
    {
        functional_narrow = u<CAST_BITS>(scaled_value());
        return functional_comb = (uint32_t)functional_narrow;
    }

    u<32>& direct_cstyle_comb_func()
    {
        uint32_t v = scaled_value();
        return direct_cstyle_comb = (uint32_t)((u<clog2(WIDTH_PARAM)>)v);
    }

    u<32>& direct_functional_comb_func()
    {
        uint32_t v = scaled_value();
        return direct_functional_comb = (uint32_t)(u<clog2(WIDTH_PARAM)>(v));
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

template class CastTypes<8>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
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

long sys_clock = -1;

template<typename T>
static T verilator_read(const void* ptr)
{
    T value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

static bool check_generated_sv()
{
    std::filesystem::path sv_path = "generated/CastTypes.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(sv_path)) {
        sv_path = "CastTypes_8/CastTypes.sv";
    }
    if (!std::filesystem::exists(sv_path)) {
        sv_path = "CastTypes/CastTypes.sv";
    }
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    bool ok = true;
    ok &= text.find("unsigned'(CAST_BITS'(") != std::string::npos;
    const std::string dependent_width_cast = "unsigned'($clog2(WIDTH_PARAM)'(";
    const size_t first_dependent = text.find(dependent_width_cast);
    ok &= first_dependent != std::string::npos;
    ok &= first_dependent != std::string::npos &&
          text.find(dependent_width_cast, first_dependent + 1) != std::string::npos;
    ok &= text.find("unsigned'(1'(") == std::string::npos;
    ok &= text.find("clog2WIDTH") == std::string::npos;
    ok &= text.find("clog2ENTRIES") == std::string::npos;
    if (!ok) {
        std::print("\nERROR: dependent-width cast generation check failed in {}\n", sv_path.string());
    }
    return ok;
}

class TestCastTypes : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    CastTypes<8> dut;
#endif

    u<32> value = 0;
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
        dut.value_in = value;
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

    uint32_t cstyle_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.cstyle_out);
#else
        return dut.cstyle_out();
#endif
    }

    uint32_t functional_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.functional_out);
#else
        return dut.functional_out();
#endif
    }

    uint32_t direct_cstyle_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.direct_cstyle_out);
#else
        return dut.direct_cstyle_out();
#endif
    }

    uint32_t direct_functional_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.direct_functional_out);
#else
        return dut.direct_functional_out();
#endif
    }

    bool check(uint32_t value_in)
    {
        value = value_in;
        eval(false);
        neg(false);
        ++sys_clock;

        constexpr uint32_t mask = (1u << CastTypes<8>::CAST_BITS) - 1u;
        uint32_t expected = ((value_in & (CastTypes<8>::WIDTH * CastTypes<8>::WIDTH - 1u)) / CastTypes<8>::WIDTH) & mask;
        if (cstyle_value() != expected || functional_value() != expected ||
            direct_cstyle_value() != expected || direct_functional_value() != expected) {
            std::print("\ncast ERROR value=0x{:08x}: cstyle=0x{:x} functional=0x{:x} direct_cstyle=0x{:x} direct_functional=0x{:x} expected=0x{:x}\n",
                value_in, cstyle_value(), functional_value(), direct_cstyle_value(), direct_functional_value(), expected);
            return false;
        }
        return true;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestCastTypes...");
#else
        std::print("CppHDL TestCastTypes...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "cast_types_test";
        _assign();

#ifdef VERILATOR
        error |= !check_generated_sv();
#endif
        for (uint32_t i = 0; i < 128 && !error; ++i) {
            error |= !check(i);
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
        ok &= VerilatorCompile(__FILE__, "CastTypes", {"Predef_pkg"}, {"../../../../include"}, 8);
        ok &= check_generated_sv();
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("CastTypes_8/obj_dir/VCastTypes") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestCastTypes().run());
}

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

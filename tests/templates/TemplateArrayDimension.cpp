#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

struct BF16E8
{
    uint16_t raw;

    constexpr static size_t _size_bits()
    {
        return 16;
    }
};

template<size_t DWIDTH_BYTES = 64>
class TemplateArrayDimensionBase : public Module
{
    static constexpr size_t VALUES_IN_WORD = DWIDTH_BYTES / sizeof(BF16E8);

public:
    _PORT(u<16>) seed_in;
    _PORT(u<6>) index_in;
    _PORT(u<16>) data_out = _ASSIGN_COMB(data_comb_func());

private:
    array<VALUES_IN_WORD, BF16E8> bf16_sum_a_comb;
    u<16> data_comb;

    u<16>& data_comb_func()
    {
        size_t i;

        for (i = 0; i < VALUES_IN_WORD; ++i) {
            bf16_sum_a_comb[i].raw = (uint16_t)seed_in() + i * 7u;
        }
        data_comb = bf16_sum_a_comb[(uint8_t)index_in() % VALUES_IN_WORD].raw;
        return data_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template<size_t DWIDTH_BYTES = 64>
class TemplateArrayDimension : public TemplateArrayDimensionBase<DWIDTH_BYTES>
{
};

template class TemplateArrayDimension<64>;

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
    std::filesystem::path top_path = "generated/TemplateArrayDimension.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(top_path)) {
        top_path = "TemplateArrayDimension/TemplateArrayDimension.sv";
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

    require(top.find("BF16E8[32-1:0][VALUES_IN_WORD-1:0]") == std::string::npos,
        "concrete and symbolic cpphdl::array dimensions were both emitted");
    require(top.find("BF16E8[VALUES_IN_WORD-1:0]") != std::string::npos,
        "cpphdl::array dimension was not emitted once with its symbolic constexpr");
    return ok;
}

class TestTemplateArrayDimension : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateArrayDimension<4> dut;
#endif

    u<16> seed;
    u<6> index;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = _ASSIGN_REG(seed);
        dut.index_in = _ASSIGN_REG(index);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    uint16_t output()
    {
#ifdef VERILATOR
        return (uint16_t)dut.data_out;
#else
        return (uint16_t)dut.data_out();
#endif
    }

    bool run()
    {
        __inst_name = "template_array_dimension_test";
        _assign();
        error |= !check_generated_sv();

        seed = u<16>(0x1230u);
        index = u<6>(1);
        ++_system_clock;
#ifdef VERILATOR
        dut.seed_in = (uint16_t)seed;
        dut.index_in = (uint8_t)index;
        dut.eval();
#endif
        uint16_t expected = (uint16_t)((uint16_t)seed + 7u);
        if (output() != expected) {
            std::print("\nERROR: array element 1 returned {:04x}, expected {:04x}\n",
                output(), expected);
            error = true;
        }
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "TemplateArrayDimension", {"Predef_pkg", "BF16E8_pkg"}, {"../../../../include"});
        auto compile_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        ok = ok && std::system("TemplateArrayDimension/obj_dir/VTemplateArrayDimension") == 0;
        std::print("Verilator compilation time: {} microseconds\n", compile_us);
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestTemplateArrayDimension().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

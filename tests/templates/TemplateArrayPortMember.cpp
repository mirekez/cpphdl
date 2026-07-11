#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

template<size_t SIZE = 4>
class TemplateArrayPortLeaf : public Module
{
public:
    _PORT(array<SIZE, u<8>>) mul_a_in;
    _PORT(u<8>) add_in[SIZE];
    _PORT(u<2>) index_in;
    _PORT(u<8>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        uint8_t index;

        index = (uint8_t)index_in();
        value_comb = mul_a_in()[index] + add_in[index]();
        return value_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class TemplateArrayPortMember : public Module
{
public:
    _PORT(array<4, u<8>>) mul_a_in;
    _PORT(u<8>) add_in[4];
    _PORT(u<2>) index_in;
    _PORT(u<8>) value_out = _ASSIGN(arithm.value_out());

private:
    TemplateArrayPortLeaf<4> arithm;

public:
    void _work(bool reset)
    {
        arithm._work(reset);
    }

    void _strobe()
    {
        arithm._strobe();
    }

    void _assign()
    {
        size_t i;

        arithm.mul_a_in = mul_a_in;
        for (i = 0; i < 4; ++i) {
            arithm.add_in[i] = add_in[i];
        }
        arithm.index_in = index_in;
        arithm._assign();
    }
};

template class TemplateArrayPortLeaf<4>;

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
    std::filesystem::path leaf_path = "generated/TemplateArrayPortLeaf.sv";
    std::filesystem::path top_path = "generated/TemplateArrayPortMember.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(top_path)) {
        leaf_path = "TemplateArrayPortMember/TemplateArrayPortLeaf.sv";
        top_path = "TemplateArrayPortMember/TemplateArrayPortMember.sv";
    }
#endif

    std::ifstream leaf_in(leaf_path);
    std::ifstream top_in(top_path);
    if (!leaf_in || !top_in) {
        std::print("\nERROR: can't open generated array-port member SystemVerilog files\n");
        return false;
    }

    std::string leaf((std::istreambuf_iterator<char>(leaf_in)), std::istreambuf_iterator<char>());
    std::string top((std::istreambuf_iterator<char>(top_in)), std::istreambuf_iterator<char>());
    bool ok = true;
    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    require(leaf.find("input wire[SIZE-1:0][8-1:0] mul_a_in") != std::string::npos,
        "child cpphdl::array port is not packed");
    require(leaf.find("input wire[8-1:0] add_in[SIZE]") != std::string::npos,
        "child C-style array port is not unpacked");
    require(top.find("wire[4-1:0][8-1:0] arithm__mul_a_in;") != std::string::npos,
        "child instance connection wire is not packed like the child port");
    require(top.find("wire[8-1:0] arithm__mul_a_in[4]") == std::string::npos,
        "child instance connection wire was emitted as an unpacked array");
    require(top.find("wire[8-1:0] arithm__add_in[4];") != std::string::npos,
        "child unpacked port became packed in its parent instance wire");
    require(top.find("wire[4-1:0][8-1:0] arithm__add_in;") == std::string::npos,
        "child unpacked port was emitted as a packed parent wire");
    require(top.find(".mul_a_in(arithm__mul_a_in)") != std::string::npos,
        "child packed array port is not connected to its parent wire");
    return ok;
}

class TestTemplateArrayPortMember : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateArrayPortMember dut;
#endif

    array<4, u<8>> values;
    u<8> add[4];
    u<2> index;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.mul_a_in = _ASSIGN_REG(values);
        dut.add_in[0] = _ASSIGN_REG(add[0]);
        dut.add_in[1] = _ASSIGN_REG(add[1]);
        dut.add_in[2] = _ASSIGN_REG(add[2]);
        dut.add_in[3] = _ASSIGN_REG(add[3]);
        dut.index_in = _ASSIGN_REG(index);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    uint8_t output()
    {
#ifdef VERILATOR
        return (uint8_t)dut.value_out;
#else
        return (uint8_t)dut.value_out();
#endif
    }

    bool run()
    {
        __inst_name = "template_array_port_member_test";
        _assign();
        error |= !check_generated_sv();

        values[0] = 0x12;
        values[1] = 0x34;
        values[2] = 0x56;
        values[3] = 0x78;
        add[0] = 1;
        add[1] = 2;
        add[2] = 3;
        add[3] = 4;
        index = 2;
        ++_system_clock;
#ifdef VERILATOR
        dut.mul_a_in = 0x78563412u;
        for (size_t i = 0; i < 4; ++i) {
            dut.add_in[i] = (uint8_t)add[i];
        }
        dut.index_in = (uint8_t)index;
        dut.eval();
#endif
        if (output() != 0x59) {
            std::print("\nERROR: packed/unpacked member arrays returned {:02x}, expected 59\n", output());
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
        ok &= VerilatorCompile(__FILE__, "TemplateArrayPortMember",
            {"Predef_pkg", "TemplateArrayPortLeaf"}, {"../../../../include"});
        auto compile_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        ok = ok && std::system("TemplateArrayPortMember/obj_dir/VTemplateArrayPortMember") == 0;
        std::print("Verilator compilation time: {} microseconds\n", compile_us);
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestTemplateArrayPortMember().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

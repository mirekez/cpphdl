#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

class TemplateInheritedArrayLeaf : public Module
{
public:
    _PORT(u8) value_in;
    _PORT(u8) value_out = _ASSIGN(value_in());
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

template<size_t COUNT = 2>
class TemplateInheritedArrayBase : public Module
{
protected:
    TemplateInheritedArrayLeaf leaf[COUNT];
    u8 value_comb;

    u8& value_comb_func()
    {
        value_comb = leaf[COUNT - 1].value_out();
        return value_comb;
    }
public:
    _PORT(u8) value_out = _ASSIGN_COMB(value_comb_func());

    void _assign()
    {
        size_t i;
        for (i = 0; i < COUNT; ++i) leaf[i].value_in = _ASSIGN_I(i);
    }
    void _work(bool reset)
    {
        size_t i;
        for (i = 0; i < COUNT; ++i) leaf[i]._work(reset);
    }
    void _strobe()
    {
        size_t i;
        for (i = 0; i < COUNT; ++i) leaf[i]._strobe();
    }
};

template<size_t COUNT = 2>
class TemplateInheritedBaseArray : public TemplateInheritedArrayBase<COUNT>
{
    using Base = TemplateInheritedArrayBase<COUNT>;

public:
    void _assign()
    {
        Base::_assign();
    }

    void _work(bool reset)
    {
        Base::_work(reset);
    }
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#include "VTemplateInheritedBaseArray.h"
#endif

long _system_clock = -1;

static bool check_generated_sv()
{
    std::filesystem::path path = "generated/TemplateInheritedBaseArray.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(path)) path = "TemplateInheritedBaseArray_3/TemplateInheritedBaseArray.sv";
#endif
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool ok = in.good() || !text.empty();
    ok &= text.find("TemplateInheritedArrayBase___leaf__value_in[COUNT]") != std::string::npos;
    ok &= text.find("TemplateInheritedArrayBase___leaf__value_in[1]") == std::string::npos;
    ok &= text.find("TemplateInheritedArrayBase___value_comb_func") != std::string::npos;
    ok &= text.find("TemplateInheritedArrayBase2___value_comb_func") == std::string::npos;
    ok &= text.find("TemplateInheritedArrayBase2____assign") == std::string::npos;
    const size_t comb = text.find("always_comb begin : TemplateInheritedArrayBase___value_comb_func");
    ok &= comb != std::string::npos
        && text.find("always_comb begin : TemplateInheritedArrayBase___value_comb_func", comb + 1) == std::string::npos;
    if (!ok) std::print("ERROR: inherited template member array did not retain COUNT\n");
    return ok;
}

int main()
{
    bool ok = check_generated_sv();
    uint8_t actual;
#ifndef VERILATOR
    TemplateInheritedBaseArray<3> dut;
    dut._assign();
    dut._work(false);
    dut._strobe();
    ++_system_clock;
    actual = (uint8_t)dut.value_out();
    ok &= VerilatorCompile(__FILE__, "TemplateInheritedBaseArray",
        {"Predef_pkg", "TemplateInheritedArrayLeaf"}, {"../../../../include"}, 3);
    ok &= std::system("TemplateInheritedBaseArray_3/obj_dir/VTemplateInheritedBaseArray") == 0;
#else
    VTemplateInheritedBaseArray dut;
    dut.clk = 0;
    dut.reset = 0;
    dut.eval();
    actual = dut.value_out;
#endif
    if (actual != 2) {
        std::print("ERROR: inherited array output={}, expected 2 for COUNT=3\n", actual);
        ok = false;
    }
    return !ok;
}
#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

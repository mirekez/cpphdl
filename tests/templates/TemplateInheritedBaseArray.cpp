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
public:
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
};

template class TemplateInheritedBaseArray<1>;
template class TemplateInheritedBaseArray<2>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include "../../examples/tools.h"

long _system_clock = -1;

static bool check_generated_sv()
{
    std::filesystem::path path = "generated/TemplateInheritedBaseArray.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(path)) path = "TemplateInheritedBaseArray/TemplateInheritedBaseArray.sv";
#endif
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool ok = in.good() || !text.empty();
    ok &= text.find("TemplateInheritedArrayBase___leaf__value_in[COUNT]") != std::string::npos;
    ok &= text.find("TemplateInheritedArrayBase___leaf__value_in[1]") == std::string::npos;
    if (!ok) std::print("ERROR: inherited template member array did not retain COUNT\n");
    return ok;
}

int main()
{
    bool ok = check_generated_sv();
#ifndef VERILATOR
    ok &= VerilatorCompile(__FILE__, "TemplateInheritedBaseArray",
        {"Predef_pkg", "TemplateInheritedArrayLeaf"}, {"../../../../include"});
    ok &= std::system("TemplateInheritedBaseArray/obj_dir/VTemplateInheritedBaseArray") == 0;
#endif
    return !ok;
}
#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

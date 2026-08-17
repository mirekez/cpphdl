#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

// Regression for a named clock process delegating to a legacy one-clock
// implementation.  Child lifecycle calls remain native-simulation glue and
// must not be duplicated in the parent's generated SystemVerilog task.

#include <cpphdl.h>

using namespace cpphdl;

class LifecycleDelegationChild : public Module
{
    reg<u<8>> count_reg;

public:
    _PORT(u<8>) count_out = _ASSIGN_REG(count_reg);

    void _work(bool reset)
    {
        count_reg._next = count_reg + 1;
        if (reset) {
            count_reg.clr();
        }
    }

    void _strobe()
    {
        count_reg.strobe();
    }

    void _assign() {}
};

class LifecycleDelegation : public Module
{
    LifecycleDelegationChild child;
    reg<u<8>> count_reg;

public:
    _PORT(u<8>) count_out = _ASSIGN_REG(count_reg);
    _PORT(u<8>) child_count_out;

    void _assign()
    {
        child._assign();
        child_count_out = _ASSIGN(child.count_out());
    }

    // Legacy implementation retained for native, one-clock users.
    void _work(bool reset)
    {
        child._work(reset);
        count_reg._next = count_reg + 1;
        if (reset) {
            count_reg.clr();
        }
    }

    void _strobe()
    {
        child._strobe();
        count_reg.strobe();
    }

    // The generated fast-clock task must preserve this local delegation.
    void _work_fast_clk(bool reset)
    {
        _work(reset);
    }

    void _strobe_fast_clk()
    {
        count_reg.strobe();
    }

    void _work_slow_clk(bool) {}
    void _strobe_slow_clk() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static bool check_generated_rtl()
{
#ifdef VERILATOR
    const std::filesystem::path path =
        "LifecycleDelegation/LifecycleDelegation.sv";
#else
    const std::filesystem::path path = "generated/LifecycleDelegation.sv";
#endif
    std::ifstream input(path);
    if (!input) {
        std::cerr << "missing generated lifecycle RTL: " << path << '\n';
        return false;
    }
    const std::string rtl{std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const size_t wrapper = rtl.find("task _work_fast_clk");
    const size_t wrapper_end = wrapper == std::string::npos
        ? std::string::npos : rtl.find("endtask", wrapper);
    if (wrapper == std::string::npos || wrapper_end == std::string::npos) {
        std::cerr << "missing generated fast-clock wrapper task\n";
        return false;
    }
    const std::string body = rtl.substr(wrapper, wrapper_end - wrapper);
    if (body.find("_work(reset);") == std::string::npos) {
        std::cerr << "local lifecycle delegation was removed from generated RTL\n";
        return false;
    }

    const size_t legacy = rtl.find("task _work (");
    const size_t legacy_end = legacy == std::string::npos
        ? std::string::npos : rtl.find("endtask", legacy);
    if (legacy == std::string::npos || legacy_end == std::string::npos) {
        std::cerr << "missing generated legacy work task\n";
        return false;
    }
    const std::string legacy_body = rtl.substr(legacy, legacy_end - legacy);
    if (legacy_body.find("child._work") != std::string::npos
        || legacy_body.find("child___work") != std::string::npos
        || legacy_body.find("child__work") != std::string::npos) {
        std::cerr << "child lifecycle call leaked into parent task\n";
        return false;
    }
    return true;
}

#ifdef VERILATOR
static bool run_model()
{
    VERILATOR_MODEL dut;
    dut.fast_clk = 0;
    dut.slow_clk = 0;
    dut.reset = 1;
    dut.eval();
    dut.fast_clk = 1;
    dut.eval();
    dut.fast_clk = 0;
    dut.reset = 0;
    dut.eval();
    dut.fast_clk = 1;
    dut.eval();
    return dut.count_out == 1 && dut.child_count_out == 1;
}
#else
static bool run_model()
{
    LifecycleDelegation dut;
    dut._assign();
    dut._work_fast_clk(true);
    dut._strobe();
    dut._work_fast_clk(false);
    dut._strobe();
    return (uint32_t)dut.count_out() == 1
        && (uint32_t)dut.child_count_out() == 1;
}
#endif

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        noveril |= std::strcmp(argv[i], "--noveril") == 0;
    }

    bool ok = check_generated_rtl() && run_model();
#ifndef VERILATOR
    if (ok && !noveril) {
        ok = VerilatorCompileInExactFolder(__FILE__, "LifecycleDelegation",
            "LifecycleDelegation", {"Predef_pkg", "LifecycleDelegationChild"},
            {"../../../../include"});
        ok = ok && std::system(
            "LifecycleDelegation/obj_dir/VLifecycleDelegation") == 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "Lifecycle delegation " << (ok ? "PASSED" : "FAILED") << '\n';
    return ok ? 0 : 1;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

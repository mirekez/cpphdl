#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

class TaskReturn : public Module
{
public:
    __PORT(bool)  early_in;
    __PORT(u<32>) first_in;
    __PORT(u<32>) second_in;
    __PORT(u<32>) value_out = __VAR(value_comb_func());

private:
    u<32> value_comb;

    u<32>& value_comb_func()
    {
        value_comb = first_in() + u<32>(0x1000);
        if (early_in()) {
            return value_comb = first_in() + u<32>(0x55);
        }
        value_comb = second_in() + u<32>(0xaa);
        return value_comb;
    }

public:
    void _work(bool reset)
    {
    }

    void _strobe() {}
    void _assign() {}
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

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

static uint32_t expected_value(bool early, uint32_t first, uint32_t second)
{
    return early ? first + 0x55u : second + 0xaau;
}

#ifdef VERILATOR
static bool generated_sv_has_comb_return_disable()
{
    const std::filesystem::path sv_path = "TaskReturn_1/TaskReturn.sv";
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool has_named_block = text.find("begin : value_comb_func") != std::string::npos;
    const bool has_disable = text.find("disable value_comb_func") != std::string::npos;
    if (!has_named_block || !has_disable) {
        std::print("\nERROR: early comb return was not emitted as named-block disable\n");
        std::print("       named block: {}, disable: {}\n", has_named_block, has_disable);
        return false;
    }
    return true;
}
#endif

class TestTaskReturn : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TaskReturn dut;
#endif

    bool early = false;
    u<32> first;
    u<32> second;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.early_in = __VAR(early);
        dut.first_in = __VAR(first);
        dut.second_in = __VAR(second);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.early_in = early;
        dut.first_in = first;
        dut.second_in = second;
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

    u<32> value()
    {
#ifdef VERILATOR
        return verilator_read<u<32>>(&dut.value_out);
#else
        return dut.value_out();
#endif
    }

    bool check(bool early_value, uint32_t first_value, uint32_t second_value)
    {
        early = early_value;
        first = first_value;
        second = second_value;
        eval(false);
        neg(false);
        ++sys_clock;

        u<32> expected = expected_value(early_value, first_value, second_value);
        if (value() != expected) {
            std::print("\nvalue ERROR: early={}, first={:08x}, second={:08x}, got {:08x}, expected {:08x}\n",
                early_value, first_value, second_value, (uint32_t)value(), (uint32_t)expected);
            return false;
        }
        return true;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTaskReturn...");
#else
        std::print("CppHDL TestTaskReturn...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "task_return_test";
        _assign();

#ifdef VERILATOR
        error |= !generated_sv_has_comb_return_disable();
#endif

        for (uint32_t i = 0; i < 64 && !error; ++i) {
            error |= !check(false, 0x10000000u + i, 0x20000000u + i * 3u);
            error |= !check(true, 0x30000000u + i * 5u, 0x40000000u + i * 7u);
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
        ok &= VerilatorCompile(__FILE__, "TaskReturn", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("TaskReturn_1/obj_dir/VTaskReturn") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestTaskReturn().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

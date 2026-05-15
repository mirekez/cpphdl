#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

// CppHDL MODEL /////////////////////////////////////////////////////////

class CodeReturn : public Module
{
public:
    _PORT(bool)  early_in;
    _PORT(u<32>) first_in;
    _PORT(u<32>) second_in;
    _PORT(u<32>) value_out = _ASSIGN_COMB(value_comb_func());
    _PORT(u<32>) task_value_out = _ASSIGN_COMB(task_value_comb_func());
    _PORT(u<32>) function_value_out = _ASSIGN_COMB(function_value_comb_func());

private:
    u<32> value_comb;
    u<32> task_value_comb;
    u<32> function_value_comb;

    void value_task(u<32>& task_out)
    {
        task_out = first_in() + u<32>(0x2000);
        if (early_in()) {
            task_out = first_in() + u<32>(0x155);
            return;
        }
        task_out = second_in() + u<32>(0x1aa);
    }

    u<32> value_function()
    {
        if (early_in()) {
            return first_in() + u<32>(0x255);
        }
        return second_in() + u<32>(0x2aa);
    }

    u<32>& value_comb_func()
    {
        value_comb = first_in() + u<32>(0x1000);
        if (early_in()) {
            return value_comb = first_in() + u<32>(0x55);
        }
        value_comb = second_in() + u<32>(0xaa);
        return value_comb;
    }

    u<32>& task_value_comb_func()
    {
        value_task(task_value_comb);
        return task_value_comb;
    }

    u<32>& function_value_comb_func()
    {
        function_value_comb = value_function();
        return function_value_comb;
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

static uint32_t expected_task_value(bool early, uint32_t first, uint32_t second)
{
    return early ? first + 0x155u : second + 0x1aau;
}

static uint32_t expected_function_value(bool early, uint32_t first, uint32_t second)
{
    return early ? first + 0x255u : second + 0x2aau;
}

#ifdef VERILATOR
static bool generated_sv_has_comb_return_disable()
{
    const std::filesystem::path sv_path = "CodeReturn_1/CodeReturn.sv";
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool has_always_comb = text.find("always_comb begin : value_comb_func") != std::string::npos;
    const bool has_named_block = text.find("begin : value_comb_func") != std::string::npos;
    const bool has_disable = text.find("disable value_comb_func") != std::string::npos;
    const bool has_task = text.find("task value_task") != std::string::npos;
    const bool has_task_disable = text.find("disable value_task") != std::string::npos;
    const bool has_function = text.find("function logic[32-1:0] value_function") != std::string::npos;
    const bool has_function_return = text.find("return ") != std::string::npos;
    const bool has_no_function_disable = text.find("disable value_function") == std::string::npos;
    if (!has_always_comb || !has_named_block || !has_disable || !has_task || !has_task_disable ||
        !has_function || !has_function_return || !has_no_function_disable) {
        std::print("\nERROR: early comb return was not emitted as named-block disable\n");
        std::print("       always_comb: {}, named block: {}, disable: {}\n", has_always_comb, has_named_block, has_disable);
        std::print("       task: {}, task disable: {}, function: {}, function return: {}, no function disable: {}\n",
            has_task, has_task_disable, has_function, has_function_return, has_no_function_disable);
        return false;
    }
    return true;
}
#endif

class TestCodeReturn : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    CodeReturn dut;
#endif

    bool early = false;
    u<32> first;
    u<32> second;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.early_in = _ASSIGN_REG(early);
        dut.first_in = _ASSIGN_REG(first);
        dut.second_in = _ASSIGN_REG(second);
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

    u<32> task_value()
    {
#ifdef VERILATOR
        return verilator_read<u<32>>(&dut.task_value_out);
#else
        return dut.task_value_out();
#endif
    }

    u<32> function_value()
    {
#ifdef VERILATOR
        return verilator_read<u<32>>(&dut.function_value_out);
#else
        return dut.function_value_out();
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
        u<32> expected_task = expected_task_value(early_value, first_value, second_value);
        u<32> expected_function = expected_function_value(early_value, first_value, second_value);
        if (value() != expected) {
            std::print("\nvalue ERROR: early={}, first={:08x}, second={:08x}, got {:08x}, expected {:08x}\n",
                early_value, first_value, second_value, (uint32_t)value(), (uint32_t)expected);
            return false;
        }
        if (task_value() != expected_task) {
            std::print("\ntask value ERROR: early={}, first={:08x}, second={:08x}, got {:08x}, expected {:08x}\n",
                early_value, first_value, second_value, (uint32_t)task_value(), (uint32_t)expected_task);
            return false;
        }
        if (function_value() != expected_function) {
            std::print("\nfunction value ERROR: early={}, first={:08x}, second={:08x}, got {:08x}, expected {:08x}\n",
                early_value, first_value, second_value, (uint32_t)function_value(), (uint32_t)expected_function);
            return false;
        }
        return true;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestCodeReturn...");
#else
        std::print("CppHDL TestCodeReturn...");
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
        ok &= VerilatorCompile(__FILE__, "CodeReturn", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("CodeReturn_1/obj_dir/VCodeReturn") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestCodeReturn().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

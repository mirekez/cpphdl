#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"

using namespace cpphdl;

class CatSink : public Module
{
public:
    _PORT(logic<17>) valid_in;
    _PORT(logic<17>) bus_out = _ASSIGN(valid_in());

    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

class CatBus : public Module
{
public:
    CatSink buffer;

    _PORT(u<32>) value_in;
    _PORT(logic<17>) bus_out = _ASSIGN(buffer.bus_out());
    _PORT(logic<17>) reg_operand_bus_out = _ASSIGN(reg_operand_buffer.bus_out());
    _PORT(logic<17>) logic_reg_out = _ASSIGN(logic_cat_reg);
    _PORT(u<17>) u_reg_out = _ASSIGN(u_cat_reg);
    _PORT(logic<17>) reg_operand_logic_reg_out = _ASSIGN(reg_operand_logic_cat_reg);
    _PORT(u<17>) reg_operand_u_reg_out = _ASSIGN(reg_operand_u_cat_reg);

    u<4> buffer1_byteenable;
    u<5> buffer1_address;
    u<8> buffer1;
    reg<u<4>> reg_byteenable;
    reg<u<5>> reg_address;
    reg<u<8>> reg_data;
    reg<logic<17>> logic_cat_reg;
    reg<u<17>> u_cat_reg;
    reg<logic<17>> reg_operand_logic_cat_reg;
    reg<u<17>> reg_operand_u_cat_reg;
    CatSink reg_operand_buffer;

    void _work(bool reset)
    {
        buffer1_byteenable = u<4>(((uint32_t)value_in() >> 13) & 0xf);
        buffer1_address = u<5>(((uint32_t)value_in() >> 8) & 0x1f);
        buffer1 = u<8>((uint32_t)value_in() & 0xff);
        reg_byteenable._next = buffer1_byteenable;
        reg_address._next = buffer1_address;
        reg_data._next = buffer1;
        logic_cat_reg._next = Cat{buffer1_byteenable, buffer1_address, buffer1};
        u_cat_reg._next = Cat{buffer1_byteenable, buffer1_address, buffer1};
        reg_operand_logic_cat_reg._next = Cat{reg_byteenable, reg_address, reg_data};
        reg_operand_u_cat_reg._next = Cat{reg_byteenable, reg_address, reg_data};
        buffer._work(reset);
        reg_operand_buffer._work(reset);
    }

    void _strobe()
    {
        reg_byteenable.strobe();
        reg_address.strobe();
        reg_data.strobe();
        logic_cat_reg.strobe();
        u_cat_reg.strobe();
        reg_operand_logic_cat_reg.strobe();
        reg_operand_u_cat_reg.strobe();
        buffer._strobe();
        reg_operand_buffer._strobe();
    }

    void _assign()
    {
        buffer.valid_in = _ASSIGN(Cat{buffer1_byteenable, buffer1_address, buffer1});
        reg_operand_buffer.valid_in = _ASSIGN(Cat{reg_byteenable, reg_address, reg_data});
        buffer._assign();
        reg_operand_buffer._assign();
    }
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
    std::filesystem::path sv_path = "generated/CatBus.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(sv_path)) {
        sv_path = "CatBus/CatBus.sv";
    }
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string concat = "{buffer1_byteenable, buffer1_address, buffer1}";
    const std::string reg_concat = "{reg_byteenable, reg_address, reg_data}";
    const size_t first_concat = text.find(concat);
    const bool has_assign_concat = first_concat != std::string::npos;
    const bool has_reg_concat = has_assign_concat && text.find(concat, first_concat + concat.size()) != std::string::npos;
    const bool has_reg_operand_concat = text.find(reg_concat) != std::string::npos;
    const bool has_concat = has_assign_concat && has_reg_concat && has_reg_operand_concat;
    if (!has_concat) {
        std::print("\nERROR: Cat assignment/register next-state was not emitted as SystemVerilog concatenation in {}\n", sv_path.string());
    }
    return has_concat;
}

class TestCatBus : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    CatBus dut;
#endif

    u<4> byteenable = 0;
    u<5> address = 0;
    u<8> data = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.value_in = _ASSIGN([&]() -> u<32> {
            return ((uint32_t)byteenable << 13) | ((uint32_t)address << 8) | (uint32_t)data;
        }());
        dut.__inst_name = "cat_bus_test/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.value_in = ((uint32_t)byteenable << 13) | ((uint32_t)address << 8) | (uint32_t)data;
        dut.clk = 1;
        dut.reset = reset;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
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

    uint32_t bus_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.bus_out) & 0x1ffffu;
#else
        return (uint32_t)dut.bus_out().to_ullong();
#endif
    }

    uint32_t logic_reg_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.logic_reg_out) & 0x1ffffu;
#else
        return (uint32_t)dut.logic_reg_out().to_ullong();
#endif
    }

    uint32_t u_reg_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.u_reg_out) & 0x1ffffu;
#else
        return (uint32_t)dut.u_reg_out();
#endif
    }

    uint32_t reg_operand_bus_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.reg_operand_bus_out) & 0x1ffffu;
#else
        return (uint32_t)dut.reg_operand_bus_out().to_ullong();
#endif
    }

    uint32_t reg_operand_logic_reg_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.reg_operand_logic_reg_out) & 0x1ffffu;
#else
        return (uint32_t)dut.reg_operand_logic_reg_out().to_ullong();
#endif
    }

    uint32_t reg_operand_u_reg_value()
    {
#ifdef VERILATOR
        return verilator_read<uint32_t>(&dut.reg_operand_u_reg_out) & 0x1ffffu;
#else
        return (uint32_t)dut.reg_operand_u_reg_out();
#endif
    }

    bool check(uint32_t step)
    {
        const uint32_t prev_expected = ((uint32_t)byteenable << 13) | ((uint32_t)address << 8) | (uint32_t)data;

        byteenable = (step * 5 + 3) & 0xf;
        address = (step * 7 + 11) & 0x1f;
        data = (step * 29 + 0x53) & 0xff;

        eval(false);
        neg(false);
        ++sys_clock;

        const uint32_t expected = ((uint32_t)byteenable << 13) | ((uint32_t)address << 8) | (uint32_t)data;
        const uint32_t got = bus_value();
        const uint32_t got_logic_reg = logic_reg_value();
        const uint32_t got_u_reg = u_reg_value();
        const uint32_t got_reg_operand_bus = reg_operand_bus_value();
        const uint32_t got_reg_operand_logic_reg = reg_operand_logic_reg_value();
        const uint32_t got_reg_operand_u_reg = reg_operand_u_reg_value();
        if (got != expected || got_logic_reg != expected || got_u_reg != expected ||
            got_reg_operand_bus != expected || got_reg_operand_logic_reg != prev_expected || got_reg_operand_u_reg != prev_expected) {
            std::print("\ncat bus ERROR step={}: byteenable=0x{:x} address=0x{:x} data=0x{:02x} bus=0x{:05x} logic_reg=0x{:05x} u_reg=0x{:05x} reg_bus=0x{:05x} reg_logic=0x{:05x} reg_u=0x{:05x} expected=0x{:05x} prev_expected=0x{:05x}\n",
                step, (uint32_t)byteenable, (uint32_t)address, (uint32_t)data, got, got_logic_reg, got_u_reg,
                got_reg_operand_bus, got_reg_operand_logic_reg, got_reg_operand_u_reg, expected, prev_expected);
            return false;
        }
        return true;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestCatBus...");
#else
        std::print("CppHDL TestCatBus...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "cat_bus_test";
        _assign();
        neg(false);

#ifdef VERILATOR
        error |= !check_generated_sv();
#endif
        for (uint32_t i = 0; i < 512 && !error; ++i) {
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
        ok &= VerilatorCompileInExactFolder(__FILE__, "CatBus", "CatBus", {"Predef_pkg", "CatSink"}, {"../../../../include"});
        ok &= check_generated_sv();
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("CatBus/obj_dir/VCatBus") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestCatBus().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

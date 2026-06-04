#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"

using namespace cpphdl;

template<uint64_t VALUE>
struct ConstexprTemplateValue
{
    static constexpr uint64_t value = VALUE;
};

constexpr uint64_t constexpr_primitive_arg(uint64_t value)
{
    return (value << 4) | 0x3u;
}

uint64_t primitive_arg(uint64_t value)
{
    return (value << 1) ^ 0x55u;
}

uint32_t primitive_arg32(uint32_t value)
{
    return (value << 3) ^ 0x1234u;
}

uint16_t primitive_arg16(uint16_t value)
{
    return (uint16_t)((value << 2) ^ 0x55aau);
}

uint8_t primitive_arg8(uint8_t value)
{
    return (uint8_t)(value ^ 0xa5u);
}

uint64_t primitive_arg_overload(uint64_t value)
{
    return value + 0x100u;
}

logic<8> primitive_arg_overload(logic<8> value)
{
    return value ^ logic<8>(0xff);
}

struct LogicCastConfig
{
    static constexpr logic<1> enable = 1;
    static constexpr logic<8> pattern = 0xa5;
    static constexpr logic<4> low_nibble = pattern.bits(3, 0);
    static constexpr logic<4> high_nibble = pattern.bits(7, 4);
    static constexpr logic<1> top_bit = pattern[7];
};

static_assert(ConstexprTemplateValue<LogicCastConfig::enable>::value == 1);
static_assert(ConstexprTemplateValue<LogicCastConfig::low_nibble>::value == 0x5);
static_assert(ConstexprTemplateValue<LogicCastConfig::high_nibble>::value == 0xa);
static_assert(ConstexprTemplateValue<LogicCastConfig::top_bit>::value == 1);
static_assert(constexpr_primitive_arg(LogicCastConfig::enable) == 0x13);
static_assert((uint64_t)LogicCastConfig::pattern == 0xa5);
static_assert((uint64_t)LogicCastConfig::pattern.bits(5, 2) == 0x9);
static_assert((bool)LogicCastConfig::top_bit);

uint64_t runtime_primitive_arg(logic<8> value)
{
    return primitive_arg(value);
}

uint32_t runtime_primitive_arg32(logic<17> value)
{
    return primitive_arg32(value);
}

uint16_t runtime_primitive_arg16(logic<12> value)
{
    return primitive_arg16(value);
}

uint8_t runtime_primitive_arg8(logic<7> value)
{
    return primitive_arg8(value);
}

uint64_t runtime_logic_multiply_right(logic<13> value)
{
    return value * 7u;
}

uint64_t runtime_logic_multiply_left(logic<13> value)
{
    return 11u * value;
}

uint64_t runtime_logic_bits_multiply(logic<16>& value)
{
    return value.bits(7, 0) * 13u;
}

logic<8> runtime_logic_overload(logic<8> value)
{
    return primitive_arg_overload(value);
}

class LogicCast : public Module
{
public:
    _PORT(logic<16>) value_in;
    _PORT(logic<16>) value_out = _ASSIGN_COMB(value_out_comb_func());

private:
    logic<16> value_out_comb;

    logic<16>& value_out_comb_func()
    {
        value_out_comb = value_in();
        value_out_comb.bits(3, 0) = logic<4>(0x5);
        value_out_comb.bits(7, 4) = logic<4>(0xa);
        value_out_comb[8] = 1;
        return value_out_comb;
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

class TestLogicCast : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    LogicCast dut;
#endif

    logic<16> value = 0;
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

    logic<16> output()
    {
#ifdef VERILATOR
        return logic<16>((uint16_t)dut.value_out);
#else
        return dut.value_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestLogicCast...");
#else
        std::print("CppHDL TestLogicCast...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "logic_cast_test";
        _assign();

        if (!(logic<4>(0x5) == LogicCastConfig::low_nibble) ||
            !(logic<12>(0x0a5) == LogicCastConfig::pattern) ||
            !(logic<12>(0x1a5) != LogicCastConfig::pattern)) {
            std::print("\nERROR: compile-time values failed runtime equality checks\n");
            error = true;
        }

        for (uint32_t i = 0; i < 1024 && !error; ++i) {
            uint16_t sample = (uint16_t)((i * 29u) ^ (i << 7) ^ 0x5a00u);
            value = logic<16>(sample);
            eval(false);

            uint16_t expected = sample;
            expected = (expected & (uint16_t)~0x01ffu) | 0x01a5u;
            uint16_t got = (uint16_t)output();
            if (got != expected) {
                std::print("\nERROR: sample=0x{:04x} got=0x{:04x} expected=0x{:04x}\n", sample, got, expected);
                error = true;
            }

            uint64_t primitive_expected = (((sample & 0xffu) << 1) ^ 0x55u);
            uint64_t primitive_got = runtime_primitive_arg(logic<8>(sample));
            if (primitive_got != primitive_expected) {
                std::print("\nERROR: primitive_arg sample=0x{:04x} got=0x{:x} expected=0x{:x}\n",
                    sample, primitive_got, primitive_expected);
                error = true;
            }

            uint32_t primitive32_expected = (((sample & 0x1ffffu) << 3) ^ 0x1234u);
            uint32_t primitive32_got = runtime_primitive_arg32(logic<17>(sample));
            if (primitive32_got != primitive32_expected) {
                std::print("\nERROR: primitive_arg32 sample=0x{:04x} got=0x{:x} expected=0x{:x}\n",
                    sample, primitive32_got, primitive32_expected);
                error = true;
            }

            uint16_t primitive16_expected = (uint16_t)(((sample & 0xfffu) << 2) ^ 0x55aau);
            uint16_t primitive16_got = runtime_primitive_arg16(logic<12>(sample));
            if (primitive16_got != primitive16_expected) {
                std::print("\nERROR: primitive_arg16 sample=0x{:04x} got=0x{:04x} expected=0x{:04x}\n",
                    sample, primitive16_got, primitive16_expected);
                error = true;
            }

            uint8_t primitive8_expected = (uint8_t)((sample & 0x7fu) ^ 0xa5u);
            uint8_t primitive8_got = runtime_primitive_arg8(logic<7>(sample));
            if (primitive8_got != primitive8_expected) {
                std::print("\nERROR: primitive_arg8 sample=0x{:04x} got=0x{:02x} expected=0x{:02x}\n",
                    sample, primitive8_got, primitive8_expected);
                error = true;
            }

            uint64_t multiply_right_expected = (sample & 0x1fffu) * 7u;
            uint64_t multiply_right_got = runtime_logic_multiply_right(logic<13>(sample));
            if (multiply_right_got != multiply_right_expected) {
                std::print("\nERROR: logic multiply right sample=0x{:04x} got=0x{:x} expected=0x{:x}\n",
                    sample, multiply_right_got, multiply_right_expected);
                error = true;
            }

            uint64_t multiply_left_expected = 11u * (sample & 0x1fffu);
            uint64_t multiply_left_got = runtime_logic_multiply_left(logic<13>(sample));
            if (multiply_left_got != multiply_left_expected) {
                std::print("\nERROR: logic multiply left sample=0x{:04x} got=0x{:x} expected=0x{:x}\n",
                    sample, multiply_left_got, multiply_left_expected);
                error = true;
            }

            logic<16> multiply_slice = logic<16>(sample);
            uint64_t multiply_bits_expected = (sample & 0xffu) * 13u;
            uint64_t multiply_bits_got = runtime_logic_bits_multiply(multiply_slice);
            if (multiply_bits_got != multiply_bits_expected) {
                std::print("\nERROR: logic_bits multiply sample=0x{:04x} got=0x{:x} expected=0x{:x}\n",
                    sample, multiply_bits_got, multiply_bits_expected);
                error = true;
            }

            uint8_t logic_overload_expected = (uint8_t)((sample & 0xffu) ^ 0xffu);
            uint8_t logic_overload_got = (uint8_t)runtime_logic_overload(logic<8>(sample));
            if (logic_overload_got != logic_overload_expected) {
                std::print("\nERROR: logic overload sample=0x{:04x} got=0x{:02x} expected=0x{:02x}\n",
                    sample, logic_overload_got, logic_overload_expected);
                error = true;
            }

            logic<12> wide_low = logic<12>(sample & 0xffu);
            logic<8> byte = logic<8>(sample);
            if (!(wide_low == byte) || byte != wide_low) {
                std::print("\nERROR: cross-width equality sample=0x{:04x} wide={} byte={}\n",
                    sample, wide_low, byte);
                error = true;
            }

            logic<12> wide_high = logic<12>((sample & 0xffu) | 0x100u);
            if (wide_high == byte || !(byte != wide_high)) {
                std::print("\nERROR: cross-width inequality sample=0x{:04x} wide={} byte={}\n",
                    sample, wide_high, byte);
                error = true;
            }

            logic<16> sliced = logic<16>(sample);
            if (!(byte == sliced.bits(7, 0)) || !(sliced.bits(7, 0) == byte)) {
                std::print("\nERROR: logic_bits equality sample=0x{:04x} slice={} byte={}\n",
                    sample, logic<16>(sliced.bits(7, 0)), byte);
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
        if (std::strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "LogicCast", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("LogicCast_1/obj_dir/VLogicCast") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestLogicCast().run());
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

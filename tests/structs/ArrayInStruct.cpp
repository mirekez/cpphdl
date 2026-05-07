#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

struct ArrayPayload
{
    unsigned prefix:4;
    array<u8, 3> bytes;
    unsigned mid:3;
    array<u16, 1> halfs;
    unsigned tail:5;
} __PACKED;

class ArrayInStruct : public Module
{
public:
    __PORT(u<8>) seed_in;
    __PORT(ArrayPayload) payload_in;
    __PORT(ArrayPayload) direct_out = __VAR(direct_comb_func());
    __PORT(ArrayPayload) state_out = __VAR(state_reg);

private:
    ArrayPayload direct_comb;
    reg<ArrayPayload> state_reg;

    static ArrayPayload make_payload(uint32_t seed)
    {
        ArrayPayload payload{};
        payload.prefix = seed & 0xf;
        payload.bytes[0] = u8(seed + 0x11);
        payload.bytes[1] = u8(seed + 0x23);
        payload.bytes[2] = u8(seed + 0x35);
        payload.mid = (seed >> 2) & 0x7;
        payload.halfs[0] = u16((seed << 8) ^ 0x5aa5);
        payload.tail = (seed + 0x17) & 0x1f;
        return payload;
    }

    ArrayPayload& direct_comb_func()
    {
        ArrayPayload in_payload = payload_in();
        direct_comb = make_payload(seed_in());
        direct_comb.prefix ^= in_payload.prefix;
        direct_comb.bytes[0] = direct_comb.bytes[0] ^ in_payload.bytes[2];
        direct_comb.bytes[1] = direct_comb.bytes[1] ^ in_payload.bytes[1];
        direct_comb.bytes[2] = direct_comb.bytes[2] ^ in_payload.bytes[0];
        direct_comb.mid ^= in_payload.mid;
        direct_comb.halfs[0] = direct_comb.halfs[0] ^ in_payload.halfs[0];
        direct_comb.tail ^= in_payload.tail;
        return direct_comb;
    }

public:
    void _work(bool reset)
    {
        if (reset) {
            state_reg._next = {};
        } else {
            state_reg._next = direct_comb_func();
        }
    }

    void _strobe()
    {
        state_reg.strobe();
    }

    void _assign() {}
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

static ArrayPayload expected_payload(uint32_t seed)
{
    ArrayPayload payload{};
    payload.prefix = seed & 0xf;
    payload.bytes[0] = u8(seed + 0x11);
    payload.bytes[1] = u8(seed + 0x23);
    payload.bytes[2] = u8(seed + 0x35);
    payload.mid = (seed >> 2) & 0x7;
    payload.halfs[0] = u16((seed << 8) ^ 0x5aa5);
    payload.tail = (seed + 0x17) & 0x1f;
    return payload;
}

static ArrayPayload expected_direct(uint32_t seed, const ArrayPayload& input)
{
    ArrayPayload payload = expected_payload(seed);
    payload.prefix ^= input.prefix;
    payload.bytes[0] = payload.bytes[0] ^ input.bytes[2];
    payload.bytes[1] = payload.bytes[1] ^ input.bytes[1];
    payload.bytes[2] = payload.bytes[2] ^ input.bytes[0];
    payload.mid ^= input.mid;
    payload.halfs[0] = payload.halfs[0] ^ input.halfs[0];
    payload.tail ^= input.tail;
    return payload;
}

static bool same_fields(const ArrayPayload& got, const ArrayPayload& exp)
{
    return got.prefix == exp.prefix &&
        got.bytes[0] == exp.bytes[0] &&
        got.bytes[1] == exp.bytes[1] &&
        got.bytes[2] == exp.bytes[2] &&
        got.mid == exp.mid &&
        got.halfs[0] == exp.halfs[0] &&
        got.tail == exp.tail;
}

template<typename T>
static void print_bytes(const char* name, const T& sample)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&sample);
    std::print("{}:", name);
    for (size_t i = 0; i < sizeof(sample); ++i) {
        std::print(" {:02x}", bytes[i]);
    }
    std::print("\n");
}

class TestArrayInStruct : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    ArrayInStruct dut;
#endif

    u<8> seed;
    ArrayPayload payload;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = __VAR(seed);
        dut.payload_in = __VAR(payload);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.seed_in = seed;
        std::memcpy(&dut.payload_in, &payload, sizeof(payload));
        dut.clk = 1;
        dut.reset = reset;
        dut.eval();
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
        dut.eval();
#else
        (void)reset;
        dut._strobe();
#endif
    }

    ArrayPayload direct()
    {
#ifdef VERILATOR
        ArrayPayload ret{};
        std::memcpy(&ret, &dut.direct_out, sizeof(ret));
        return ret;
#else
        return dut.direct_out();
#endif
    }

    ArrayPayload state()
    {
#ifdef VERILATOR
        ArrayPayload ret{};
        std::memcpy(&ret, &dut.state_out, sizeof(ret));
        return ret;
#else
        return dut.state_out();
#endif
    }

    void check_fields(const char* name, uint32_t value, const ArrayPayload& got, const ArrayPayload& exp)
    {
        if (!same_fields(got, exp)) {
            std::print("\nseed {} {} field mismatch\n", value, name);
            print_bytes("got     ", got);
            print_bytes("expected", exp);
            error = true;
        }
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestArrayInStruct...");
#else
        std::print("CppHDL TestArrayInStruct...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "array_in_struct_test";
        _assign();

        seed = 0;
        payload = {};
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = true;
        dut.eval();
        eval(true);
        neg(true);
#endif
        eval(true);
        neg(true);
        check_fields("reset_state", 0, state(), ArrayPayload{});
        ++sys_clock;

        for (uint32_t i = 0; i < 256 && !error; ++i) {
            seed = i;
            payload = expected_payload(i ^ 0x5a);
            ArrayPayload exp = expected_direct(i, payload);
            eval(false);
            check_fields("direct", i, direct(), exp);
            neg(false);
            check_fields("state", i, state(), exp);
            ++sys_clock;
        }

        std::print(" {} ({} us, size={})\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count(),
            sizeof(ArrayPayload));
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
        ok &= VerilatorCompile(__FILE__, "ArrayInStruct", {
            "Predef_pkg",
            "ArrayPayload_pkg"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("ArrayInStruct_1/obj_dir/VArrayInStruct") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestArrayInStruct().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

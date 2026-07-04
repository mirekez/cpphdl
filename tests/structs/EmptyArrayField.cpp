#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

struct EmptyArrayPayload
{
    unsigned head:4;
    uint8_t removed_bytes[0];
    unsigned mid:4;
    uint16_t removed_words[0];
    uint8_t tail;
} __PACKED;

class EmptyArrayField : public Module
{
public:
    _PORT(EmptyArrayPayload) payload_in;
    _PORT(EmptyArrayPayload) payload_out = _ASSIGN_COMB(payload_comb_func());

private:
    EmptyArrayPayload payload_comb;

    EmptyArrayPayload& payload_comb_func()
    {
        EmptyArrayPayload value;
        uint8_t head;
        uint8_t mid;
        uint8_t tail;

        value = payload_in();
        head = value.head;
        mid = value.mid;
        tail = value.tail;

        payload_comb = {};
        payload_comb.head = tail & 0xf;
        payload_comb.mid = (head ^ mid ^ 0x5) & 0xf;
        payload_comb.tail = uint8_t((tail << 1) ^ head ^ 0xa5);
        return payload_comb;
    }

public:
    void _work(bool) {}
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
#include <print>
#include <sstream>
#include <string>
#include <vector>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static EmptyArrayPayload make_payload(uint32_t seed)
{
    EmptyArrayPayload payload{};
    payload.head = seed & 0xf;
    payload.mid = (seed >> 4) & 0xf;
    payload.tail = uint8_t((seed * 17) ^ 0xa6);
    return payload;
}

static EmptyArrayPayload transform_payload(const EmptyArrayPayload& input)
{
    EmptyArrayPayload payload{};
    payload.head = input.tail & 0xf;
    payload.mid = (input.head ^ input.mid ^ 0x5) & 0xf;
    payload.tail = uint8_t((input.tail << 1) ^ input.head ^ 0xa5);
    return payload;
}

static bool same_payload(const EmptyArrayPayload& got, const EmptyArrayPayload& expected)
{
    return got.head == expected.head &&
        got.mid == expected.mid &&
        got.tail == expected.tail;
}

[[maybe_unused]] static uint16_t pack_payload(const EmptyArrayPayload& payload)
{
    return uint16_t((uint16_t(payload.tail) << 8) |
        ((uint16_t(payload.mid) & 0xf) << 4) |
        (uint16_t(payload.head) & 0xf));
}

[[maybe_unused]] static EmptyArrayPayload unpack_payload(uint16_t raw)
{
    EmptyArrayPayload payload{};
    payload.head = raw & 0xf;
    payload.mid = (raw >> 4) & 0xf;
    payload.tail = uint8_t(raw >> 8);
    return payload;
}

static std::filesystem::path generated_sv_path(const std::string& file)
{
    std::filesystem::path copied = std::filesystem::path("EmptyArrayField_1") / file;
    std::filesystem::path generated = std::filesystem::path("generated") / file;
    if (std::filesystem::exists(copied) && (!std::filesystem::exists(generated) ||
            std::filesystem::last_write_time(copied) >= std::filesystem::last_write_time(generated))) {
        return copied;
    }
    return generated;
}

static bool generated_sv_drops_empty_arrays()
{
    const auto sv_path = generated_sv_path("EmptyArrayPayload_pkg.sv");
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    bool ok = true;
    const std::vector<std::string> forbidden = {
        "removed_bytes",
        "removed_words",
        "[0-1:0]"
    };
    for (const auto& needle : forbidden) {
        if (text.find(needle) != std::string::npos) {
            std::print("\nERROR: {} still contains '{}'\n", sv_path.string(), needle);
            ok = false;
        }
    }

    const std::vector<std::string> required = {
        "logic[7:0] tail;",
        "logic[4-1:0] mid;",
        "logic[4-1:0] head;"
    };
    for (const auto& needle : required) {
        if (text.find(needle) == std::string::npos) {
            std::print("\nERROR: {} does not contain '{}'\n", sv_path.string(), needle);
            ok = false;
        }
    }
    return ok;
}

class TestEmptyArrayField : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    EmptyArrayField dut;
#endif

    EmptyArrayPayload payload;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.payload_in = _ASSIGN_REG(payload);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval()
    {
#ifdef VERILATOR
        dut.payload_in = pack_payload(payload);
        dut.eval();
#else
        dut._work(false);
#endif
    }

    EmptyArrayPayload output()
    {
#ifdef VERILATOR
        return unpack_payload(dut.payload_out);
#else
        return dut.payload_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestEmptyArrayField...");
#else
        std::print("CppHDL TestEmptyArrayField...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "empty_array_field_test";
        _assign();

        for (uint32_t i = 0; i < 256 && !error; ++i) {
            payload = make_payload(i);
            EmptyArrayPayload expected = transform_payload(payload);
            eval();
            EmptyArrayPayload got = output();
            if (!same_payload(got, expected)) {
                std::print("\nempty-array payload ERROR sample={}: got head/mid/tail={:x}/{:x}/{:02x}"
                    ", expected {:x}/{:x}/{:02x}\n",
                    i, unsigned(got.head), unsigned(got.mid), unsigned(got.tail),
                    unsigned(expected.head), unsigned(expected.mid), unsigned(expected.tail));
                error = true;
            }
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
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "EmptyArrayField", {
            "Predef_pkg",
            "EmptyArrayPayload_pkg"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("EmptyArrayField_1/obj_dir/VEmptyArrayField") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
    ok &= generated_sv_drops_empty_arrays();
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestEmptyArrayField().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

template<size_t COUNT>
struct ModuleStructPayload
{
    array<COUNT, u8, true> values;
};

// This class must remain a module while also exposing a struct-returning
// function that another module can use as its signal source.
class ModuleStructSource : public Module
{
public:
    static constexpr unsigned PARAMS = 3;
    using Payload = ModuleStructPayload<PARAMS>;

    _PORT(Payload) payload_in;
    _PORT(Payload) payload_out = _ASSIGN(transform(payload_in()));

public:
    static Payload transform(Payload payload)
    {
        size_t i;
        for (i = 0; i < PARAMS; ++i) {
            payload.values[i] = (uint8_t)payload.values[i] + (uint8_t)i + 1u;
        }
        return payload;
    }

    Payload transform_member(Payload payload)
    {
        size_t i;
        for (i = 0; i < PARAMS; ++i) {
            payload.values[i] = (uint8_t)payload.values[i] + 0x10u + (uint8_t)i;
        }
        return payload;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

// Exercise both ways a module class can source a function in another module:
// a static class call and an instance call through an actual member module.
class ModuleStruct : public Module
{
public:
    using Payload = ModuleStructSource::Payload;

    _PORT(Payload) payload_in;
    _PORT(Payload) payload_out = _ASSIGN(ModuleStructSource::transform(payload_in()));
    _PORT(Payload) member_payload_out = _ASSIGN(source.transform_member(payload_in()));

private:
    ModuleStructSource source;

public:
    void _work(bool reset)
    {
        source._work(reset);
    }

    void _strobe()
    {
        source._strobe();
    }

    void _assign()
    {
        source.payload_in = payload_in;
        source._assign();
    }
};

/////////////////////////////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

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

static std::filesystem::path generated_path(const std::string& file)
{
    const std::filesystem::path copied = std::filesystem::path("ModuleStruct") / file;
    const std::filesystem::path generated = std::filesystem::path("generated") / file;
    if (std::filesystem::exists(copied) && (!std::filesystem::exists(generated) ||
            std::filesystem::last_write_time(copied) >= std::filesystem::last_write_time(generated))) {
        return copied;
    }
    return generated;
}

static std::string read_generated(const std::string& file, bool& ok)
{
    const auto path = generated_path(file);
    std::ifstream in(path);
    if (!in) {
        std::print("ERROR: cannot open generated SystemVerilog file {}\n", path.string());
        ok = false;
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static bool check_generated_sv()
{
    bool ok = true;
    const std::string consumer = read_generated("ModuleStruct.sv", ok);
    const std::string source = read_generated("ModuleStructSource.sv", ok);
    const std::string package = read_generated("ModuleStructSource_pkg.sv", ok);
    if (!ok) {
        return false;
    }

    ok &= consumer.find("import ModuleStructSource_pkg::*;") != std::string::npos;
    ok &= consumer.find("ModuleStructSource_pkg::PARAMS") != std::string::npos;
    ok &= consumer.find("ModuleStructSource___transform(payload_in)") != std::string::npos;
    ok &= consumer.find("source.transform_member(payload_in)") != std::string::npos;
    ok &= consumer.find("ModuleStructSource___transform_member") == std::string::npos;
    ok &= source.find("ModuleStructSource_pkg::PARAMS") == std::string::npos;
    ok &= source.find("i < PARAMS") != std::string::npos;
    ok &= package.find("package ModuleStructSource_pkg;") != std::string::npos;
    ok &= package.find("PARAMS = 'h3") != std::string::npos;
    if (!ok) {
        std::print("ERROR: module static constexpr package output is incorrect\n");
    }
    return ok;
}

int main()
{
    bool ok = true;
#ifdef VERILATOR
    VERILATOR_MODEL dut;
    dut.clk = 0;
    dut.reset = 0;
    dut.payload_in = 0x121110u;
    dut.eval();
    if ((uint32_t)dut.payload_out != 0x151311u) {
        std::print("ERROR: Verilator payload is {:06x}, expected 151311\n",
            (uint32_t)dut.payload_out);
        ok = false;
    }
    if ((uint32_t)dut.member_payload_out != 0x242220u) {
        std::print("ERROR: Verilator member payload is {:06x}, expected 242220\n",
            (uint32_t)dut.member_payload_out);
        ok = false;
    }
#else
    ModuleStruct dut;
    ModuleStruct::Payload input{};
    size_t i;

    dut.payload_in = _ASSIGN_REG(input);
    dut._assign();
    for (i = 0; i < ModuleStructSource::PARAMS; ++i) {
        input.values[i] = (uint8_t)(0x10u + i);
    }
    for (i = 0; i < ModuleStructSource::PARAMS; ++i) {
        if ((uint8_t)dut.payload_out().values[i] != (uint8_t)(0x11u + i * 2u)) {
            std::print("ERROR: payload value {} is {}, expected {}\n", i,
                (uint32_t)dut.payload_out().values[i], (uint32_t)(0x11u + i * 2u));
            ok = false;
        }
        if ((uint8_t)dut.member_payload_out().values[i] != (uint8_t)(0x20u + i * 2u)) {
            std::print("ERROR: member payload value {} is {}, expected {}\n", i,
                (uint32_t)dut.member_payload_out().values[i], (uint32_t)(0x20u + i * 2u));
            ok = false;
        }
    }
#endif

    ok &= check_generated_sv();
#ifndef VERILATOR
    if (ok) {
        ok &= VerilatorCompile(__FILE__, "ModuleStruct", {"ModuleStructSource"}, {"../../../../include"});
    }
    if (ok) {
        ok &= SystemEcho("ModuleStruct/obj_dir/VModuleStruct") == 0;
    }
#endif
    return ok ? 0 : 1;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

struct NestedInterfaceWord
{
    uint16_t data:8;
    uint16_t valid:1;
} __PACKED;

template<size_t WIDTH>
struct NestedInterfacePayload
{
    logic<WIDTH> data;
    uint8_t tag;
} __PACKED;

struct PlainNestedIf : public Interface
{
    _PORT(NestedInterfaceWord) request_in;
    _PORT(NestedInterfaceWord) response_out;
};

template<size_t WIDTH>
struct ParameterizedNestedIf : public Interface
{
    _PORT(NestedInterfacePayload<WIDTH>) request_in;
    _PORT(NestedInterfacePayload<WIDTH>) response_out;
};

class NestedInterfaces : public Module
{
public:
    PlainNestedIf plain_if;
    ParameterizedNestedIf<16> parameterized_if;

    void _assign()
    {
        plain_if.response_out = _ASSIGN(plain_if.request_in());
        parameterized_if.response_out = _ASSIGN(parameterized_if.request_in());
    }

    void _work(bool) {}
    void _strobe() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <cstring>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static std::filesystem::path generated_file(const std::string& name)
{
#ifdef VERILATOR
    const auto copied = std::filesystem::path("NestedInterfaces") / name;
    return copied;
#else
    return std::filesystem::path("generated") / name;
#endif
}

static std::string read_generated(const std::string& name, bool& ok)
{
    const auto path = generated_file(name);
    std::ifstream input(path);
    if (!input) {
        std::print("ERROR: missing generated file {}\n", path.string());
        ok = false;
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static bool check_generated_sv()
{
    bool ok = true;
    const std::string module = read_generated("NestedInterfaces.sv", ok);
    const std::string plain_package = read_generated("NestedInterfaceWord_pkg.sv", ok);
    const std::string parameterized_package = read_generated("NestedInterfacePayload16_pkg.sv", ok);
    if (!ok) {
        return false;
    }

    const bool imports_plain =
        module.find("import NestedInterfaceWord_pkg::*;") != std::string::npos;
    const bool imports_parameterized =
        module.find("import NestedInterfacePayload16_pkg::*;") != std::string::npos;
    const bool declares_plain =
        module.find("NestedInterfaceWord plain_if__request_in") != std::string::npos;
    const bool declares_parameterized =
        module.find("NestedInterfacePayload16 parameterized_if__request_in") != std::string::npos;
    const bool plain_defined =
        plain_package.find("package NestedInterfaceWord_pkg;") != std::string::npos;
    const bool parameterized_defined =
        parameterized_package.find("package NestedInterfacePayload16_pkg;") != std::string::npos;

    if (!imports_plain || !imports_parameterized || !declares_plain || !declares_parameterized ||
        !plain_defined || !parameterized_defined) {
        std::print("ERROR: nested Interface struct package traversal is incomplete\n");
        std::print("       imports plain={}, parameterized={} declarations plain={}, parameterized={}\n",
            imports_plain, imports_parameterized, declares_plain, declares_parameterized);
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = check_generated_sv();
#ifndef VERILATOR
    if (ok && !noveril) {
        ok &= VerilatorCompile(__FILE__, "NestedInterfaces", {}, {"../../../../include"});
        ok = ok && SystemEcho("NestedInterfaces/obj_dir/VNestedInterfaces") == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
    VERILATOR_MODEL dut;
    dut.eval();
#endif
    return ok ? 0 : 1;
}

#endif

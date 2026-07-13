#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

struct AssignIfIndexedIf : public Interface
{
    _PORT(logic<32>) data_in;
    _PORT(logic<32>) data_out;

    AssignIfIndexedIf& operator=(AssignIfIndexedIf& other)
    {
        data_in = other.data_in;
        data_out = other.data_out;
        return *this;
    }
};

class AssignIfIndexedSource : public Module
{
public:
#ifdef SYNTHESIS
    array<9, AssignIfIndexedIf, true> ports_in;
#else
    AssignIfIndexedIf ports_in[9];
#endif

    void _assign() {}
    void _work(bool reset) {}
    void _strobe() {}
};

class AssignIfIndexedSink : public Module
{
public:
    AssignIfIndexedIf port_out;

    void _assign() {}
    void _work(bool reset) {}
    void _strobe() {}
};

class AssignIfIndexedProxy : public Module
{
    static constexpr size_t PORTS_CNT = 4;
    AssignIfIndexedSource source;
    AssignIfIndexedSink sink;

public:
    void _assign()
    {
        assignIf(source, sink, source.ports_in[PORTS_CNT * 2], sink.port_out);
    }

    void _work(bool reset) {}
    void _strobe() {}
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

long _system_clock = -1;

static bool check_generated_sv()
{
    std::filesystem::path path = "generated/AssignIfIndexedProxy.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(path)) {
        path = "AssignIfIndexedProxy/AssignIfIndexedProxy.sv";
    }
#endif

    std::ifstream in(path);
    if (!in) {
        std::print("\nERROR: can't open generated assignIf indexed-proxy module\n");
        return false;
    }
    std::string sv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    bool ok = true;
    auto require = [&](const std::string& text, const char* description) {
        if (sv.find(text) == std::string::npos) {
            std::print("\nERROR: missing {}: {}\n", description, text);
            ok = false;
        }
    };

    require("assign source__ports_in__data_in[PORTS_CNT*'h2]=sink__port_out__data_out;",
        "indexed input-side interface assignment");
    require("assign sink__port_out__data_in=source__ports_in__data_out[PORTS_CNT*'h2];",
        "indexed output-side interface assignment");
    if (sv.find("RecoveryExpr") != std::string::npos || sv.find("unknown:") != std::string::npos ||
        sv.find("assignIf(") != std::string::npos) {
        std::print("\nERROR: unresolved assignIf AST expression leaked into generated SV\n");
        ok = false;
    }
    return ok;
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
    if (!noveril) {
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "AssignIfIndexedProxy",
            {"Predef_pkg", "AssignIfIndexedSource", "AssignIfIndexedSink"},
            {"../../../../include"});
        auto compile_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        ok = ok && std::system("AssignIfIndexedProxy/obj_dir/VAssignIfIndexedProxy") == 0;
        std::print("Verilator compilation time: {} microseconds\n", compile_us);
    }
#else
    Verilated::commandArgs(argc, argv);
#endif
    return !ok;
}

#endif

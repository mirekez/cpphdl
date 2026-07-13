#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

struct ModuleArrayIf : public Interface
{
    _PORT(logic<32>) data_in;
    _PORT(logic<32>) data_out;

    ModuleArrayIf& operator=(ModuleArrayIf& other)
    {
        data_in = other.data_in;
        data_out = other.data_out;
        return *this;
    }
};

class InterfaceModuleArrayChild : public Module
{
public:
    ModuleArrayIf port_out;

    void _assign() {}
    void _work(bool reset) {}
    void _strobe() {}
};

class InterfaceModuleArraySource : public Module
{
    logic<32> source[8];

public:
    ModuleArrayIf ports_in[8];

    void _assign()
    {
        uint32_t i;
        for (i = 0; i < 8; ++i) {
            ports_in[i].data_out = _ASSIGN_REG_I(source[i]);
        }
    }

    void _work(bool reset) {}
    void _strobe() {}
};

class InterfaceModuleArray : public Module
{
    InterfaceModuleArrayChild direct_children[4];
    InterfaceModuleArrayChild connected_children[4];
    InterfaceModuleArraySource source;

public:
    void _assign()
    {
        uint32_t i;
        for (i = 0; i < 4; ++i) {
            if (i == 1) {
                direct_children[i].port_out.data_out = _ASSIGN(0x11);
            }
            else if (i == 3) {
                direct_children[i].port_out.data_out = _ASSIGN(0x33);
            }
            else {
                direct_children[i].port_out.data_out = _ASSIGN(0);
            }
            direct_children[i]._assign();
            connected_children[i].port_out = source.ports_in[4 + i];
            connected_children[i]._assign();
        }
        source._assign();
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
    std::filesystem::path top_path = "generated/InterfaceModuleArray.sv";
    std::filesystem::path child_path = "generated/InterfaceModuleArrayChild.sv";
#ifdef VERILATOR
    if (!std::filesystem::exists(top_path)) {
        top_path = "InterfaceModuleArray/InterfaceModuleArray.sv";
        child_path = "InterfaceModuleArray/InterfaceModuleArrayChild.sv";
    }
#endif

    std::ifstream top_in(top_path);
    std::ifstream child_in(child_path);
    if (!top_in || !child_in) {
        std::print("\nERROR: can't open generated interface module-array files\n");
        return false;
    }
    std::string top((std::istreambuf_iterator<char>(top_in)), std::istreambuf_iterator<char>());
    std::string child((std::istreambuf_iterator<char>(child_in)), std::istreambuf_iterator<char>());

    bool ok = true;
    auto require = [&](const std::string& text, const char* description) {
        if (top.find(text) == std::string::npos) {
            std::print("\nERROR: missing {}: {}\n", description, text);
            ok = false;
        }
    };

    require("wire[32-1:0] direct_children__port_out__data_in[4];",
        "flattened module-array interface declaration");
    require("wire[32-1:0] direct_children__port_out__data_out[4];",
        "opposite-direction flattened module-array interface declaration");
    require("assign direct_children__port_out__data_in[gi] = 'h11;",
        "conditional interface assignment for index 1");
    require("assign direct_children__port_out__data_in[gi] = 'h33;",
        "conditional interface assignment for index 3");
    require("assign connected_children__port_out__data_in[gi]=source__ports_in__data_out['h4 + gi];",
        "expanded whole-interface member assignment");

    if (top.find("__port_out[gi]__") != std::string::npos ||
        top.find("assign connected_children__port_out[gi]") != std::string::npos) {
        std::print("\nERROR: module-array index was emitted before the flattened interface member\n");
        ok = false;
    }
    if (top.find("assign direct_children__port_out__data_out[gi]") != std::string::npos) {
        std::print("\nERROR: output-facing interface assignment used the child-driven data_out signal\n");
        ok = false;
    }
    if (child.find("input wire[32-1:0] port_out__data_in") == std::string::npos ||
        child.find("output wire[32-1:0] port_out__data_out") == std::string::npos) {
        std::print("\nERROR: child interface member directions were not resolved correctly\n");
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
        ok &= VerilatorCompile(__FILE__, "InterfaceModuleArray",
            {"Predef_pkg", "InterfaceModuleArrayChild", "InterfaceModuleArraySource"},
            {"../../../../include"});
        auto compile_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        ok = ok && std::system("InterfaceModuleArray/obj_dir/VInterfaceModuleArray") == 0;
        std::print("Verilator compilation time: {} microseconds\n", compile_us);
    }
#else
    Verilated::commandArgs(argc, argv);
#endif
    return !ok;
}

#endif

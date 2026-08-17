#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

struct HierarchyValidReadyIf : public Interface
{
    _PORT(bool) valid_in;
    _PORT(logic<32>) data_in;
    _PORT(logic<32>) data_out;
    _PORT(bool) ready_out;
};

class HierarchySource : public Module
{
public:
    HierarchyValidReadyIf source_out;

    void _assign()
    {
        source_out.valid_in = _ASSIGN(true);
        source_out.data_in = _ASSIGN(0x12345678);
    }

    void _work(bool) {}
    void _strobe() {}
};

class HierarchySink : public Module
{
public:
    HierarchyValidReadyIf sink_in;

    void _assign()
    {
        sink_in.data_out = _ASSIGN(0xcafef00d);
        sink_in.ready_out = _ASSIGN(true);
    }

    void _work(bool) {}
    void _strobe() {}

#ifndef SYNTHESIS
    bool received()
    {
        return sink_in.valid_in() && sink_in.data_in() == logic<32>(0x12345678);
    }
#endif
};

class HierarchySourceProxy : public Module
{
    HierarchySource source;

public:
    HierarchyValidReadyIf source_out;

    void _assign()
    {
        assignIf(source, *this, source.source_out, source_out);
    }

    void _work(bool) {}
    void _strobe() {}

#ifndef SYNTHESIS
    bool accepted()
    {
        return source.source_out.ready_out() &&
            source.source_out.data_out() == logic<32>(0xcafef00d);
    }
#endif
};

class HierarchySinkProxy : public Module
{
    HierarchySink sink;

public:
    HierarchyValidReadyIf sink_in;

    void _assign()
    {
        assignIf(*this, sink, sink_in, sink.sink_in);
    }

    void _work(bool) {}
    void _strobe() {}

#ifndef SYNTHESIS
    bool received()
    {
        return sink.received();
    }
#endif
};

class AssignIfHierarchyProxy : public Module
{
    HierarchySourceProxy source;
    HierarchySinkProxy proxy;

public:
    void _assign()
    {
        source._assign();
        proxy._assign();
        assignIf(source, proxy, source.source_out, proxy.sink_in);
    }

    void _work(bool) {}
    void _strobe() {}

#ifndef SYNTHESIS
    bool passed()
    {
        return source.accepted() && proxy.received();
    }
#endif
};

class HierarchySourceArrayProxy : public Module
{
    static constexpr size_t COUNT = 2;
    HierarchySource sources[COUNT];

public:
#ifdef SYNTHESIS
    array<COUNT, HierarchyValidReadyIf, true> source_out;
#else
    HierarchyValidReadyIf source_out[COUNT];
#endif

    void _assign()
    {
        size_t i;
        for (i = 0; i < COUNT; ++i) {
            assignIf(sources[i], *this, sources[i].source_out, source_out[i]);
        }
    }

    void _work(bool) {}
    void _strobe() {}
};

class HierarchySinkArrayProxy : public Module
{
    static constexpr size_t COUNT = 2;
    HierarchySink sinks[COUNT];

public:
#ifdef SYNTHESIS
    array<COUNT, HierarchyValidReadyIf, true> sink_in;
#else
    HierarchyValidReadyIf sink_in[COUNT];
#endif

    void _assign()
    {
        size_t i;
        for (i = 0; i < COUNT; ++i) {
            assignIf(*this, sinks[i], sink_in[i], sinks[i].sink_in);
        }
    }

    void _work(bool) {}
    void _strobe() {}
};

class AssignIfHierarchyArrayProxy : public Module
{
    static constexpr size_t COUNT = 2;
    HierarchySourceArrayProxy source;
    HierarchySinkArrayProxy sink;

public:
    void _assign()
    {
        size_t i;
        source._assign();
        sink._assign();
        for (i = 0; i < COUNT; ++i) {
            assignIf(source, sink, source.source_out[i], sink.sink_in[i]);
        }
    }

    void _work(bool) {}
    void _strobe() {}

#ifndef SYNTHESIS
    bool passed()
    {
        size_t i;
        bool result;
        result = true;
        for (i = 0; i < COUNT; ++i) {
            result &= source.source_out[i].ready_out();
            result &= source.source_out[i].data_out() == logic<32>(0xcafef00d);
            result &= sink.sink_in[i].valid_in();
            result &= sink.sink_in[i].data_in() == logic<32>(0x12345678);
        }
        return result;
    }
#endif
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <print>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = 0;

static bool function_ref_copy_preserves_wiring()
{
    bool low;
    bool high;
    function_ref<bool> destination;
    function_ref<bool> replacement;
    function_ref<bool> unassigned;
    low = false;
    high = true;
    destination = [&]() { return low; };
    replacement = [&]() { return high; };

    bool passed = !destination();
    destination = replacement;
    passed &= destination();
    destination = unassigned;
    passed &= destination();
    function_ref<bool> copied(replacement);
    passed &= copied();
    return passed;
}

static bool has_unresolved_assign_if(const std::string& text)
{
    return text.find("RecoveryExpr") != std::string::npos ||
        text.find("unknown:") != std::string::npos ||
        text.find("assignIf(") != std::string::npos;
}

int main(int argc, char** argv)
{
#ifdef VERILATOR
    Verilated::commandArgs(argc, argv);
    VERILATOR_MODEL model;
    model.clk = 0;
    model.reset = 1;
    model.eval();
    return 0;
#else
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    AssignIfHierarchyProxy test;
    AssignIfHierarchyArrayProxy array_test;
    test._assign();
    array_test._assign();
    bool passed = function_ref_copy_preserves_wiring() && test.passed() && array_test.passed();

    std::ifstream sink_input(std::filesystem::path("generated") / "HierarchySinkProxy.sv");
    std::string sink_generated((std::istreambuf_iterator<char>(sink_input)),
        std::istreambuf_iterator<char>());
    passed &= sink_input.good() || sink_input.eof();
    passed &= sink_generated.find("assign sink__sink_in__valid_in=sink_in__valid_in;") != std::string::npos;
    passed &= sink_generated.find("assign sink__sink_in__data_in=sink_in__data_in;") != std::string::npos;
    passed &= sink_generated.find("assign sink_in__data_out=sink__sink_in__data_out;") != std::string::npos;
    passed &= sink_generated.find("assign sink_in__ready_out=sink__sink_in__ready_out;") != std::string::npos;
    passed &= !has_unresolved_assign_if(sink_generated);

    std::ifstream source_input(std::filesystem::path("generated") / "HierarchySourceProxy.sv");
    std::string source_generated((std::istreambuf_iterator<char>(source_input)),
        std::istreambuf_iterator<char>());
    passed &= source_input.good() || source_input.eof();
    passed &= source_generated.find("assign source_out__valid_out=source__source_out__valid_out;") != std::string::npos;
    passed &= source_generated.find("assign source_out__data_out=source__source_out__data_out;") != std::string::npos;
    passed &= source_generated.find("assign source__source_out__data_in=source_out__data_in;") != std::string::npos;
    passed &= source_generated.find("assign source__source_out__ready_in=source_out__ready_in;") != std::string::npos;
    passed &= !has_unresolved_assign_if(source_generated);

    std::ifstream array_sink_input(std::filesystem::path("generated") / "HierarchySinkArrayProxy.sv");
    std::string array_sink_generated((std::istreambuf_iterator<char>(array_sink_input)),
        std::istreambuf_iterator<char>());
    passed &= array_sink_input.good() || array_sink_input.eof();
    passed &= array_sink_generated.find(
        "assign sinks__sink_in__valid_in[gi]=sink_in__valid_in[gi];") != std::string::npos;
    passed &= array_sink_generated.find(
        "assign sinks__sink_in__data_in[gi]=sink_in__data_in[gi];") != std::string::npos;
    passed &= array_sink_generated.find(
        "assign sink_in__data_out[gi]=sinks__sink_in__data_out[gi];") != std::string::npos;
    passed &= array_sink_generated.find(
        "assign sink_in__ready_out[gi]=sinks__sink_in__ready_out[gi];") != std::string::npos;
    passed &= !has_unresolved_assign_if(array_sink_generated);

    std::ifstream array_source_input(std::filesystem::path("generated") / "HierarchySourceArrayProxy.sv");
    std::string array_source_generated((std::istreambuf_iterator<char>(array_source_input)),
        std::istreambuf_iterator<char>());
    passed &= array_source_input.good() || array_source_input.eof();
    passed &= array_source_generated.find(
        "assign source_out__valid_out[gi]=sources__source_out__valid_out[gi];") != std::string::npos;
    passed &= array_source_generated.find(
        "assign source_out__data_out[gi]=sources__source_out__data_out[gi];") != std::string::npos;
    passed &= array_source_generated.find(
        "assign sources__source_out__data_in[gi]=source_out__data_in[gi];") != std::string::npos;
    passed &= array_source_generated.find(
        "assign sources__source_out__ready_in[gi]=source_out__ready_in[gi];") != std::string::npos;
    passed &= !has_unresolved_assign_if(array_source_generated);

    std::ifstream array_top_input(std::filesystem::path("generated") / "AssignIfHierarchyArrayProxy.sv");
    std::string array_top_generated((std::istreambuf_iterator<char>(array_top_input)),
        std::istreambuf_iterator<char>());
    passed &= array_top_input.good() || array_top_input.eof();
    passed &= array_top_generated.find(
        "assign sink__sink_in__valid_in[gi]=source__source_out__valid_out[gi];") != std::string::npos;
    passed &= array_top_generated.find(
        "assign sink__sink_in__data_in[gi]=source__source_out__data_out[gi];") != std::string::npos;
    passed &= array_top_generated.find(
        "assign source__source_out__data_in[gi]=sink__sink_in__data_out[gi];") != std::string::npos;
    passed &= array_top_generated.find(
        "assign source__source_out__ready_in[gi]=sink__sink_in__ready_out[gi];") != std::string::npos;
    passed &= !has_unresolved_assign_if(array_top_generated);

    if (!noveril) {
        auto start = std::chrono::high_resolution_clock::now();
        passed &= VerilatorCompile(__FILE__, "AssignIfHierarchyArrayProxy",
            {"Predef_pkg", "HierarchySource", "HierarchySink",
                "HierarchySourceArrayProxy", "HierarchySinkArrayProxy"},
            {"../../../../include"});
        auto compile_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        passed = passed && std::system(
            "AssignIfHierarchyArrayProxy/obj_dir/VAssignIfHierarchyArrayProxy") == 0;
        std::print("Verilator compilation time: {} microseconds\n", compile_us);
    }
    return passed ? 0 : 1;
#endif
}

#endif

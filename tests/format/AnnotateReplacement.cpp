#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

#define CPPHDL_STR2(x) #x
#define CPPHDL_STR(x) CPPHDL_STR2(x)
#define CPPHDL_REPLACEMENT_MASK A5
#define CPPHDL_REPLACEMENT_SCRIPT_MASK 3C

struct AnnotateReplacementBadStruct
{
    double bad;
};

struct AnnotateReplacementScriptBadStruct
{
    double bad;
};

struct AnnotateReplacementFileBadStruct
{
    double bad;
};

struct AnnotateReplacementNestedLocal
{
    unsigned value:8;
} __PACKED;

struct AnnotateReplacementParentLocal
{
    unsigned value:8;
} __PACKED;

class [[clang::annotate(
    "CPPHDL_REPLACEMENT="
    "`default_nettype none\n"
    "\n"
    "module AnnotateReplacement (\n"
    "    input wire clk\n"
    ",   input wire reset\n"
    ",   input wire[8-1:0] value_in\n"
    ",   output wire[8-1:0] value_out\n"
    ");\n"
    "    // CPPHDL_ANNOTATE_REPLACEMENT_MARKER_" CPPHDL_STR(CPPHDL_REPLACEMENT_MASK) "\n"
    "    assign value_out = value_in ^ 8'h" CPPHDL_STR(CPPHDL_REPLACEMENT_MASK) ";\n"
    "endmodule\n"
    ";"
)]] AnnotateReplacement : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    u<8> value_comb;
    AnnotateReplacementBadStruct bad_struct;

    u<8>& value_comb_func()
    {
        bad_struct.bad = 1.0;
        return value_comb = value_in() ^ u<8>(0xa5);
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

class [[clang::annotate(
    "CPPHDL_REPLACEMENT_SCRIPT=AnnotateReplacementScript.sh "
    CPPHDL_STR(CPPHDL_REPLACEMENT_SCRIPT_MASK)
    ";"
)]]
AnnotateReplacementScript : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    u<8> value_comb;
    AnnotateReplacementScriptBadStruct bad_struct;

    u<8>& value_comb_func()
    {
        bad_struct.bad = 2.0;
        return value_comb = value_in() ^ u<8>(0x3c);
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

class [[clang::annotate("CPPHDL_REPLACEMENT_FILE=AnnotateReplacementFile.sv;")]]
AnnotateReplacementFile : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    u<8> value_comb;
    AnnotateReplacementFileBadStruct bad_struct;

    u<8>& value_comb_func()
    {
        bad_struct.bad = 3.0;
        return value_comb = value_in() ^ u<8>(0x5a);
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

class AnnotateReplacementNested : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    AnnotateReplacement child;
    AnnotateReplacementNestedLocal local;
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0xa5);
    }

public:
    void _assign()
    {
        child.value_in = value_in;
        child.__inst_name = __inst_name + "/child";
        child._assign();
    }

    void _work(bool reset)
    {
        local.value = value_in();
        child._work(reset);
    }

    void _strobe()
    {
        child._strobe();
    }
};

class AnnotateReplacementParent : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    AnnotateReplacementNested nested;
    AnnotateReplacementParentLocal local;
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0xa5);
    }

public:
    void _assign()
    {
        nested.value_in = value_in;
        nested.__inst_name = __inst_name + "/nested";
        nested._assign();
    }

    void _work(bool reset)
    {
        local.value = value_in();
        nested._work(reset);
    }

    void _strobe()
    {
        nested._strobe();
    }
};

class AnnotateReplacementScriptNested : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    AnnotateReplacementScript child;
    AnnotateReplacementNestedLocal local;
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0x3c);
    }

public:
    void _assign()
    {
        child.value_in = value_in;
        child.__inst_name = __inst_name + "/child";
        child._assign();
    }

    void _work(bool reset)
    {
        local.value = value_in();
        child._work(reset);
    }

    void _strobe()
    {
        child._strobe();
    }
};

class AnnotateReplacementScriptParent : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    AnnotateReplacementScriptNested nested;
    AnnotateReplacementParentLocal local;
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0x3c);
    }

public:
    void _assign()
    {
        nested.value_in = value_in;
        nested.__inst_name = __inst_name + "/nested";
        nested._assign();
    }

    void _work(bool reset)
    {
        local.value = value_in();
        nested._work(reset);
    }

    void _strobe()
    {
        nested._strobe();
    }
};

class AnnotateReplacementFileNested : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    AnnotateReplacementFile child;
    AnnotateReplacementNestedLocal local;
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0x5a);
    }

public:
    void _assign()
    {
        child.value_in = value_in;
        child.__inst_name = __inst_name + "/child";
        child._assign();
    }

    void _work(bool reset)
    {
        local.value = value_in();
        child._work(reset);
    }

    void _strobe()
    {
        child._strobe();
    }
};

class AnnotateReplacementFileParent : public Module
{
public:
    __PORT(u<8>) value_in;
    __PORT(u<8>) value_out = __VAR(value_comb_func());

private:
    AnnotateReplacementFileNested nested;
    AnnotateReplacementParentLocal local;
    u<8> value_comb;

    u<8>& value_comb_func()
    {
        return value_comb = value_in() ^ u<8>(0x5a);
    }

public:
    void _assign()
    {
        nested.value_in = value_in;
        nested.__inst_name = __inst_name + "/nested";
        nested._assign();
    }

    void _work(bool reset)
    {
        local.value = value_in();
        nested._work(reset);
    }

    void _strobe()
    {
        nested._strobe();
    }
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

static bool generated_sv_is_replacement(const std::filesystem::path& sv_path,
    const std::string& marker)
{
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool has_marker = text.find(marker) != std::string::npos;
    bool has_normal_import = text.find("import Predef_pkg::*;") != std::string::npos;
    bool has_cpphdl_regs_comment = text.find("// regs and combs") != std::string::npos;

    if (!has_marker || has_normal_import || has_cpphdl_regs_comment) {
        std::print("\nERROR: generated SV was not replaced: marker={}, import={}, regs_comment={}\n",
            has_marker, has_normal_import, has_cpphdl_regs_comment);
        return false;
    }
    return true;
}

static bool generated_sv_lacks_import(const std::filesystem::path& sv_path,
    const std::string& forbidden_import)
{
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find(forbidden_import) != std::string::npos) {
        std::print("\nERROR: generated SV {} leaked child replacement import {}\n",
            sv_path.string(), forbidden_import);
        return false;
    }
    return true;
}

template <typename NativeDut>
class TestAnnotateReplacement : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    NativeDut dut;
#endif

    u<8> value;
    bool error = false;
    const char* label;
    uint8_t mask;
    std::filesystem::path generated_path;
    std::string marker;
    std::filesystem::path parent_path;
    std::filesystem::path nested_path;
    std::string forbidden_import;

public:
    TestAnnotateReplacement(const char* label,
        uint8_t mask,
        std::filesystem::path generated_path,
        std::string marker,
        std::filesystem::path parent_path,
        std::filesystem::path nested_path,
        std::string forbidden_import)
        : label(label)
        , mask(mask)
        , generated_path(std::move(generated_path))
        , marker(std::move(marker))
        , parent_path(std::move(parent_path))
        , nested_path(std::move(nested_path))
        , forbidden_import(std::move(forbidden_import))
    {
    }

    void _assign()
    {
#ifndef VERILATOR
        dut.value_in = __VAR(value);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.value_in = value;
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

    u<8> value_out()
    {
#ifdef VERILATOR
        return u<8>(dut.value_out);
#else
        return dut.value_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR {}...", label);
#else
        std::print("CppHDL {}...", label);
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "annotate_replacement_test";
        _assign();

        error |= !generated_sv_is_replacement(generated_path, marker);
        error |= !generated_sv_lacks_import(parent_path, forbidden_import);
        error |= !generated_sv_lacks_import(nested_path, forbidden_import);

        for (uint32_t i = 0; i < 256 && !error; ++i) {
            value = u<8>(i);
            eval(false);
            u<8> expected = u<8>(i ^ mask);
            if (value_out() != expected) {
                std::print("\nvalue ERROR at {}: got {}, expected {}\n", i, value_out(), expected);
                error = true;
            }
            neg(false);
            ++sys_clock;
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
        ok &= VerilatorCompile(__FILE__, "AnnotateReplacementParent", {
            "Predef_pkg",
            "AnnotateReplacementNestedLocal_pkg",
            "AnnotateReplacementParentLocal_pkg",
            "AnnotateReplacement",
            "AnnotateReplacementNested"
        }, {"../../../../include"}, 1);
        setenv("CPPHDL_VERILATOR_CFLAGS", "-DCPPHDL_SCRIPT_REPLACEMENT_TOP", 1);
        ok &= VerilatorCompile(__FILE__, "AnnotateReplacementScriptParent", {
            "Predef_pkg",
            "AnnotateReplacementNestedLocal_pkg",
            "AnnotateReplacementParentLocal_pkg",
            "AnnotateReplacementScript",
            "AnnotateReplacementScriptNested"
        }, {"../../../../include"}, 1);
        setenv("CPPHDL_VERILATOR_CFLAGS", "-DCPPHDL_FILE_REPLACEMENT_TOP", 1);
        ok &= VerilatorCompile(__FILE__, "AnnotateReplacementFileParent", {
            "Predef_pkg",
            "AnnotateReplacementNestedLocal_pkg",
            "AnnotateReplacementParentLocal_pkg",
            "AnnotateReplacementFile",
            "AnnotateReplacementFileNested"
        }, {"../../../../include"}, 1);
        unsetenv("CPPHDL_VERILATOR_CFLAGS");
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("AnnotateReplacementParent_1/obj_dir/VAnnotateReplacementParent") == 0;
        ok = ok && std::system("AnnotateReplacementScriptParent_1/obj_dir/VAnnotateReplacementScriptParent") == 0;
        ok = ok && std::system("AnnotateReplacementFileParent_1/obj_dir/VAnnotateReplacementFileParent") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

#ifdef VERILATOR
#ifdef CPPHDL_FILE_REPLACEMENT_TOP
    return !(ok && TestAnnotateReplacement<AnnotateReplacementFileParent>(
        "TestAnnotateReplacementFile",
        0x5a,
        "AnnotateReplacementFileParent_1/AnnotateReplacementFile.sv",
        "CPPHDL_ANNOTATE_REPLACEMENT_FILE_MARKER",
        "AnnotateReplacementFileParent_1/AnnotateReplacementFileParent.sv",
        "AnnotateReplacementFileParent_1/AnnotateReplacementFileNested.sv",
        "import AnnotateReplacementFileBadStruct_pkg::*;").run());
#elif defined(CPPHDL_SCRIPT_REPLACEMENT_TOP)
    return !(ok && TestAnnotateReplacement<AnnotateReplacementScriptParent>(
        "TestAnnotateReplacementScript",
        0x3c,
        "AnnotateReplacementScriptParent_1/AnnotateReplacementScript.sv",
        "CPPHDL_ANNOTATE_REPLACEMENT_SCRIPT_MARKER_3C",
        "AnnotateReplacementScriptParent_1/AnnotateReplacementScriptParent.sv",
        "AnnotateReplacementScriptParent_1/AnnotateReplacementScriptNested.sv",
        "import AnnotateReplacementScriptBadStruct_pkg::*;").run());
#else
    return !(ok && TestAnnotateReplacement<AnnotateReplacementParent>(
        "TestAnnotateReplacement",
        0xa5,
        "AnnotateReplacementParent_1/AnnotateReplacement.sv",
        "CPPHDL_ANNOTATE_REPLACEMENT_MARKER_A5",
        "AnnotateReplacementParent_1/AnnotateReplacementParent.sv",
        "AnnotateReplacementParent_1/AnnotateReplacementNested.sv",
        "import AnnotateReplacementBadStruct_pkg::*;").run());
#endif
#else
    ok = ok && TestAnnotateReplacement<AnnotateReplacementParent>(
        "TestAnnotateReplacement",
        0xa5,
        "generated/AnnotateReplacement.sv",
        "CPPHDL_ANNOTATE_REPLACEMENT_MARKER_A5",
        "generated/AnnotateReplacementParent.sv",
        "generated/AnnotateReplacementNested.sv",
        "import AnnotateReplacementBadStruct_pkg::*;").run();
    ok = ok && TestAnnotateReplacement<AnnotateReplacementScriptParent>(
        "TestAnnotateReplacementScript",
        0x3c,
        "generated/AnnotateReplacementScript.sv",
        "CPPHDL_ANNOTATE_REPLACEMENT_SCRIPT_MARKER_3C",
        "generated/AnnotateReplacementScriptParent.sv",
        "generated/AnnotateReplacementScriptNested.sv",
        "import AnnotateReplacementScriptBadStruct_pkg::*;").run();
    ok = ok && TestAnnotateReplacement<AnnotateReplacementFileParent>(
        "TestAnnotateReplacementFile",
        0x5a,
        "generated/AnnotateReplacementFile.sv",
        "CPPHDL_ANNOTATE_REPLACEMENT_FILE_MARKER",
        "generated/AnnotateReplacementFileParent.sv",
        "generated/AnnotateReplacementFileNested.sv",
        "import AnnotateReplacementFileBadStruct_pkg::*;").run();
    return !ok;
#endif
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

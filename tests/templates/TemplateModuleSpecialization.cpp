#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>
#include <string.h>

using namespace cpphdl;

struct TemplateModuleSpecWide
{
    static constexpr uint16_t MASK = 0x005a;
};

struct TemplateModuleSpecNarrow
{
    static constexpr uint16_t MASK = 0x000f;
};

inline constexpr char TemplateModuleSpecRx[] = "rx";
inline constexpr char TemplateModuleSpecTx[] = "tx";

template<size_t SCALE, typename FMT, const char* TAG>
class TemplateModuleLeaf : public Module
{
public:
    static constexpr uint16_t TYPE_MASK = FMT::MASK;

    _PORT(logic<16>) data_in;
    _PORT(logic<16>) data_out = _ASSIGN_COMB(data_comb_func());

private:
    logic<16> data_comb;

public:
    logic<16>& data_comb_func()
    {
        data_comb = logic<16>((uint16_t)((uint16_t)data_in() + SCALE + TYPE_MASK));
        return data_comb;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class TemplateModuleSpecialization : public Module
{
public:
    _PORT(logic<16>) data_in;
    _PORT(logic<16>) wide_small_out = _ASSIGN(wide_small.data_out());
    _PORT(logic<16>) wide_large_out = _ASSIGN(wide_large.data_out());
    _PORT(logic<16>) narrow_text_out = _ASSIGN(narrow_text.data_out());

private:
    TemplateModuleLeaf<3, TemplateModuleSpecWide, TemplateModuleSpecRx> wide_small;
    TemplateModuleLeaf<7, TemplateModuleSpecWide, TemplateModuleSpecRx> wide_large;
    TemplateModuleLeaf<5, TemplateModuleSpecNarrow, TemplateModuleSpecTx> narrow_text;

public:
    void _assign()
    {
        wide_small.data_in = data_in;
        wide_large.data_in = data_in;
        narrow_text.data_in = data_in;
        wide_small._assign();
        wide_large._assign();
        narrow_text._assign();
    }

    void _work(bool reset)
    {
        wide_small._work(reset);
        wide_large._work(reset);
        narrow_text._work(reset);
    }

    void _strobe()
    {
        wide_small._strobe();
        wide_large._strobe();
        narrow_text._strobe();
    }
};

template class TemplateModuleLeaf<3, TemplateModuleSpecWide, TemplateModuleSpecRx>;
template class TemplateModuleLeaf<7, TemplateModuleSpecWide, TemplateModuleSpecRx>;
template class TemplateModuleLeaf<5, TemplateModuleSpecNarrow, TemplateModuleSpecTx>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <set>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static std::filesystem::path generated_dir()
{
    std::filesystem::path copied = "TemplateModuleSpecialization_1";
    if (std::filesystem::exists(copied)) {
        return copied;
    }
    return "generated";
}

static std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static bool check_generated_sv()
{
    const std::filesystem::path dir = generated_dir();
    bool ok = true;
    std::set<std::string> leaf_modules;

    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string file_name = entry.path().filename().string();
        if (file_name.rfind("TemplateModuleLeaf", 0) != 0
            || entry.path().extension() != ".sv") {
            continue;
        }

        std::string text = read_file(entry.path());
        const size_t module_pos = text.find("module TemplateModuleLeaf");
        if (module_pos != std::string::npos) {
            const size_t name_begin = module_pos + strlen("module ");
            const size_t name_end = text.find_first_of(" #(\n", name_begin);
            leaf_modules.insert(text.substr(name_begin, name_end - name_begin));
        }

        require(text.find("parameter SCALE") != std::string::npos,
            "numeric template argument SCALE was not preserved as a module parameter");
        require(text.find("parameter TAG") == std::string::npos,
            "text template argument TAG was emitted as a module parameter");
        require(text.find("parameter FMT") == std::string::npos,
            "type template argument FMT was emitted as a module parameter");
        require(text.find("FMT_pkg::") == std::string::npos,
            "type template parameter leaked as FMT_pkg");
        require(text.find("TAG[") == std::string::npos,
            "text template parameter leaked into generated code");
        require(text.find("unknown") == std::string::npos,
            "generated leaf module contains unresolved expression");
        require(file_name.find("Leaf3") == std::string::npos
                && file_name.find("Leaf5") == std::string::npos
                && file_name.find("Leaf7") == std::string::npos,
            "numeric template argument was added to leaf module file name");
    }

    require(leaf_modules.size() == 2,
        "module specializations should be split by type/text arguments only");

    const std::filesystem::path top_path = dir / "TemplateModuleSpecialization.sv";
    require(std::filesystem::exists(top_path), "top module SystemVerilog was not generated");
    if (std::filesystem::exists(top_path)) {
        const std::string top = read_file(top_path);
        require(top.find("#(\n        3\n    ) wide_small") != std::string::npos,
            "parent did not instantiate SCALE=3");
        require(top.find("#(\n        7\n    ) wide_large") != std::string::npos,
            "parent did not instantiate SCALE=7");
    }

    return ok;
}

static uint16_t expected_wide_small(uint16_t value)
{
    return (uint16_t)(value + 3 + TemplateModuleSpecWide::MASK);
}

static uint16_t expected_wide_large(uint16_t value)
{
    return (uint16_t)(value + 7 + TemplateModuleSpecWide::MASK);
}

static uint16_t expected_narrow_text(uint16_t value)
{
    return (uint16_t)(value + 5 + TemplateModuleSpecNarrow::MASK);
}

class TestTemplateModuleSpecialization : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateModuleSpecialization dut;
#endif

    logic<16> data = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.data_in = _ASSIGN_REG(data);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.data_in = (uint16_t)data;
        dut.clk = 1;
        dut.reset = reset;
        dut.eval();
#else
        dut._work(reset);
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTemplateModuleSpecialization...");
#else
        std::print("CppHDL TestTemplateModuleSpecialization...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "template_module_specialization_test";
        _assign();

        error |= !check_generated_sv();

        for (uint32_t i = 0; i < 512 && !error; ++i) {
            uint16_t sample = (uint16_t)((i * 109u) ^ 0x7123u);
            data = logic<16>(sample);
            eval(false);

#ifdef VERILATOR
            uint16_t got_small = (uint16_t)dut.wide_small_out;
            uint16_t got_large = (uint16_t)dut.wide_large_out;
            uint16_t got_text = (uint16_t)dut.narrow_text_out;
#else
            uint16_t got_small = (uint16_t)dut.wide_small_out();
            uint16_t got_large = (uint16_t)dut.wide_large_out();
            uint16_t got_text = (uint16_t)dut.narrow_text_out();
#endif

            if (got_small != expected_wide_small(sample)
                || got_large != expected_wide_large(sample)
                || got_text != expected_narrow_text(sample)) {
                std::print("\nmodule specialization ERROR sample=0x{:04x}: got=0x{:04x}/0x{:04x}/0x{:04x}\n",
                    sample, got_small, got_large, got_text);
                error = true;
            }
            ++_system_clock;
        }

        auto stop = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        std::print(" {} ({} microseconds)\n", error ? "FAILED" : "PASSED", us);
        return error;
    }
};

int main()
{
    TestTemplateModuleSpecialization test;
    return test.run() ? 1 : 0;
}

#endif

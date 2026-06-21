#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <stdint.h>

using namespace cpphdl;

struct TemplateStructSpecSmall
{
    static constexpr size_t FIELD_BITS = 5;
};

struct TemplateStructSpecLarge
{
    static constexpr size_t FIELD_BITS = 9;
};

inline constexpr char TemplateStructSpecAlpha[] = "alpha";
inline constexpr char TemplateStructSpecBeta[] = "beta";

template<size_t WIDTH, typename FMT, const char* TAG>
struct TemplateStructPayload
{
    static constexpr size_t WIDTH_VALUE = WIDTH;
    static constexpr size_t FORMAT_BITS = FMT::FIELD_BITS;
    static constexpr uint16_t TAG_FIRST = (uint16_t)TAG[0];

    uint16_t raw;
    uint16_t selected : WIDTH;
    uint16_t formatted : FMT::FIELD_BITS;
} __PACKED;

class TemplateStructSpecialization : public Module
{
public:
    _PORT(TemplateStructPayload<3, TemplateStructSpecSmall, TemplateStructSpecAlpha>) small_in;
    _PORT(TemplateStructPayload<6, TemplateStructSpecLarge, TemplateStructSpecBeta>) large_in;
    _PORT(logic<16>) small_raw_out = _ASSIGN_COMB(small_comb_func());
    _PORT(logic<16>) large_raw_out = _ASSIGN_COMB(large_comb_func());

private:
    logic<16> small_comb;
    logic<16> large_comb;

public:
    logic<16>& small_comb_func()
    {
        TemplateStructPayload<3, TemplateStructSpecSmall, TemplateStructSpecAlpha> payload;

        payload = small_in();
        small_comb = logic<16>((uint16_t)(payload.raw + payload.selected + payload.formatted
            + TemplateStructPayload<3, TemplateStructSpecSmall, TemplateStructSpecAlpha>::WIDTH_VALUE
            + TemplateStructPayload<3, TemplateStructSpecSmall, TemplateStructSpecAlpha>::FORMAT_BITS
            + TemplateStructPayload<3, TemplateStructSpecSmall, TemplateStructSpecAlpha>::TAG_FIRST));
        return small_comb;
    }

    logic<16>& large_comb_func()
    {
        TemplateStructPayload<6, TemplateStructSpecLarge, TemplateStructSpecBeta> payload;

        payload = large_in();
        large_comb = logic<16>((uint16_t)(payload.raw + payload.selected + payload.formatted
            + TemplateStructPayload<6, TemplateStructSpecLarge, TemplateStructSpecBeta>::WIDTH_VALUE
            + TemplateStructPayload<6, TemplateStructSpecLarge, TemplateStructSpecBeta>::FORMAT_BITS
            + TemplateStructPayload<6, TemplateStructSpecLarge, TemplateStructSpecBeta>::TAG_FIRST));
        return large_comb;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include <vector>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static std::filesystem::path generated_dir()
{
    std::filesystem::path copied = "TemplateStructSpecialization_1";
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
    size_t payload_packages = 0;

    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    require(!std::filesystem::exists(dir / "TemplateStructPayload_pkg.sv"),
        "unspecialized template struct package was generated");

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string file_name = entry.path().filename().string();
        if (file_name.find("TemplateStructPayload") == std::string::npos
            || file_name.find("_pkg.sv") == std::string::npos) {
            continue;
        }

        ++payload_packages;
        const std::string text = read_file(entry.path());
        require(text.find("WIDTH_VALUE") != std::string::npos,
            "specialized struct WIDTH_VALUE constexpr is missing");
        require(text.find("FORMAT_BITS") != std::string::npos,
            "specialized struct FORMAT_BITS constexpr is missing");
        require(text.find("TAG_FIRST") != std::string::npos,
            "specialized struct TAG_FIRST constexpr is missing");
        require(text.find("WIDTH") == std::string::npos || text.find("WIDTH_VALUE") != std::string::npos,
            "numeric template parameter leaked into specialized struct package");
        require(text.find("FMT") == std::string::npos,
            "type template parameter leaked into specialized struct package");
        require(text.find("TAG[") == std::string::npos,
            "text template parameter leaked into specialized struct package");
        require(text.find("unknown") == std::string::npos,
            "specialized struct package contains unresolved expression");
        require(file_name.find("3") != std::string::npos || file_name.find("6") != std::string::npos,
            "numeric struct template parameter was not included in package name");
        require(file_name.find("alpha") != std::string::npos || file_name.find("beta") != std::string::npos,
            "text struct template parameter was not included in package name");
    }

    require(payload_packages == 2,
        "struct template should produce exactly two concrete specialization packages");
    return ok;
}

class TestTemplateStructSpecialization : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TemplateStructSpecialization dut;
#endif

    bool error = false;

public:
    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestTemplateStructSpecialization...");
#else
        std::print("CppHDL TestTemplateStructSpecialization...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "template_struct_specialization_test";

        error |= !check_generated_sv();

#ifdef VERILATOR
        dut.eval();
#else
        dut._assign();
        dut._work(false);
#endif
        ++_system_clock;

        auto stop = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        std::print(" {} ({} microseconds)\n", error ? "FAILED" : "PASSED", us);
        return error;
    }
};

int main()
{
    TestTemplateStructSpecialization test;
    return test.run() ? 1 : 0;
}

#endif

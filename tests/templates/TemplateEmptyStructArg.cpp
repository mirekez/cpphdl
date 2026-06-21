#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"

#include <stdint.h>

using namespace cpphdl;

struct TemplateEmptyStructArgFullState
{
    uint16_t value;
};

template<typename TAG>
struct TemplateEmptyStructArgStage
{
    struct State
    {
    };
};

template<typename... STATES>
struct TemplateEmptyStructArgBigState : STATES...
{
};

class TemplateEmptyStructArg : public Module
{
public:
    using EmptyState = TemplateEmptyStructArgStage<int>::State;
    using BigState = TemplateEmptyStructArgBigState<TemplateEmptyStructArgFullState, EmptyState>;

    _PORT(BigState) state_in;
    _PORT(logic<16>) value_out = _ASSIGN_COMB(value_comb_func());

private:
    logic<16> value_comb;

public:
    logic<16>& value_comb_func()
    {
        BigState state;

        state = state_in();
        value_comb = state.value;
        return value_comb;
    }

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>

long _system_clock = -1;

static std::filesystem::path generated_dir()
{
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
    bool found_empty_state_pkg = false;
    bool found_big_state_pkg = false;

    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            std::print("\nERROR: {}\n", message);
            ok = false;
        }
    };

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string file_name = entry.path().filename().string();
        if (file_name.find("_pkg.sv") == std::string::npos) {
            continue;
        }

        const std::string text = read_file(entry.path());
        if (file_name == "TemplateEmptyStructArgStageint_State_pkg.sv") {
            found_empty_state_pkg = true;
            require(text.find("_pad") != std::string::npos,
                "empty nested state package does not contain generated pad field");
        }
        if (file_name.find("TemplateEmptyStructArgBigState") != std::string::npos) {
            found_big_state_pkg = true;
            require(file_name.find("TemplateEmptyStructArgStageint_State") != std::string::npos,
                "aggregate specialization name did not include the empty nested state type");
        }
    }

    require(found_empty_state_pkg, "empty nested state package was not generated");
    require(found_big_state_pkg, "aggregate state package was not generated");
    return ok;
}

class TestTemplateEmptyStructArg : Module
{
    TemplateEmptyStructArg dut;
    bool error = false;

public:
    bool run()
    {
        std::print("CppHDL TestTemplateEmptyStructArg...");
        auto start = std::chrono::high_resolution_clock::now();

        dut._assign();
        dut._work(false);
        ++_system_clock;

        error |= !check_generated_sv();

        auto stop = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        std::print(" {} ({} microseconds)\n", error ? "FAILED" : "PASSED", us);
        return error;
    }
};

int main()
{
    TestTemplateEmptyStructArg test;
    return test.run() ? 1 : 0;
}

#endif

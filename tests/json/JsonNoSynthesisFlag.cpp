#include "cpphdl.h"

// This module deliberately exists only when SYNTHESIS is not predefined.
// It reproduces JSON extraction losing simulation-only declarations.
// The test confirms --no-synthesis-flag exposes the guarded module and ports.
#if !defined(SYNTHESIS)

using namespace cpphdl;

class JsonAliasLeaf : public Module
{
public:
    _PORT(bool) request_in;
    _PORT(bool) response_out;

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class JsonTestHarness : public Module
{
    JsonAliasLeaf dut;
    reg<u1> request_reg;
    bool response_value = false;

public:
    _PORT(bool) trigger_in;
    _PORT(bool) observed_out;

    void _work(bool)
    {
        response_value = dut.response_out();
    }
    void _strobe() {}
    void _assign()
    {
        dut.request_in = _ASSIGN_REG(request_reg);
        dut._assign();
    }
};

#endif

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

long _system_clock = -1;

int main()
{
    const std::filesystem::path jsonPath = "generated/JsonNoSynthesisFlag.json";
    std::ifstream input(jsonPath);
    if (!input) {
        std::cerr << "can't open " << jsonPath << "\n";
        return 1;
    }

    const std::string text(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (text.find("\"JsonTestHarness\"") == std::string::npos ||
            text.find("\"trigger_in\"") == std::string::npos ||
            text.find("\"observed_out\"") == std::string::npos ||
            text.find("\"request_reg\": {\"hide_name\": 0, \"bits\": [6]") == std::string::npos ||
            text.find("\"dut__request_in\": {\"hide_name\": 0, \"bits\": [6]") == std::string::npos ||
            text.find("\"response_value\": {\"hide_name\": 0, \"bits\": [7]") == std::string::npos ||
            text.find("\"dut__response_out\": {\"hide_name\": 0, \"bits\": [7]") == std::string::npos) {
        std::cerr << "test-only module is missing from JSON output\n";
        return 1;
    }
    return 0;
}

#endif

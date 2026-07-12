#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "../../tribe/common/Axi4.h"

using namespace cpphdl;

struct JsonPayload
{
    bool valid;
    u<8> tag;
    logic<7> data;
};

class JsonLeaf : public Module
{
public:
    _PORT(bool) start_in;
    _PORT(JsonPayload) payload_in;
    Axi4If<16, 4, 32> axi;
    _PORT(logic<17>) result_out = _ASSIGN_COMB(result_comb_func());

private:
    logic<17> result_comb;

    logic<17>& result_comb_func()
    {
        result_comb = logic<17>(payload_in().data) | (logic<17>(payload_in().tag) << 7);
        if (start_in()) {
            result_comb[16] = 1;
        }
        return result_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

class JsonParent : public Module
{
public:
    _PORT(bool) start_in;
    _PORT(JsonPayload) payload_in;
    Axi4If<16, 4, 32> axi;
    _PORT(logic<17>) result_out = _ASSIGN_COMB(result_comb_func());

private:
    JsonLeaf leaf;
    logic<17> result_comb;

    logic<17>& result_comb_func()
    {
        result_comb = leaf.result_out();
        return result_comb;
    }

public:
    void _assign()
    {
        leaf.start_in = start_in;
        leaf.payload_in = payload_in;
        leaf._assign();
    }

    void _work(bool reset)
    {
        leaf._work(reset);
    }

    void _strobe()
    {
        leaf._strobe();
    }
};

/////////////////////////////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

long _system_clock = -1;

static bool contains(const std::string& text, const std::string& needle)
{
    if (text.find(needle) != std::string::npos) {
        return true;
    }
    std::cerr << "missing JSON fragment: " << needle << "\n";
    return false;
}

int main()
{
    const std::filesystem::path jsonPath = "generated/json_output.json";
    std::ifstream in(jsonPath);
    if (!in) {
        std::cerr << "can't open " << jsonPath << "\n";
        return 1;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> expected = {
        "\"creator\": \"cpphdl\"",
        "\"modules\":",
        "\"JsonLeaf\":",
        "\"JsonParent\":",
        "\"start_in\": {\"direction\": \"input\"",
        "\"payload_in\": {\"direction\": \"input\"",
        "\"payload_in\": {\"direction\": \"input\", \"bits\": [5, 6, 7, 8",
        "\"result_out\": {\"direction\": \"output\"",
        "\"axi__awvalid_in\": {\"direction\": \"input\"",
        "\"axi__awready_out\": {\"direction\": \"output\"",
        "\"axi__awaddr_in\": {\"direction\": \"input\", \"bits\": [31, 32, 33, 34",
        "\"axi__wdata_in\": {\"direction\": \"input\"",
        "\"axi__wdata_in\": {\"direction\": \"input\", \"bits\": [53, 54, 55, 56",
        "\"axi__wstrb_in\": {\"direction\": \"input\", \"bits\": [85, 86, 87, 88]",
        "\"leaf\":",
        "\"type\": \"JsonLeaf\"",
        "\"connections\":",
        "\"start_in\": [4]",
        "\"payload_in\": [5, 6, 7, 8",
        "\"leaf__start_in\"",
        "\"leaf__start_in\": {\"hide_name\": 0, \"bits\": [4]",
        "\"leaf__payload_in\"",
        "\"leaf__payload_in\": {\"hide_name\": 0, \"bits\": [5, 6, 7, 8",
        "\"leaf__axi__awvalid_in\"",
        "\"netnames\":"
    };

    for (const auto& needle : expected) {
        if (!contains(text, needle)) {
            return 1;
        }
    }

    return 0;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

// Regression: a child Module name may be a textual prefix of its parent name.
// The child remains a separate module and its methods/fields must not leak into the parent.
class PrefixCore : public Module
{
    logic<8> value_comb;

    logic<8>& value_comb_func()
    {
        value_comb = (logic<8>)value_in();
        return value_comb;
    }

public:
    _PORT(logic<8>) value_in;
    _PORT(logic<8>) value_out = _ASSIGN_COMB(value_comb_func());

    void _assign() {}
    void _work(bool) {}
    void _strobe() {}
};

template<size_t COUNT>
class PrefixCoreCluster : public Module
{
public:
    PrefixCore cores[COUNT];
    _PORT(logic<8>) value_in;
    _PORT(logic<8>) value_out = _ASSIGN(cores[0].value_out());

    void _assign()
    {
        cores[0].value_in = value_in;
        cores[0]._assign();
    }

    void _work(bool reset)
    {
        cores[0]._work(reset);
    }

    void _strobe()
    {
        cores[0]._strobe();
    }
};

template class PrefixCoreCluster<1>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <filesystem>
#include <fstream>
#include <print>
#include <string>

long _system_clock = -1;

int main()
{
    const std::filesystem::path path = "generated/PrefixCoreCluster.sv";
    std::ifstream input(path);
    if (!input) {
        std::print("ERROR: cannot open {}\n", path.string());
        return 1;
    }

    const std::string sv{std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const bool separate_child = sv.find("PrefixCore") != std::string::npos;
    const bool no_child_method = sv.find("value_comb_func") == std::string::npos;
    const bool no_child_storage = sv.find("value_comb;") == std::string::npos;
    if (!separate_child || !no_child_method || !no_child_storage) {
        std::print("ERROR: prefix-named child leaked into parent module\n");
        return 1;
    }
    return 0;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

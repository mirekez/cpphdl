#include <cpphdl.h>
#include <CliConfig.h>

#if CPPHDL_CLI_VALUE != CPPHDL_CLI_EXPECTED
#error Compiler-side include paths and defines must reach Clang
#endif

using namespace cpphdl;

class CommandLineInput : public Module
{
public:
    _PORT(u<8>) value_out = _ASSIGN(u<8>(CPPHDL_CLI_VALUE));

    void _assign() {}
    void _work(bool reset) {}
    void _strobe() {}
};

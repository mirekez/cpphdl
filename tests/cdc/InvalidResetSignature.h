#include <cpphdl.h>

using namespace cpphdl;

class InvalidResetSignature : public Module
{
public:
    reg<u<8>> fast_reg;

    void _work_fast_clk(bool reset)
    {
        fast_reg._next = reset ? 0 : fast_reg + 1;
    }

    void _strobe_fast_clk()
    {
        fast_reg.strobe();
    }

    void _work_slow_clk() {}
    void _strobe_slow_clk() {}
};

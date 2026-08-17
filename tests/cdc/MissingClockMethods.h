#include <cpphdl.h>

using namespace cpphdl;

class MissingClockMethods : public Module
{
public:
    reg<u<8>> value_reg;

    void _work_fast_clk(bool reset)
    {
        value_reg._next = reset ? 0 : value_reg + 1;
    }

    void _strobe_fast_clk()
    {
        value_reg.strobe();
    }
};

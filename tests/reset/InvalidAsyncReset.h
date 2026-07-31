#include <cpphdl.h>

using namespace cpphdl;

class InvalidAsyncReset : public Module
{
public:
    reg<u<8>> fast_reg;
    reg<u<8>> slow_reg;

    void _work_fast_clk(bool)
    {
        fast_reg._next = fast_reg + 1;
    }

    void _strobe_fast_clk()
    {
        fast_reg.strobe();
    }

    void _work_slow_clk(bool)
    {
        slow_reg._next = slow_reg + 1;
    }

    void _strobe_slow_clk()
    {
        slow_reg.strobe();
    }

#if defined(INVALID_ASYNC_RESET_SIGNATURE)
    void _reset_pos_fast_clk(bool)
    {
        fast_reg.clr();
    }
#elif defined(INVALID_ASYNC_RESET_NEG_EDGE)
    void _reset_neg_slow_clk()
    {
        slow_reg.clr();
    }
#elif defined(INVALID_ASYNC_RESET_OWNER)
    void _reset_pos_fast_clk()
    {
        slow_reg.clr();
    }
#endif
};

#include <cpphdl.h>

using namespace cpphdl;

class NamedLifecycleChild : public Module
{
public:
    reg<u<8>> rising;
    reg<u<8>> falling;
    memory<logic<8>, 1, 4> storage;
    void _work_fast_clk(bool) {}
    void _strobe_fast_clk() {}
    void _work_slow_clk(bool reset) { rising._next = reset ? 0 : 1; }
    void _work_neg_slow_clk(bool reset) { falling._next = reset ? 0 : 2; }
    void _strobe_slow_clk() { rising.strobe(); storage.apply(); }
    void _strobe_neg_slow_clk() { falling.strobe(); }
};

class NamedLifecycle : public Module
{
public:
    NamedLifecycleChild child;
    void _work_fast_clk(bool reset) { child._work_fast_clk(reset); }
    void _strobe_fast_clk() { child._strobe_fast_clk(); }
    void _work_slow_clk(bool reset)
    {
#ifndef MISSING_NAMED
        child._work_slow_clk(reset);
#endif
    }
    void _strobe_slow_clk()
    {
#ifndef MISSING_NAMED
        child._strobe_slow_clk();
#endif
    }
    void _work_neg_slow_clk(bool reset) { child._work_neg_slow_clk(reset); }
    void _strobe_neg_slow_clk() { child._strobe_neg_slow_clk(); }
};

#include <cpphdl.h>

using namespace cpphdl;

class EmptyLifecycleChild : public Module
{
public:
#ifdef MISSING_EFFECTFUL
    reg<u<8>> value;
    void _work(bool reset) { value._next = reset ? 0 : 1; }
    void _strobe() { value.strobe(); }
#else
    void _work(bool) {}
    void _strobe() {}
#endif
};

class EmptyLifecycle : public Module
{
public:
    EmptyLifecycleChild working;
    EmptyLifecycleChild committing;

    void _work(bool reset)
    {
#ifdef MISSING_EFFECTFUL
        committing._work(reset);
#endif
    }

    void _strobe()
    {
#ifdef MISSING_EFFECTFUL
        working._strobe();
#endif
    }
};

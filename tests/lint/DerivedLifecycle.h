#include <cpphdl.h>

using namespace cpphdl;

class DerivedLifecycleState : public Module
{
public:
    reg<u<8>> value;
};

class DerivedLifecycleWork : public DerivedLifecycleState
{
public:
    void _work(bool reset) { value._next = reset ? 0 : 1; }
};

class DerivedLifecycle : public DerivedLifecycleWork
{
public:
    void _strobe() { value.strobe(); }
};

#ifdef COMPOSED_BASE
class ComposedLifecycle : public Module
{
public:
    DerivedLifecycleState child;
};
#endif

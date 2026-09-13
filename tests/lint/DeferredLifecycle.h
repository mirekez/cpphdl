#include <cpphdl.h>

using namespace cpphdl;

template<size_t WIDTH>
class DeferredLifecycleChild : public Module
{
public:
    reg<u<WIDTH>> value;
    void _work(bool reset) { value._next = reset ? 0 : 1; }
    void _strobe() { value.strobe(); }
};

template<typename CHILD>
class DeferredLifecycleBase : public Module
{
public:
    CHILD child;
    void _work(bool reset) { child._work(reset); }
    void _strobe() { child._strobe(); }
};

class DeferredLifecycle : public DeferredLifecycleBase<DeferredLifecycleChild<8>>
{
};

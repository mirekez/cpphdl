#include <cpphdl.h>

using namespace cpphdl;

template<unsigned N>
class LifecycleBase : public Module
{
public:
    reg<u<8>> values[N];
    void _work(bool reset)
    {
        for (unsigned i = 0; i < N; ++i) {
            values[i]._next = reset ? 0 : 1;
        }
    }
    void _strobe()
    {
        for (unsigned i = 0; i < N; ++i) {
            values[i].strobe();
        }
    }
};

class LifecycleInherited : public LifecycleBase<2>
{
public:
    reg<u<8>> own;
    memory<logic<8>, 1, 4> storage[2];
    void _work(bool reset)
    {
        LifecycleBase<2>::_work(reset);
        own._next = reset ? 0 : 1;
    }
    void commit()
    {
        own.strobe();
        for (unsigned i = 0; i < 2; ++i) {
            storage[i].apply();
        }
    }
    void _strobe()
    {
#ifndef HIDDEN_BASE
        LifecycleBase<2>::_strobe();
#endif
        commit();
    }
};

class ValidLifecycle : public Module
{
public:
    LifecycleInherited children[2];
    void calculate(bool reset)
    {
        for (unsigned i = 0; i < 2; ++i) {
            children[i]._work(reset);
        }
    }
    void commit()
    {
        for (unsigned i = 0; i < 2; ++i) {
            children[i]._strobe();
        }
    }
    void _work(bool reset) { calculate(reset); }
    void _strobe() { commit(); }
};

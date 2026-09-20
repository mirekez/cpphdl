#include <cpphdl.h>

using namespace cpphdl;

template<unsigned Width>
class EmptyLifecycleBase : public Module
{
public:
    _PORT(logic<Width>) data_in;
    _PORT(logic<Width>) data_out = _ASSIGN(data_in());
    void _work(bool) {}
    void _strobe() {}
};

class EmptyLifecycleChild : public EmptyLifecycleBase<8>
{
public:
    void no_work(bool reset) { EmptyLifecycleBase<8>::_work(reset); }
    void no_strobe() { EmptyLifecycleBase<8>::_strobe(); }
    void _work(bool reset) { no_work(reset); }
    void _strobe() { no_strobe(); }
};

class WorkOnlyLifecycle : public Module
{
public:
    uint32_t count;
    void update() { count = 1; }
    void _work(bool) { update(); }
    void _strobe() {}
};

class StrobeOnlyLifecycle : public Module
{
public:
    uint32_t count;
    void update() { count = 1; }
    void _work(bool) {}
    void _strobe() { update(); }
};

class EmptyLifecycle : public Module
{
public:
    EmptyLifecycleBase<16> empty[2][3];
    EmptyLifecycleChild wrapper;
    WorkOnlyLifecycle working;
    StrobeOnlyLifecycle committing;
    void _work(bool reset)
    {
#ifndef MISSING_EFFECTFUL
        working._work(reset);
#endif
    }
    void _strobe()
    {
#ifndef MISSING_EFFECTFUL
        committing._strobe();
#endif
    }
};

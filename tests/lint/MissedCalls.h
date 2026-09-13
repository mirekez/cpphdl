#include <cpphdl.h>

using namespace cpphdl;

class LifecycleLeaf : public Module
{
public:
    reg<u<8>> value;
    void _work(bool reset) { value._next = reset ? 0 : 1; }
    void _strobe() { value.strobe(); }
};

class MissedCalls : public Module
{
public:
    reg<u<8>> value;
    memory<logic<8>, 1, 4> storage;
    LifecycleLeaf child;

#ifndef MISSING_WORK_METHOD
    void _work(bool reset)
    {
        value._next = reset ? 0 : 1;
        storage[0] = 1;
#if !defined(MISSING_CHILD_WORK) && !defined(WRONG_PHASE) && !defined(DEAD_HELPER)
        child._work(reset);
#endif
#ifdef WRONG_PHASE
        value.strobe();
        storage.apply();
        child._strobe();
#endif
    }
#endif

#ifndef MISSING_STROBE_METHOD
    void _strobe()
    {
#if !defined(MISSING_REGISTER) && !defined(WRONG_PHASE) && !defined(DEAD_HELPER)
        value.strobe();
#endif
#if !defined(MISSING_MEMORY) && !defined(WRONG_PHASE) && !defined(DEAD_HELPER)
        storage.apply();
#endif
#if !defined(MISSING_CHILD_STROBE) && !defined(WRONG_PHASE) && !defined(DEAD_HELPER)
        child._strobe();
#endif
#ifdef WRONG_PHASE
        child._work(false);
#endif
#ifdef DEAD_HELPER
        if (false) {
            _strobe_unused();
        }
#endif
    }
#endif

    // Merely defining this method must not satisfy any lifecycle requirement.
    void _strobe_unused()
    {
        value.strobe();
        storage.apply();
        child._work(false);
        child._strobe();
    }
};

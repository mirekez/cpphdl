#include <cpphdl.h>

using namespace cpphdl;

struct DeferredState
{
    uint32_t words[32];
};

template<typename State>
class DeferredLifecycleBase : public Module
{
public:
    reg<State> state;
    void _work(bool) { state._next = State{}; }
    void _strobe() { state.strobe(); }
};

// The native compiler need not instantiate either inherited method here.
// Linting must not instantiate the struct's implicit copy assignment after
// Clang has torn down its parser scopes.
class DeferredLifecycle : public DeferredLifecycleBase<DeferredState>
{
};

template<typename State>
class DeferredDelegatingBase : public DeferredLifecycleBase<State>
{
public:
    void _work(bool reset) { DeferredLifecycleBase<State>::_work(reset); }
    void _strobe() { DeferredLifecycleBase<State>::_strobe(); }
};

class DeferredDelegating : public DeferredDelegatingBase<DeferredState>
{
};

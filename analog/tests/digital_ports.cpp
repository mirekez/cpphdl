#include <cpphdl.h>
#include <type_traits>

long _system_clock = 0;
static_assert(std::is_same<_PORT(cpphdl::logic<8>), cpphdl::function_ref<cpphdl::logic<8>>>::value);

int main() {
    cpphdl::reg<cpphdl::logic<8>> state;
    state.set(cpphdl::logic<8>(7));
    _PORT(cpphdl::logic<8>) input;
    input = [&] { return &state; };
    if (static_cast<std::uint64_t>(input()) != 7)
        return 1;
    state._next = 19;
    if (static_cast<std::uint64_t>(input()) != 7)
        return 2;
    state.strobe();
    ++_system_clock;
    return static_cast<std::uint64_t>(input()) == 19 ? 0 : 3;
}

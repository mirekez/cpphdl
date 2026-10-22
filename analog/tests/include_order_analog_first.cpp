#include <analog.hpp>
#include <cpphdl.h>
#include <type_traits>

static_assert(std::is_same_v<_PORT(analog::electrical), analog::port<analog::electrical>>);
static_assert(std::is_same_v<_PORT(analog::capacitance), analog::port<analog::capacitance>>);
static_assert(std::is_same_v<_PORT(analog::bits<8>), analog::port<analog::bits<8>>>);
static_assert(std::is_same_v<_PORT(cpphdl::logic<8>), cpphdl::function_ref<cpphdl::logic<8>>>);
static_assert(!std::is_base_of_v<cpphdl::Module, analog::IC>);

bool analog_first_ports() {
    cpphdl::logic<8> value(23);
    _PORT(cpphdl::logic<8>) input;
    input = [&] { return &value; };
    return static_cast<std::uint64_t>(input()) == 23;
}

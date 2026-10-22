#include <cpphdl_analog.h>
using namespace cpphdl::analog;
using namespace analog::literals;
void invalid(const electrical& wire) {
    resistor bad(wire, 1_nF);
}

#include "cpphdl.h"
#include <map>
#include <vector>
#include <cstdlib>
#include <cmath>
#include <type_traits>

#ifndef _LIBCPP_VERSION
#error "Explicit libc++ selection was lost"
#endif
#ifdef __GLIBCXX__
#error "libstdc++ headers were mixed into libc++"
#endif
static_assert(std::is_same<std::vector<unsigned>::value_type, unsigned>::value, "vector headers");
static_assert(std::is_same<std::map<unsigned, unsigned>::mapped_type, unsigned>::value, "map headers");

class LibcxxHeaders : public cpphdl::Module {
public:
    _PORT(uint32_t) value_out;
    void _assign() { value_out = _ASSIGN(17u); }
};

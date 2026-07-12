#include "cpphdl.h"
#include <array>
#include <cstdio>

using namespace cpphdl;

// Packed-array field assignment previously repeated or narrowed scalar sources.
// SystemVerilog instead assigns the scalar to the complete packed destination value.
// Verify nonzero replication is rejected and zero assignment clears every element.
// Nested packed arrays also verify recursive overload lookup selects array assignment.
// sv_cast of std::array verifies array overloads are declared before its dependent call.
int main()
{
    array<4, logic<4>, true> value;

    sv_assign_field(value, 3);

    const auto packed = value.pack();
    if (uint64_t(packed) != 0x0003ull) {
        std::printf("sv_assign_field packed array scalar result 0x%llx\n",
                    (unsigned long long)uint64_t(packed));
        return 1;
    }

    sv_assign_field(value, 0);
    if (uint64_t(value.pack()) != 0ull) {
        std::printf("sv_assign_field packed array zero result 0x%llx\n",
                    (unsigned long long)uint64_t(value.pack()));
        return 2;
    }

    array<2, array<2, logic<4>, true>, true> nested;
    sv_assign_field(nested, 3);
    if (uint64_t(nested.pack()) != 0x0003ull) {
        std::printf("sv_assign_field nested packed array scalar result 0x%llx\n",
                    (unsigned long long)uint64_t(nested.pack()));
        return 3;
    }

    const auto standard = sv_cast<std::array<int, 2>>(3);
    if (standard[0] != 3 || standard[1] != 3) {
        std::printf("sv_cast std::array did not select aggregate assignment\n");
        return 4;
    }

    return 0;
}

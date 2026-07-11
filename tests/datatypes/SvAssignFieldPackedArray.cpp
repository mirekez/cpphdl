#include "cpphdl.h"
#include <cstdio>

using namespace cpphdl;

// Packed-array field assignment previously repeated or narrowed scalar sources.
// SystemVerilog instead assigns the scalar to the complete packed destination value.
// Verify nonzero replication is rejected and zero assignment clears every element.
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

    return 0;
}

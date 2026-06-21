// Shared test harness helpers only, not L2 cache behaviour requirements:
// 1. Section labels make per-requirement failures easy to locate.
// 2. Integer-like CppHDL values are compared without relying on std::format.
// 3. Tests return a conventional process status for CTest.
#pragma once

#include <cstdint>
#include <cstdio>

namespace l2test
{

struct Context
{
    bool ok = true;
};

inline void section(const char* name)
{
    std::printf("  %s\n", name);
}

template<typename T, typename U>
inline void expect_eq(Context& ctx, const char* name, T got, U expected)
{
    uint64_t got_u;
    uint64_t expected_u;

    got_u = (uint64_t)got;
    expected_u = (uint64_t)expected;
    if (got_u != expected_u) {
        std::printf("    %s: got 0x%llx, expected 0x%llx\n",
            name, (unsigned long long)got_u, (unsigned long long)expected_u);
        ctx.ok = false;
    }
}

inline void expect_true(Context& ctx, const char* name, bool value)
{
    if (!value) {
        std::printf("    %s: got false, expected true\n", name);
        ctx.ok = false;
    }
}

inline void expect_false(Context& ctx, const char* name, bool value)
{
    if (value) {
        std::printf("    %s: got true, expected false\n", name);
        ctx.ok = false;
    }
}

inline int finish(const char* test_name, const Context& ctx)
{
    std::printf("%s %s\n", test_name, ctx.ok ? "PASSED" : "FAILED");
    return ctx.ok ? 0 : 1;
}

}

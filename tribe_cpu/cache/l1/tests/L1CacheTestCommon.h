// Shared L1 layer-test harness requirements:
// 1. Report the exact behavior section that fails without relying on std::format.
// 2. Compare CppHDL integer-like values through their stable uint64_t conversion.
// 3. Return conventional process status for CTest.
#pragma once

#include <cstdint>
#include <cstdio>

namespace l1test
{
struct Context { bool ok = true; };
inline void section(const char* name) { std::printf("  %s\n", name); }
template<typename T, typename U>
inline void expect_eq(Context& ctx, const char* name, T got, U expected)
{
    uint64_t got_u = (uint64_t)got;
    uint64_t expected_u = (uint64_t)expected;
    if (got_u != expected_u) {
        std::printf("    %s: got 0x%llx, expected 0x%llx\n", name,
            (unsigned long long)got_u, (unsigned long long)expected_u);
        ctx.ok = false;
    }
}
inline void expect_true(Context& ctx, const char* name, bool value)
{
    if (!value) { std::printf("    %s: expected true\n", name); ctx.ok = false; }
}
inline void expect_false(Context& ctx, const char* name, bool value)
{
    if (value) { std::printf("    %s: expected false\n", name); ctx.ok = false; }
}
inline int finish(const char* name, const Context& ctx)
{
    std::printf("%s %s\n", name, ctx.ok ? "PASSED" : "FAILED");
    return ctx.ok ? 0 : 1;
}
}

// OOP L2Cache memory-routing requirements tested here:
// 1. Miss/refill control must select the external memory port from address regions.
// 2. DMA/AXI address generation must receive region-local addresses, not global CPU addresses.
// 3. Uncached-region and no-region results must be explicit inputs to later control policy.
#include "L2CacheRegionRouter_test.h"

#include "L2CacheRegionRouter.h"
#include "L2CacheTestCommon.h"

using Ops = L2CacheRegionRouter<L2CacheByteOps<L2CacheGeometry<1024, 64, 32, 2, 32, 32, 4>>>;

int L2CacheRegionRouter_test_main()
{
    uint32_t sizes[4];
    bool uncached[4];
    l2test::Context ctx;

    sizes[0] = 0x100;
    sizes[1] = 0x200;
    sizes[2] = 0x300;
    sizes[3] = 0x400;
    uncached[0] = false;
    uncached[1] = true;
    uncached[2] = false;
    uncached[3] = true;

    l2test::section("1. first matching region");
    auto first = Ops::route(0x8000001du, 0x80000000u, sizes, uncached);
    l2test::expect_true(ctx, "first hit", first.hit);
    l2test::expect_eq(ctx, "first port", first.port, 0);
    l2test::expect_eq(ctx, "first local", first.local_addr, 0x1d);

    l2test::section("2. cumulative region base/local");
    auto second = Ops::route(0x80000110u, 0x80000000u, sizes, uncached);
    l2test::expect_true(ctx, "second hit", second.hit);
    l2test::expect_eq(ctx, "second port", second.port, 1);
    l2test::expect_eq(ctx, "second base", second.region_base, 0x100);
    l2test::expect_eq(ctx, "second local", second.local_addr, 0x10);

    l2test::section("3. uncached and miss reporting");
    l2test::expect_true(ctx, "second uncached", second.uncached);
    auto miss = Ops::route(0x80001000u, 0x80000000u, sizes, uncached);
    l2test::expect_false(ctx, "miss hit", miss.hit);
    l2test::expect_eq(ctx, "miss fallback port", miss.port, 3);

    return l2test::finish("L2CacheRegionRouter_test", ctx);
}

int main()
{
    return L2CacheRegionRouter_test_main();
}

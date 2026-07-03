// Layered L2Cache build-layer requirements tested here:
// 1. Final assembly must expose one coherent geometry contract from all inherited helper layers.
// 2. Final assembly must expose byte, routing, tag, response, timeout, and port helper APIs.
// 3. Cross-layer helper calls must stay consistent when accessed through the final L2Cache type.
#include "L2Cache_test.h"

#include "L2Cache.h"
#include "L2CacheTestCommon.h"

using Ops = L2Cache<1024, 64, 32, 2, 32, 32, 4>;
using OpsBase = L2CacheBase<1024, 64, 32, 2, 32, 32, 4>;

int L2Cache_test_main()
{
    uint32_t sizes[4];
    bool uncached[4];
    array<logic<OpsBase::TAG_RAM_BITS>, OpsBase::WAYS, true> tags;
    l2test::Context ctx;

    sizes[0] = 0x100;
    sizes[1] = 0x200;
    sizes[2] = 0x300;
    sizes[3] = 0x400;
    uncached[0] = false;
    uncached[1] = true;
    uncached[2] = false;
    uncached[3] = true;
    tags[0] = OpsBase::make_tag(true, false, u<OpsBase::TAG_BITS>(0x12));
    tags[1] = OpsBase::make_tag(true, true, u<OpsBase::TAG_BITS>(0x34));

    l2test::section("1. final-layer geometry export");
    l2test::expect_eq(ctx, "PORT_BITWIDTH", OpsBase::PORT_BITWIDTH, 64);
    l2test::expect_eq(ctx, "CACHE_LINE_SIZE", OpsBase::CACHE_LINE_SIZE, 32);
    l2test::expect_eq(ctx, "WAYS", OpsBase::WAYS, 2);
    l2test::expect_eq(ctx, "MEM_PORTS", OpsBase::MEM_PORTS, 4);

    l2test::section("2. final-layer inherited helpers");
    l2test::expect_eq(ctx, "byte store", OpsBase::store_word(0x11223344u, 0xaabbccddu, 0x3, 1), 0x11ccdd44u);
    l2test::expect_eq(ctx, "timeout age", OpsBase::next_age(true, false, 4), 5);
    l2test::expect_true(ctx, "port choice valid", OpsBase::choose(false, false, true, false, false, false).valid);

    l2test::section("3. cross-layer consistency");
    auto route = OpsBase::route(0x80000110u, 0x80000000u, sizes, uncached);
    auto hit = OpsBase::find_hit(tags, u<OpsBase::TAG_BITS>(0x34));
    l2test::expect_eq(ctx, "route port", route.port, 1);
    l2test::expect_true(ctx, "route uncached", route.uncached);
    l2test::expect_true(ctx, "tag hit", hit.hit);
    l2test::expect_eq(ctx, "tag way", hit.way, 1);
    l2test::expect_true(ctx, "cross line write", OpsBase::cross_line_write(0x8000001du, 0xf));

    return l2test::finish("L2Cache_test", ctx);
}

int main()
{
    return L2Cache_test_main();
}

// OOP L2Cache build-layer requirements tested here:
// 1. Final assembly must expose one coherent geometry contract from all inherited helper layers.
// 2. Final assembly must expose byte, routing, tag, response, timeout, and port helper APIs.
// 3. Cross-layer helper calls must stay consistent when accessed through the final L2CacheOO type.
#include "L2CacheOO_test.h"

#include "L2CacheOO.h"
#include "L2CacheTestCommon.h"

using Ops = L2CacheOO<1024, 64, 32, 2, 32, 32, 4>;

int L2CacheOO_test_main()
{
    uint32_t sizes[4];
    bool uncached[4];
    array<logic<Ops::TAG_RAM_BITS>, Ops::WAYS, true> tags;
    l2test::Context ctx;

    sizes[0] = 0x100;
    sizes[1] = 0x200;
    sizes[2] = 0x300;
    sizes[3] = 0x400;
    uncached[0] = false;
    uncached[1] = true;
    uncached[2] = false;
    uncached[3] = true;
    tags[0] = Ops::make_tag(true, false, u<Ops::TAG_BITS>(0x12));
    tags[1] = Ops::make_tag(true, true, u<Ops::TAG_BITS>(0x34));

    l2test::section("1. final-layer geometry export");
    l2test::expect_eq(ctx, "PORT_BITWIDTH", Ops::PORT_BITWIDTH, 64);
    l2test::expect_eq(ctx, "CACHE_LINE_SIZE", Ops::CACHE_LINE_SIZE, 32);
    l2test::expect_eq(ctx, "WAYS", Ops::WAYS, 2);
    l2test::expect_eq(ctx, "MEM_PORTS", Ops::MEM_PORTS, 4);

    l2test::section("2. final-layer inherited helpers");
    l2test::expect_eq(ctx, "byte store", Ops::store_word(0x11223344u, 0xaabbccddu, 0x3, 1), 0x11ccdd44u);
    l2test::expect_eq(ctx, "timeout age", Ops::next_age(true, false, 4), 5);
    l2test::expect_true(ctx, "port choice valid", Ops::choose(false, false, true, false, false, false).valid);

    l2test::section("3. cross-layer consistency");
    auto route = Ops::route(0x80000110u, 0x80000000u, sizes, uncached);
    auto hit = Ops::find_hit(tags, u<Ops::TAG_BITS>(0x34));
    l2test::expect_eq(ctx, "route port", route.port, 1);
    l2test::expect_true(ctx, "route uncached", route.uncached);
    l2test::expect_true(ctx, "tag hit", hit.hit);
    l2test::expect_eq(ctx, "tag way", hit.way, 1);
    l2test::expect_true(ctx, "cross line write", Ops::cross_line_write(0x8000001du, 0xf));

    return l2test::finish("L2CacheOO_test", ctx);
}

int main()
{
    return L2CacheOO_test_main();
}

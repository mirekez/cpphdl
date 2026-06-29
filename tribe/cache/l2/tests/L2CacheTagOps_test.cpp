// OOP L2Cache tag-store requirements tested here:
// 1. Tag RAM entries must carry valid, dirty, and tag payload in one packed word.
// 2. Lookup control must find the matching valid way deterministically.
// 3. Invalid tags must never produce a hit, even when their payload matches.
#include "L2CacheTagOps_test.h"

#include "L2CacheTagOps.h"
#include "L2CacheTestCommon.h"

using Ops = L2CacheTagOps<L2CacheRegionRouter<L2CacheByteOps<L2CacheGeometry<1024, 64, 32, 2, 32, 32, 4>>>>;

int L2CacheTagOps_test_main()
{
    array<logic<Ops::TAG_RAM_BITS>, Ops::WAYS, true> tags;
    l2test::Context ctx;

    l2test::section("1. tag entry packing");
    auto entry = Ops::make_tag(true, true, u<Ops::TAG_BITS>(0x12345));
    l2test::expect_true(ctx, "valid bit", Ops::tag_valid(entry));
    l2test::expect_true(ctx, "dirty bit", Ops::tag_dirty(entry));
    l2test::expect_eq(ctx, "tag payload", Ops::tag_value(entry), 0x12345);

    l2test::section("2. valid matching way");
    tags[0] = Ops::make_tag(true, false, u<Ops::TAG_BITS>(0x12));
    tags[1] = Ops::make_tag(true, true, u<Ops::TAG_BITS>(0x34));
    auto hit = Ops::find_hit(tags, u<Ops::TAG_BITS>(0x34));
    l2test::expect_true(ctx, "hit", hit.hit);
    l2test::expect_eq(ctx, "hit way", hit.way, 1);

    l2test::section("3. invalid entries and miss");
    tags[0] = Ops::make_tag(false, false, u<Ops::TAG_BITS>(0x55));
    tags[1] = Ops::make_tag(true, false, u<Ops::TAG_BITS>(0x66));
    auto invalid_match = Ops::find_hit(tags, u<Ops::TAG_BITS>(0x55));
    auto miss = Ops::find_hit(tags, u<Ops::TAG_BITS>(0x77));
    l2test::expect_false(ctx, "invalid match ignored", invalid_match.hit);
    l2test::expect_false(ctx, "miss", miss.hit);

    return l2test::finish("L2CacheTagOps_test", ctx);
}

int main()
{
    return L2CacheTagOps_test_main();
}

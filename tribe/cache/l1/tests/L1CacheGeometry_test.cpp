// L1CacheGeometry requirements tested here:
// 1. Derive set, tag, line, word, and refill-beat geometry from one parameter set.
// 2. Classify cacheable I/D reads at line boundaries.
// 3. Detect direct reads crossing a backing-memory beat.
#include "L1CacheGeometry_test.h"
#include "L1CacheGeometry.h"
#include "L1CacheTestCommon.h"

using IGeom = L1CacheGeometry<1024, 32, 2, 0, 17, 64>;
using DGeom = L1CacheGeometry<1024, 32, 2, 1, 17, 64>;

int L1CacheGeometry_test_main()
{
    l1test::Context ctx;
    l1test::section("1. geometry and decode");
    l1test::expect_eq(ctx, "sets", IGeom::SETS, 16);
    l1test::expect_eq(ctx, "set", IGeom::set_index(0x13c), 9);
    l1test::expect_eq(ctx, "word", IGeom::word_index(0x13c), 7);
    l1test::expect_eq(ctx, "beat", IGeom::refill_beat(0x13c), 3);
    l1test::section("2. cacheability");
    l1test::expect_false(ctx, "I line crossing", IGeom::cacheable(0x13e, false));
    l1test::expect_false(ctx, "D line crossing", DGeom::cacheable(0x13d, false));
    l1test::expect_true(ctx, "aligned", DGeom::cacheable(0x138, false));
    l1test::section("3. direct crossing");
    l1test::expect_true(ctx, "cross beat", DGeom::direct_cross_beat(0x13d, false));
    l1test::expect_false(ctx, "disabled raw address", DGeom::direct_cross_beat(0x13d, true));
    return l1test::finish("L1CacheGeometry_test", ctx);
}
int main() { return L1CacheGeometry_test_main(); }

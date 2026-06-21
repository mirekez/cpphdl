// OOP L2Cache response-glue requirements tested here:
// 1. Read-hit/refill datapath must slice cache lines into the configured port beat width.
// 2. Cross-line unaligned reads must combine low-line and high-line data correctly.
// 3. The same helper must preserve normal aligned read data when no split is needed.
#include "L2CacheResponseGlue_test.h"

#include "L2CacheResponseGlue.h"
#include "L2CacheTestCommon.h"

using Ops = L2CacheResponseGlue<L2CacheTagOps<L2CacheRegionRouter<L2CacheByteOps<L2CacheGeometry<1024, 64, 32, 2, 32, 32, 4>>>>>;

static logic<64> make_beat(uint32_t word0, uint32_t word1)
{
    logic<64> ret;

    ret = 0;
    ret.bits(31, 0) = word0;
    ret.bits(63, 32) = word1;
    return ret;
}

int L2CacheResponseGlue_test_main()
{
    logic<256> line;
    l2test::Context ctx;

    line = 0;
    for (size_t i = 0; i < Ops::LINE_WORDS; ++i) {
        line.bits(i * 32 + 31, i * 32) = 0x1000u + (uint32_t)i;
    }

    l2test::section("1. beat slicing");
    l2test::expect_eq(ctx, "beat0", Ops::beat_from_line(line, 0), make_beat(0x1000, 0x1001));
    l2test::expect_eq(ctx, "beat3", Ops::beat_from_line(line, 3), make_beat(0x1006, 0x1007));

    l2test::section("2. cross-line unaligned read glue");
    auto cross = Ops::cross_line_read_data(make_beat(0x01020304u, 0xa1b2c3d4u), make_beat(0x55667788u, 0x99aabbccu), 0x8000001du);
    l2test::expect_eq(ctx, "cross data", (uint32_t)cross, 0x88a1b2c3u);

    l2test::section("3. same-beat unaligned read glue");
    auto same = Ops::cross_line_read_data(make_beat(0x01020304u, 0xa1b2c3d4u), make_beat(0x55667788u, 0x99aabbccu), 0x80000000u);
    l2test::expect_eq(ctx, "same data", (uint32_t)same, 0x01020304u);

    return l2test::finish("L2CacheResponseGlue_test", ctx);
}

int main()
{
    return L2CacheResponseGlue_test_main();
}

// Layered L2Cache geometry requirements tested here:
// 1. Every inherited layer must see the same derived line, set, tag, and memory-port constants.
// 2. Address decoding must be centralized so later stateful layers do not duplicate bit math.
// 3. Unaligned crossing predicates must identify when read/write control needs split handling.
#include "L2CacheGeometry_test.h"

#include "L2CacheGeometry.h"
#include "L2CacheTestCommon.h"

using Geom = L2CacheGeometry<1024, 64, 32, 2, 32, 32, 4>;

int L2CacheGeometry_test_main()
{
    l2test::Context ctx;

    l2test::section("1. static constants");
    l2test::expect_eq(ctx, "LINE_WORDS", Geom::LINE_WORDS, 8);
    l2test::expect_eq(ctx, "PORT_BYTES", Geom::PORT_BYTES, 8);
    l2test::expect_eq(ctx, "PORT_WORDS", Geom::PORT_WORDS, 2);
    l2test::expect_eq(ctx, "LINE_BEATS", Geom::LINE_BEATS, 4);
    l2test::expect_eq(ctx, "SETS", Geom::SETS, 16);
    l2test::expect_eq(ctx, "SET_BITS", Geom::SET_BITS, 4);
    l2test::expect_eq(ctx, "LINE_BITS", Geom::LINE_BITS, 5);
    l2test::expect_eq(ctx, "TAG_BITS", Geom::TAG_BITS, 23);
    l2test::expect_eq(ctx, "TAG_RAM_BITS", Geom::TAG_RAM_BITS, 32);
    l2test::expect_eq(ctx, "MEM_PORT_BITS", Geom::MEM_PORT_BITS, 2);

    l2test::section("2. address decode helpers");
    l2test::expect_eq(ctx, "line_base", Geom::line_base(0x8000013du), 0x80000120u);
    l2test::expect_eq(ctx, "beat_base", Geom::beat_base(0x8000013du), 0x80000138u);
    l2test::expect_eq(ctx, "byte_offset", Geom::byte_offset(0x8000013du), 1);
    l2test::expect_eq(ctx, "word_index", Geom::word_index(0x8000013du), 7);
    l2test::expect_eq(ctx, "beat_word_index", Geom::beat_word_index(0x8000013du), 1);
    l2test::expect_eq(ctx, "beat_index", Geom::beat_index(0x8000013du), 3);
    l2test::expect_eq(ctx, "set_index", Geom::set_index(0x8000013du), 9);
    l2test::expect_eq(ctx, "tag_value", Geom::tag_value(0x8000013du), 0x400000u);

    l2test::section("3. unaligned crossing detection");
    l2test::expect_true(ctx, "cross_beat_read", Geom::cross_beat_read(0x8000013du));
    l2test::expect_true(ctx, "cross_line_read", Geom::cross_line_read(0x8000013du));
    l2test::expect_true(ctx, "cross_line_write", Geom::cross_line_write(0x8000013du, 0xf));
    l2test::expect_false(ctx, "aligned does not cross", Geom::cross_line_write(0x8000013cu, 0xf));

    return l2test::finish("L2CacheGeometry_test", ctx);
}

int main()
{
    return L2CacheGeometry_test_main();
}

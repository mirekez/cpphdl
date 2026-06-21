// OOP L2Cache byte-lane requirements tested here:
// 1. Write-hit datapath must merge masked stores without changing unselected bytes.
// 2. Split-store control must get correct spill data and mask for the next word.
// 3. Read-response glue must build architectural words from adjacent stored words.
#include "L2CacheByteOps_test.h"

#include "L2CacheByteOps.h"
#include "L2CacheTestCommon.h"

using Ops = L2CacheByteOps<L2CacheGeometry<1024, 64, 32, 2, 32, 32, 4>>;

int L2CacheByteOps_test_main()
{
    l2test::Context ctx;

    l2test::section("1. masked store into addressed word");
    l2test::expect_eq(ctx, "aligned full store", Ops::store_word(0x11223344u, 0xaabbccddu, 0xf, 0), 0xaabbccddu);
    l2test::expect_eq(ctx, "offset partial store", Ops::store_word(0x11223344u, 0xaabbccddu, 0x3, 1), 0x11ccdd44u);

    l2test::section("2. unaligned spill into next word");
    l2test::expect_eq(ctx, "next word merge", Ops::store_next_word(0x11223344u, 0xaabbccddu, 0xf, 3), 0x11aabbccu);
    l2test::expect_eq(ctx, "spill data", Ops::cross_write_data(0xaabbccddu, 3), 0x00aabbccu);
    l2test::expect_eq(ctx, "spill mask", Ops::cross_write_mask(0xf, 3), 0x7);

    l2test::section("3. unaligned read joining");
    l2test::expect_eq(ctx, "aligned read", Ops::unaligned_read_word(0x11223344u, 0xaabbccddu, 0), 0x11223344u);
    l2test::expect_eq(ctx, "offset read", Ops::unaligned_read_word(0x11223344u, 0xaabbccddu, 1), 0xdd112233u);

    return l2test::finish("L2CacheByteOps_test", ctx);
}

int main()
{
    return L2CacheByteOps_test_main();
}

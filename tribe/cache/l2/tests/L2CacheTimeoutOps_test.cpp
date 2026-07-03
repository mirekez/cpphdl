// Layered L2Cache timeout requirements tested here:
// 1. Idle controller state must keep timeout age cleared.
// 2. Any forward progress must reset age and prevent false timeout reports.
// 3. Active stalled operations must age and expire at the configured watchdog limit.
#include "L2CacheTimeoutOps_test.h"

#include "L2CacheTimeoutOps.h"
#include "L2CacheTestCommon.h"

using Ops = L2CacheTimeoutOps<L2CacheResponseGlue<L2CacheTagOps<L2CacheRegionRouter<L2CacheByteOps<L2CacheGeometry<1024, 64, 32, 2, 32, 32, 4>>>>>>;

int L2CacheTimeoutOps_test_main()
{
    l2test::Context ctx;

    l2test::section("1. inactive clears age");
    l2test::expect_eq(ctx, "inactive next age", Ops::next_age(false, false, 7), 0);
    l2test::expect_false(ctx, "inactive not expired", Ops::expired(false, false, 7, 7));

    l2test::section("2. progress clears age and expiry");
    l2test::expect_eq(ctx, "progress next age", Ops::next_age(true, true, 7), 0);
    l2test::expect_false(ctx, "progress not expired", Ops::expired(true, true, 7, 7));

    l2test::section("3. stalled operation ages and expires");
    l2test::expect_eq(ctx, "stalled next age", Ops::next_age(true, false, 7), 8);
    l2test::expect_true(ctx, "expired at limit", Ops::expired(true, false, 7, 7));
    l2test::expect_false(ctx, "zero disables expiry", Ops::expired(true, false, 100, 0));

    return l2test::finish("L2CacheTimeoutOps_test", ctx);
}

int main()
{
    return L2CacheTimeoutOps_test_main();
}

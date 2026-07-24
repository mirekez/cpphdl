// L1CacheLookup requirements tested here:
// 1. Require valid, current global/set epochs, and matching tag for a hit.
// 2. Reject stale global epoch, stale set epoch, and invalid entries even when tag bits match.
// 3. Keep tag matching isolated from RAM/controller sequencing.
#include "L1CacheLookup_test.h"
#include "L1CacheLookup.h"
#include "L1CacheTestCommon.h"

class LookupProbe : public L1CacheLookup<1024, 32, 2, 0, 17, 64>
{
public:
    static bool matches(logic<18> entry, uint32_t tag, bool epoch, uint8_t set_epoch)
    { return tag_matches(entry, tag, epoch, set_epoch); }
};

int L1CacheLookup_test_main()
{
    l1test::Context ctx;
    logic<18> entry = 0x15;
    entry.bits(15, 8) = 7;
    entry[16] = true;
    entry[17] = true;
    l1test::section("1. complete tag match");
    l1test::expect_true(ctx, "valid current tag", LookupProbe::matches(entry, 0x15, true, 7));
    l1test::section("2. rejection properties");
    l1test::expect_false(ctx, "stale global epoch", LookupProbe::matches(entry, 0x15, false, 7));
    l1test::expect_false(ctx, "stale set epoch", LookupProbe::matches(entry, 0x15, true, 8));
    entry[17] = false;
    l1test::expect_false(ctx, "invalid", LookupProbe::matches(entry, 0x15, true, 7));
    return l1test::finish("L1CacheLookup_test", ctx);
}
int main() { return L1CacheLookup_test_main(); }

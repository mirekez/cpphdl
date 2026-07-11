// L1CacheLookup requirements tested here:
// 1. Require valid, current epoch, and matching tag for a hit.
// 2. Reject stale-epoch and invalid entries even when tag bits match.
// 3. Keep tag matching isolated from RAM/controller sequencing.
#include "L1CacheLookup_test.h"
#include "L1CacheLookup.h"
#include "L1CacheTestCommon.h"

class LookupProbe : public L1CacheLookup<1024, 32, 2, 0, 17, 64>
{
public:
    static bool matches(logic<10> entry, uint32_t tag, bool epoch)
    { return tag_matches(entry, tag, epoch); }
};

int L1CacheLookup_test_main()
{
    l1test::Context ctx;
    logic<10> entry = 0x15;
    entry[8] = true;
    entry[9] = true;
    l1test::section("1. complete tag match");
    l1test::expect_true(ctx, "valid current tag", LookupProbe::matches(entry, 0x15, true));
    l1test::section("2. rejection properties");
    l1test::expect_false(ctx, "stale epoch", LookupProbe::matches(entry, 0x15, false));
    entry[9] = false;
    l1test::expect_false(ctx, "invalid", LookupProbe::matches(entry, 0x15, true));
    return l1test::finish("L1CacheLookup_test", ctx);
}
int main() { return L1CacheLookup_test_main(); }

// L1CacheState requirements tested here:
// 1. Own public CPU and L1MemIf protocol endpoints at the lowest stateful layer.
// 2. Expose geometry-sized RAM/register storage to derived behavior layers.
// 3. Keep grouped request/refill/lookup/response records independently assignable.
#include "L1CacheState_test.h"
#include "L1CacheState.h"
#include "L1CacheTestCommon.h"

class StateProbe : public L1CacheState<1024, 32, 2, 0, 17, 64>
{
public:
    static size_t sets() { return SETS; }
    static size_t ways() { return 2; }
    void set_grouped_state()
    {
        req_reg._next = {};
        req_reg._next.addr = 0x456;
        req_reg._next.read = true;
        refill_reg._next = {};
        refill_reg._next.beat = 3;
        response_reg._next = {};
        response_reg._next.data = 0x12345678;
        response_reg._next.valid = true;
        req_reg.strobe();
        refill_reg.strobe();
        response_reg.strobe();
    }
    L1RequestState request() const { return req_reg; }
    L1RefillState refill() const { return refill_reg; }
    L1HeldResponse response() const { return response_reg; }
};

int L1CacheState_test_main()
{
    l1test::Context ctx;
    StateProbe probe;
    L1MemDriver driver{};
    L1LookupComb lookup{};
    L1CpuResponseComb response{};
    driver.addr = 0x1234;
    lookup.hit = true;
    response.data = 0x89abcdef;
    l1test::section("1. owned protocol endpoints");
    probe.debugen_in = false;
    l1test::expect_eq(ctx, "sets", StateProbe::sets(), 16);
    l1test::section("2. grouped state records");
    l1test::expect_eq(ctx, "driver address", driver.addr, 0x1234);
    l1test::expect_true(ctx, "lookup hit", lookup.hit);
    l1test::expect_eq(ctx, "response data", response.data, 0x89abcdef);
    l1test::section("3. grouped transaction registers");
    probe.set_grouped_state();
    l1test::expect_eq(ctx, "request address", probe.request().addr, 0x456);
    l1test::expect_true(ctx, "request read", probe.request().read);
    l1test::expect_eq(ctx, "refill beat", probe.refill().beat, 3);
    l1test::expect_true(ctx, "held response valid", probe.response().valid);
    return l1test::finish("L1CacheState_test", ctx);
}
int main() { return L1CacheState_test_main(); }

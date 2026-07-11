// L1CacheResponse requirements tested here:
// 1. Replay held address/data/valid fields in ST_DONE.
// 2. Report initialization and refill states as busy.
// 3. Classify performance state from the same grouped response snapshot.
#include "L1CacheResponse_test.h"
#include "L1CacheResponse.h"
#include "L1CacheTestCommon.h"

long _system_clock = 0;

class ResponseProbe : public L1CacheResponse<1024, 32, 2, 0, 17, 64>
{
    logic<64> memory_data = 0;
    bool input_read = false;
public:
    void _assign()
    {
        mem_out.read_data_out = _ASSIGN_REG(memory_data);
        read_in = _ASSIGN_REG(input_read);
    }
    void set(uint32_t state, bool valid, uint32_t addr, uint32_t data)
    {
        state_reg._next = state; response_reg._next.valid = valid;
        response_reg._next.addr = addr; response_reg._next.data = data;
        state_reg.strobe(); response_reg.strobe();
        ++_system_clock;
    }
    L1CpuResponseComb response() { return cpu_response_comb_func(); }
    L1CachePerf perf() { return perf_comb_func(); }
};

int L1CacheResponse_test_main()
{
    l1test::Context ctx;
    ResponseProbe probe;
    probe._assign();
    probe.set(L1_ST_DONE, true, 0x120, 0xdeadbeef);
    auto response = probe.response();
    l1test::section("1. held response");
    l1test::expect_true(ctx, "valid", response.valid);
    l1test::expect_eq(ctx, "address", response.addr, 0x120);
    l1test::expect_eq(ctx, "data", response.data, 0xdeadbeef);
    l1test::section("2. busy states");
    probe.set(L1_ST_INIT, false, 0, 0);
    l1test::expect_true(ctx, "init busy", probe.response().busy);
    l1test::expect_true(ctx, "init perf", probe.perf().init_wait);
    return l1test::finish("L1CacheResponse_test", ctx);
}
int main() { return L1CacheResponse_test_main(); }

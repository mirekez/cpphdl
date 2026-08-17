// Layered L2Cache request/response pipeline requirements tested here:
// 1. CacheRequest must retain the complete arbitrated request across a clock edge.
// 2. CacheResponse must not become visible before its response register strobes.
// 3. CPU response identity and data must advance atomically in one register stage.
// 4. AXI B and R responses must remain independent inside one endpoint response record.
#include "L2CachePipeline_test.h"

#include "L2CacheState.h"
#include "L2CacheTestCommon.h"

int L2CachePipeline_test_main()
{
    cpphdl::reg<CacheRequest> request_reg;
    cpphdl::reg<CacheResponse> response_reg;
    l2test::Context ctx;

    request_reg.clr();
    response_reg.clr();

    l2test::section("1. registered request capture");
    request_reg._next.addr = 0x12345678u;
    request_reg._next.read = true;
    request_reg._next.port = true;
    request_reg._next.cpu_index = 3;
    l2test::expect_eq(ctx, "request before edge", request_reg.addr, 0u);
    request_reg.strobe();
    l2test::expect_eq(ctx, "request address after edge", request_reg.addr, 0x12345678u);
    l2test::expect_true(ctx, "request read after edge", request_reg.read);
    l2test::expect_true(ctx, "request port after edge", request_reg.port);
    l2test::expect_eq(ctx, "request CPU index after edge", request_reg.cpu_index, 3u);

    l2test::section("2. one-clock response latency");
    response_reg._next.valid = true;
    response_reg._next.addr = request_reg.addr;
    response_reg._next.read = request_reg.read;
    response_reg._next.data_port = request_reg.port;
    response_reg._next.r.data = 0x89abcdefu;
    l2test::expect_false(ctx, "response before edge", response_reg.valid);
    response_reg.strobe();
    l2test::expect_true(ctx, "response after edge", response_reg.valid);

    l2test::section("3. atomic CPU response payload");
    l2test::expect_eq(ctx, "response address", response_reg.addr, 0x12345678u);
    l2test::expect_true(ctx, "response read", response_reg.read);
    l2test::expect_true(ctx, "response data port", response_reg.data_port);
    l2test::expect_eq(ctx, "response data", (uint32_t)response_reg.r.data, 0x89abcdefu);

    l2test::section("4. independent AXI response channels");
    response_reg._next.b.valid = true;
    response_reg._next.b.id = 3;
    response_reg._next.r.valid = true;
    response_reg._next.r.id = 5;
    response_reg._next.r.last = true;
    response_reg._next.r.data = 0x10203040u;
    response_reg.strobe();
    l2test::expect_true(ctx, "write response valid", response_reg.b.valid);
    l2test::expect_eq(ctx, "write response id", response_reg.b.id, 3u);
    l2test::expect_true(ctx, "read response valid", response_reg.r.valid);
    l2test::expect_eq(ctx, "read response id", response_reg.r.id, 5u);
    l2test::expect_true(ctx, "read response last", response_reg.r.last);
    l2test::expect_eq(ctx, "read response data", (uint32_t)response_reg.r.data, 0x10203040u);

    return l2test::finish("L2CachePipeline_test", ctx);
}

int main()
{
    return L2CachePipeline_test_main();
}

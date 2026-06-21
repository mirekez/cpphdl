// OOP L2Cache port-arbitration requirements tested here:
// 1. The controller must classify the selected requester and operation kind in one result object.
// 2. Instruction fetch must wait behind data/slave traffic and unresolved cache work.
// 3. Data access must wait behind slave traffic and unresolved cache work.
#include "L2CachePortOps_test.h"

#include "L2CachePortOps.h"
#include "L2CacheTestCommon.h"

using Ops = L2CachePortOps<L2CacheTimeoutOps<L2CacheResponseGlue<L2CacheTagOps<L2CacheRegionRouter<L2CacheByteOps<L2CacheGeometry<1024, 64, 32, 2, 32, 32, 4>>>>>>>;

int L2CachePortOps_test_main()
{
    l2test::Context ctx;

    l2test::section("1. request-kind arbitration");
    auto slave = Ops::choose(true, false, true, false, true, false);
    l2test::expect_true(ctx, "slave valid", slave.valid);
    l2test::expect_true(ctx, "slave source", slave.from_slave);
    l2test::expect_true(ctx, "slave read", slave.read);
    l2test::expect_false(ctx, "slave not data", slave.data_port);
    auto data_write = Ops::choose(false, false, false, true, true, false);
    l2test::expect_true(ctx, "data valid", data_write.valid);
    l2test::expect_true(ctx, "data port", data_write.data_port);
    l2test::expect_true(ctx, "data write", data_write.write);
    auto inst = Ops::choose(false, false, false, false, true, false);
    l2test::expect_true(ctx, "inst read", inst.read);
    l2test::expect_false(ctx, "inst not data", inst.data_port);

    l2test::section("2. instruction wait rules");
    l2test::expect_false(ctx, "completed instruction read", Ops::cpu_i_wait(true, false, false, true, true));
    l2test::expect_true(ctx, "data blocks instruction", Ops::cpu_i_wait(true, true, false, false, true));
    l2test::expect_true(ctx, "busy cache blocks instruction", Ops::cpu_i_wait(false, false, false, false, false));

    l2test::section("3. data wait rules");
    l2test::expect_false(ctx, "completed data request", Ops::cpu_d_wait(true, false, true, true));
    l2test::expect_true(ctx, "slave blocks data", Ops::cpu_d_wait(true, true, false, true));
    l2test::expect_true(ctx, "busy cache blocks data", Ops::cpu_d_wait(false, false, false, false));

    return l2test::finish("L2CachePortOps_test", ctx);
}

int main()
{
    return L2CachePortOps_test_main();
}

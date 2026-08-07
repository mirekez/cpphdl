// Layered L2Cache port-arbitration requirements tested here:
// 1. The controller must classify the selected requester and operation kind in one result object.
// 2. Instruction fetch must wait behind data/slave traffic and unresolved cache work.
// 3. Data access must wait behind slave traffic and unresolved cache work.
#pragma once

int L2CachePortOps_test_main();

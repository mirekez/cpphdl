// Layered L2Cache request/response pipeline requirements tested here:
// 1. CacheRequest must retain the complete arbitrated request across a clock edge.
// 2. CacheResponse must not become visible before its response register strobes.
// 3. CPU response identity and data must advance atomically in one register stage.
// 4. AXI B and R responses must remain independent inside one endpoint response record.
#pragma once

int L2CachePipeline_test_main();

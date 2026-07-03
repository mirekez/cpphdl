// Layered L2Cache memory-routing requirements tested here:
// 1. Miss/refill control must select the external memory port from address regions.
// 2. DMA/AXI address generation must receive region-local addresses, not global CPU addresses.
// 3. Uncached-region and no-region results must be explicit inputs to later control policy.
#pragma once

int L2CacheRegionRouter_test_main();

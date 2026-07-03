// Layered L2Cache response-glue requirements tested here:
// 1. Read-hit/refill datapath must slice cache lines into the configured port beat width.
// 2. Cross-line unaligned reads must combine low-line and high-line data correctly.
// 3. The same helper must preserve normal aligned read data when no split is needed.
#pragma once

int L2CacheResponseGlue_test_main();

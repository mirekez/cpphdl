// Layered L2Cache tag-store requirements tested here:
// 1. Tag RAM entries must carry valid, dirty, and tag payload in one packed word.
// 2. Lookup control must find the matching valid way deterministically.
// 3. Invalid tags must never produce a hit, even when their payload matches.
#pragma once

int L2CacheTagOps_test_main();

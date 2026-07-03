// Layered L2Cache geometry requirements tested here:
// 1. Every inherited layer must see the same derived line, set, tag, and memory-port constants.
// 2. Address decoding must be centralized so later stateful layers do not duplicate bit math.
// 3. Unaligned crossing predicates must identify when read/write control needs split handling.
#pragma once

int L2CacheGeometry_test_main();

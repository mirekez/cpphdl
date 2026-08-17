// Layered L2Cache build-layer requirements tested here:
// 1. Final assembly must expose one coherent geometry contract from all inherited helper layers.
// 2. Final assembly must expose byte, routing, tag, response, timeout, and port helper APIs.
// 3. Cross-layer helper calls must stay consistent when accessed through the final L2Cache type.
#pragma once

int L2Cache_test_main();

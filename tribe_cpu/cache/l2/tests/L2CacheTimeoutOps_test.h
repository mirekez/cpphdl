// Layered L2Cache timeout requirements tested here:
// 1. Idle controller state must keep timeout age cleared.
// 2. Any forward progress must reset age and prevent false timeout reports.
// 3. Active stalled operations must age and expire at the configured watchdog limit.
#pragma once

int L2CacheTimeoutOps_test_main();

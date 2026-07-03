// Layered L2Cache byte-lane requirements tested here:
// 1. Write-hit datapath must merge masked stores without changing unselected bytes.
// 2. Split-store control must get correct spill data and mask for the next word.
// 3. Read-response glue must build architectural words from adjacent stored words.
#pragma once

int L2CacheByteOps_test_main();

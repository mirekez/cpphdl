#pragma once

// Public composition point; each inherited layer owns one independently tested responsibility.
#include "L1CacheController.h"

/*
L1Cache behavior map

Request-facing actions that directly answer or retire a CPU request:

1. Accept an unstalled CPU read so tag/data lookup can begin, achieved by
   L1CacheLookup::input_request_comb_func() issuing RAM reads and the controller
   latching addr_in() and request attributes into req_* registers.
   1.1. L1CacheRequest::input_decode_comb_func() derives the live set and
        cacheability from one address snapshot.
   1.2. The controller accepts reads in L1_ST_IDLE, after a completed response,
        or while chaining a different address after a hit.
   1.3. even_ram[], odd_ram[], and tag_ram[] read the accepted set so their
        synchronous outputs belong to req_reg.addr in L1_ST_LOOKUP.

2. Answer a cached read hit without an L2 transaction, achieved by
   L1CacheLookup::lookup_comb_func() selecting one valid current-epoch way and
   assembling its split even/odd line banks.
   2.1. Valid, epoch, and tag are compared together so stale entries cannot
        select data.
   2.2. L1CacheRefill::assemble_line_word() joins both 16-bit banks and handles
        supported unaligned words.
   2.3. L1CacheResponse::cpu_response_comb_func() presents the live lookup data
        immediately when downstream is ready.
   2.4. A stalled hit is copied to response_reg address/data/valid fields before
        entering L1_ST_DONE.

3. Complete a held response after downstream removes backpressure, achieved by
   replaying last_* registers through L1CpuResponseComb until stall_in() clears.
   3.1. The response address, data, and valid flag come from the same grouped
        comb result and therefore cannot describe different requests.
   3.2. A following read can be accepted directly from L1_ST_DONE; otherwise
        the controller clears the pending request and returns to L1_ST_IDLE.

4. Answer a non-cacheable read through backing memory, achieved by
   L1CacheRequest::mem_driver_comb_func() issuing a direct request and
   L1CacheRefill::direct_data_comb_func() assembling the returned word.
   4.1. Explicit cache disable and reads unsupported by split-line storage take
        this path.
   4.2. D-cache unaligned accesses use a containing beat or direct cross-beat
        address according to L1RequestGeometryComb::direct_cross_beat.
   4.3. The accepted result is stored in last_* registers and completed through
        the same L1_ST_DONE protocol as a cached miss.

5. Retire a CPU store as write-through/no-write-allocate, achieved by forwarding
   write/data/mask together in L1MemDriver and invalidating the addressed tag set.
   5.1. mem_out receives all five L1MemDriver fields from one grouped comb.
   5.2. tag_ram[] writes zero for write_in(), preventing a stale subsequent hit.
   5.3. response_reg.valid is cleared so an older held read cannot survive the store.

Background and delayed activities:

1. Reset clears every tag set before requests are accepted, while runtime
   invalidate removes all visible entries in one cycle by changing tag epoch.
   1.1. Reset enters L1_ST_INIT and tag_ram[] writes zero at init_set_reg.
   1.2. The controller advances init_set_reg until SETS - 1, then enters
        L1_ST_IDLE.
   1.3. invalidate_in() flips tag_epoch_reg, clears pending response state, and
        returns immediately to L1_ST_IDLE.

2. Flush discards stale in-flight response state and redirects lookup, achieved
   by clearing last/refill valid registers and reloading req_* from live inputs.
   2.1. An active read enters L1_ST_LOOKUP for the redirected address.
   2.2. A flush without read_in() leaves no request and returns to L1_ST_IDLE.

3. A lookup miss starts line refill so a complete 32-byte line can be installed,
   achieved by clearing refill accumulators and entering L1_ST_REFILL.
   3.1. L1MemDriver emits each line-aligned address plus refill_reg.beat.
   3.2. mem_out.wait_out() prevents beat counters and line images from advancing
        until returned data is accepted.
   3.3. L1CacheRefill::refill_lines_comb_func() updates low and high 16-bit bank
        images from the same memory beat.

4. The final accepted refill beat installs line and tag atomically, achieved by
   writing the victim way and preparing a held CPU response in the same cycle.
   4.1. even_ram[] and odd_ram[] receive L1RefillLinesComb for victim_reg.
   4.2. tag_ram[] receives tag, current epoch, and valid from refill_tag_comb_func().
   4.3. The requested word is selected from the current beat, an earlier saved
        beat, or the completed line image and written to response_reg.data.
   4.4. victim_reg advances round-robin and the controller enters L1_ST_DONE.

5. Grouped outputs are recomputed continuously so every consumer observes one
   coherent state snapshot.
   5.1. L1CpuResponseComb drives CPU data/address/valid/busy together.
   5.2. L1CachePerf classifies hit and wait reasons from that response snapshot.
   5.3. L1MemDriver drives the complete L1MemIf request without mixing sources.
*/

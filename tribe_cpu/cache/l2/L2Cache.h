#pragma once

#include "cpphdl.h"
#include "Axi4.h"
#include "RAM.h"
#include "L2CachePortOps.h"

using namespace cpphdl;

template<size_t CACHE_SIZE_ = 16384, size_t PORT_BITWIDTH_ = 256, size_t CACHE_LINE_SIZE_ = 32,
    size_t WAYS_ = 4, size_t ADDR_BITS_ = 32, size_t MEM_ADDR_BITS_ = 32, size_t MEM_PORTS_ = 1>
// Compose pure helper layers in dependency order; L2Cache owns the stateful controller below.
using L2CacheBase = L2CachePortOps<
    L2CacheTimeoutOps<
    L2CacheResponseGlue<
    L2CacheTagOps<
    L2CacheRegionRouter<
    L2CacheByteOps<
    L2CacheGeometry<CACHE_SIZE_, PORT_BITWIDTH_, CACHE_LINE_SIZE_, WAYS_, ADDR_BITS_, MEM_ADDR_BITS_, MEM_PORTS_>>>>>>>;

/*
L2Cache behavior map

Request-facing actions that can directly answer an input request:

1. ST_IDLE is initiated by any pending CPU or external AXI request, with the
   goal of selecting one coherent L2 operation, achieved by
   active_request_comb_func() arbitration and grouped request-register capture.
   1.1. Prioritize external coherent AXI slave writes so DMA/peer masters see
        coherent stores before CPU reads; detect them with
        active_request_comb_func().request.write from either a staged
        slave_aw_reg[i].valid plus W or simultaneous AW/W on axi_in[].
   1.2. Select external coherent AXI slave reads next so peer masters can
        observe cached data; detect them with active_request_comb_func().request.read
        when AR is valid and no R response is already pending.
   1.3. Select CPU D-cache requests after external traffic so load/store data
        accesses share the same coherent path; identify them with
        active_request_comb_func().request.port.
   1.4. Select CPU I-cache requests last so instruction fetch uses otherwise
        idle L2 bandwidth; use i.read_in/i.write_in only when no external or
        D-cache request is active.
   1.5. Build the active request fields so the rest of the state machine does
        not depend on the original port; active_request_comb_func().request
        produces address, read/write intent, CPU write data, AXI write beat,
        byte strobes, word mask, source ownership, and AXI response identity.
   1.6. Latch the selected request so ST_LOOKUP works from stable inputs;
        capture req_reg.addr, req_reg.write_data, req_reg.write_beat,
        req_reg.write_mask, req_reg.write_strobe, req_reg.write_word_mask,
        req_reg.read, req_reg.write, req_reg.port, req_reg.from_slave,
        req_reg.slave_index, and req_reg.slave_id.
   1.7. Present the selected set to tag_ram[] and data_ram[] so lookup data is
        ready next cycle; drive the RAM addresses from
        active_request_comb_func().set.
   1.8. Latch an external AW independently so split AXI AW/W writes can later
        be treated as one cache operation; store address and ID in
        slave_aw_reg[i].valid, slave_aw_reg[i].addr, and slave_aw_reg[i].id.

2. ST_LOOKUP is initiated after request capture, with the goal of rejecting
   addresses outside memory_base_in()/memory_size_in(), achieved by
   request_geometry_comb_func().addr_in_memory and an immediate zero/OK response.
   2.1. Reject unmapped addresses so no cache line is created for invalid
        space; request_geometry_comb_func().addr_in_memory compares req_reg.addr with
        memory_base_in()/memory_size_in().
   2.2. Complete an unmapped external read so the AXI slave port cannot hang;
        write response_reg[i].r.valid/id and zero response_reg[i].r.data.
   2.3. Complete an unmapped external write so the AXI slave port cannot hang;
        write response_reg[i].b.valid and response_reg[i].b.id with the
        original request ID.
   2.4. Complete an unmapped CPU read with zero so the CPU can continue; write
        the CPU CacheResponse slot and let read_data_comb_func() return it.
   2.5. Complete an unmapped CPU write as a no-op so the CPU can continue;
        register a CPU CacheResponse without touching tag_ram[], data_ram[], or axi_out[].

3. A cached read hit is initiated by a registered read request in ST_LOOKUP,
   with the goal of returning the requested beat without external memory,
   achieved by tag_ram[] hit selection and data_ram[] beat extraction.
   3.1. Find the matching way so cached data can be used immediately;
        hit_lookup_comb_func() compares valid tag_ram[] tags with
        request_geometry_comb_func().tag and returns one matching way.
   3.2. Extract the requested beat so the responder sees PORT_BITWIDTH data;
        hit_lookup_comb_func().beat reads data_ram[] at
        request_geometry_comb_func().set/beat using that same way.
   3.3. Return an external AXI read hit directly so no memory refill is issued;
        write response_reg[i].r from hit_lookup_comb_func().beat and the request ID.
   3.4. Return an aligned CPU/L1 read hit through the response register so the CPU wait
        protocol stays uniform; write the request CPU's response slot data with
        hit_lookup_comb_func().beat.
   3.5. Preserve the low word of an intra-line cross-beat CPU read so the
        delayed half can be assembled later; write the CPU response data[31:0] from
        hit_lookup_comb_func().read_word.
   3.6. Leave cache contents unchanged on read hits because no ownership state
        changes; do not write tag_ram[], data_ram[], or axi_out[].

4. A cached write hit is initiated by a registered write request in ST_LOOKUP,
   with the goal of updating the cached line and acknowledging the writer,
   achieved by data_ram[] masked writes and tag_ram[] dirty-tag rewrite.
   4.1. Apply an external AXI write hit so peer-master stores become visible in
        the cache; use req_reg.write_beat and req_reg.write_word_mask to write
        the selected full-beat lanes into data_ram[].
   4.2. Apply a CPU/L1 write hit so the addressed bytes change in place; use
        hit_write_pair_comb_func().word to merge req_reg.write_data with
        hit_lookup_comb_func().aligned_word under req_reg.write_mask.
   4.3. Apply the next-word part of an intra-line CPU write so unaligned stores
        are not truncated; use hit_write_pair_comb_func().next_word when the
        write crosses a PORT_BITWIDTH beat inside the same line.
   4.4. Mark the written line dirty so later replacement writes it back; write
        tag_ram[] with tag_write_data_comb_func().
   4.5. Acknowledge the writer so the input port can retire the request;
        external writes set response_reg[i].b.valid/id and CPU writes set the
        registered CPU CacheResponse valid bit.
   4.6. Avoid external memory traffic on write hits because the cache owns the
        updated line; defer axi_out[] writeback until eviction.

5. ST_CROSS_WRITE_LOOKUP is initiated by a CPU write whose bytes spill into
   the next cache line, with the goal of completing the second-line update,
   achieved by remapped request registers and a normal second-line hit lookup.
   5.1. Retarget the request to the next line so the spilled bytes address the
        correct cache entry; rewrite req_reg.addr, req_reg.write_data, and
        req_reg.write_mask with request_geometry_comb_func().cross_write_data
        and request_geometry_comb_func().cross_write_mask.
   5.2. Repeat the hit check on the second line so normal tag matching rules
        still apply; use request_geometry_comb_func().set/tag,
        and hit_lookup_comb_func().hit/way.
   5.3. Write the second-line bytes on hit so the whole original CPU store is
        represented; update data_ram[], mark tag_ram[] dirty, and release the
        CPU port through its registered CacheResponse without axi_out[].

6. A CacheResponse is registered after a CPU request prepares its final result,
   with the goal of isolating L1 timing from cache lookup and memory return,
   achieved by driving read data and wait only from the CPU response slot.
   6.1. Drive the prepared CPU read data so the selected requester can sample
        it; read_data_comb_func() returns the indexed CPU response data
        while the response identity selects the completed port.
   6.2. Release only the completed CPU port so arbitration remains precise;
        cpu_wait_comb_func().instruction/data deassert wait according to
        the registered address, operation, and data-port flag.
   6.3. Clear the CPU valid bit after one clock while permitting an unrelated
        AXI request to enter the request pipeline in parallel.

Background and delayed activities:

1. Reset initiates cache invalidation, with the goal of preventing stale line
   hits, achieved by ST_INIT walking init_set_reg and clearing every tag_ram[]
   entry.
   1.1. Enter ST_INIT on reset so every tag can be cleared before normal use;
        clear state_reg-facing request, fill, eviction, slave response, and
        pending-AW registers.
   1.2. Clear one cache set per ST_INIT cycle so all ways become invalid;
        drive tag_ram[].addr_in from init_set_reg, assert tag_ram[].wr_in, and
        write zero through tag_write_data_comb_func().
   1.3. Leave data_ram[] untouched because invalid tags are sufficient to block
        stale hits; stale data is ignored until a refill writes a valid tag.

2. Every cycle initiates external AXI response retirement and AW staging, with
   the goal of keeping slave response slots reusable, achieved by bready/rready
   cleanup and slave_aw_reg address latching.
   2.1. Retire completed B responses so another external write can use the
        slot; clear response_reg[i].b.valid when axi_in[i].bready_in() is true.
   2.2. Retire completed R responses so another external read can use the
        slot; clear response_reg[i].r.valid when axi_in[i].rready_in() is true.
   2.3. Stage accepted AW handshakes so split AXI writes keep their address;
        latch slave_aw_reg[i].valid, slave_aw_reg[i].addr, and
        slave_aw_reg[i].id in ST_IDLE.

3. A miss in ST_LOOKUP or ST_CROSS_WRITE_LOOKUP initiates replacement setup,
   with the goal of preparing either writeback or refill, achieved by capturing
   victim, fill, eviction tag, and eviction line registers.
   3.1. Capture the replacement candidate so later eviction/refill states use a
        stable way; copy victim_reg into fill_way_reg and reset fill_beat_reg
        and evict_beat_reg.
   3.2. Snapshot dirty victim metadata so writeback can proceed after lookup
        RAM outputs change; capture evict_candidate_comb_func().tag into
        evict_tag_reg and evict_candidate_comb_func().line into evict_line_reg.
   3.3. Choose refill or writeback so clean misses avoid unnecessary writes;
        use evict_candidate_comb_func().valid/dirty to enter either ST_AXI_AR or
        ST_EVICT_AW.

4. A dirty valid victim initiates external writeback, with the goal of
   preserving modified data before replacement, achieved by sending every
   evict_line_reg beat through axi_out[] AW/W/B handshakes.
   4.1. Reconstruct and route the evicted beat address so memory receives the
        original line location; axi_route_comb_func().aw_* combines
        evict_tag_reg, request_geometry_comb_func().set, evict_beat_reg, region selection,
        and local-address conversion.
   4.2. Build one logical AXI write driver so the selected axi_out[] port sees a
        complete grouped transaction; axi_out_driver_comb_func().aw/w/b carries
        valid, address, data, strobe, and response-ready fields.
   4.3. Send the write address first so the AXI write transaction can begin;
        ST_EVICT_AW waits for selected axi_out[].awready_out().
   4.4. Send the evicted data beat so dirty bytes reach memory; ST_EVICT_W
        drives axi_out_driver_comb_func().w from evict_line_reg and all-one
        strobes.
   4.5. Wait for the write response so the beat is known accepted; ST_EVICT_B
        waits for selected axi_out[].bvalid_out(), then increments
        evict_beat_reg or starts refill at ST_AXI_AR after the final beat.
   4.6. Leave tag/data replacement to refill so eviction only preserves old
        dirty data; do not update tag_ram[] or data_ram[] here.

5. A clean/evicted miss initiates external refill, with the goal of installing
   the requested line and eventually answering the stalled request, achieved by
   ST_AXI_AR/ST_AXI_R beat reads into data_ram[] and final tag_ram[] install.
   5.1. Issue the next refill read so one PORT_BITWIDTH beat can be fetched;
        ST_AXI_AR drives axi_out_driver_comb_func().ar using the read side of
        axi_route_comb_func().
   5.2. Accept the refill data beat so it can be installed; ST_AXI_R waits for
        axi_out_selected_resp_comb_func().r.valid while
        axi_out_driver_comb_func().r.ready drives the selected axi_out[].rready_in().
   5.3. Write each accepted refill beat into the victim way so the line becomes
        cached; data_ram[] writes fill_way_reg/request_geometry_comb_func().set/
        fill_beat_reg with axi_out_selected_resp_comb_func().r.data.
   5.4. Merge write-miss data into the refill beat so the original store is not
        lost; use fill_write_word_comb() for CPU writes or
        req_reg.write_beat/req_reg.write_strobe for external writes.
   5.5. Preserve early requested read data so the response can be emitted after
        the full line is valid; retain the selected beat in the unified
        response stage when fill_beat_reg matches the requested beat.
   5.6. Install the final tag so future lookups can hit; write
        tag_ram[fill_way_reg] with tag_write_data_comb_func() and advance
        victim_reg round-robin.
   5.7. Complete the stalled requester after the full line is installed so
        coherent state is visible first; set response_reg[i].r/b for external
        requests or register the CPU response slot.

6. An unaligned I-side read crossing a beat or line initiates a two-beat read
   sequence, with the goal of assembling one 32-bit instruction word, achieved
   by cross_low_reg/cross_high_reg and cross_read_data_comb_func().
   6.1. Detect the crossing instruction fetch so normal single-line lookup is
        bypassed; active_request_comb_func().cross_line_read enters ST_CROSS_AR0.
   6.2. Fetch the first beat so the low bytes are available; ST_CROSS_AR0 and
        ST_CROSS_R0 read through axi_out[] and latch cross_low_reg.
   6.3. Fetch the following beat so the high bytes are available; ST_CROSS_AR1
        and ST_CROSS_R1 read through axi_out[] and latch cross_high_reg.
   6.4. Assemble the 32-bit result so the CPU receives one instruction word;
        ST_CROSS_DONE uses cross_read_data_comb_func(), then registers the CPU
        CacheResponse data and returns to request arbitration.
   6.5. Avoid cache allocation on this bypass path so no partial-line ownership
        is created; do not allocate, dirty, or evict cache lines.

7. A CPU write whose mask spills past the last line word initiates a second
   write lookup, with the goal of updating the next line, achieved by
   request_geometry_comb_func().cross_write_data/cross_write_mask remapping.
   7.1. Detect the spill after first-line handling so the original store is not
        considered complete too early; use
        request_geometry_comb_func().cross_line_write.
   7.2. Remap the spilled bytes to the next line so normal write logic can be
        reused; rewrite request registers with
        request_geometry_comb_func().cross_write_data/cross_write_mask.
   7.3. Process the second line through normal lookup rules so hit/miss/evict
        behavior stays identical; ST_CROSS_WRITE_LOOKUP writes on hit, fills on
        clean miss, or evicts then fills on dirty miss.

8. A request to a mem_region_uncached_in[] region initiates an uncached access,
   with the goal of reaching devices without allocating cache lines, achieved
   by ST_IO_AR/ST_IO_R or ST_IO_AW/ST_IO_W/ST_IO_B on axi_out[].
   8.1. Identify device-region requests so MMIO bypasses cache state;
        req_uncached_region_comb_func() checks mem_region_uncached_in[] for the
        selected memory region.
   8.2. Issue uncached reads directly to the selected device so the returned
        value is not cached; ST_IO_AR/ST_IO_R route req_reg.addr through
        axi_route_comb_func().ar_*.
   8.3. Return uncached read data to the original requester so the bypass path
        completes like a normal response; update the CPU CacheResponse data or
        response_reg[i].r for external slave reads.
   8.4. Issue uncached writes directly to the selected device so stores take
        effect outside cache state; ST_IO_AW/ST_IO_W/ST_IO_B drive
        io_write_payload_comb_func().data/strobe.
   8.5. Preserve cache state on uncached accesses because devices are not cache
        lines; do not read, allocate, dirty, or evict tag_ram[] or data_ram[].

9. Any delayed external access initiates AXI master port routing, with the goal
   of reaching the correct memory/device region, achieved by cumulative
   mem_region_size_in[] selection and local address conversion.
   9.1. Select the target axi_out[] region so memory and devices see only their
        own traffic; axi_route_comb_func() compares the read/write access
        offsets against cumulative mem_region_size_in[] ranges.
   9.2. Convert the physical access to a region-local address so downstream
        devices can use smaller address maps; axi_route_comb_func().ar_local_addr
        and axi_route_comb_func().aw_local_addr subtract the selected region base
        and mask to MEM_ADDR_BITS.
   9.3. Drive only the selected axi_out[] port so unrelated regions stay idle;
        assert arvalid, awvalid, wvalid, bready, or rready only for the chosen
        delayed transaction port.
*/

#include "L2CacheController.h"

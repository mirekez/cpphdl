# Tribe 4-core timing analysis

## Target and method

- Vivado 2026.1
- `xc7k325tffg676-3` (Vivado part name for XC7K325T-3FFG676E)
- Four Tribe cores with private I/D L1 caches and one shared 64 KiB, four-way L2
- `clk`: 312 MHz, 3.205 ns
- `l2_clock`: 156 MHz, 6.410 ns
- 256-bit L2/AXI datapath

The complete cluster is synthesized out of context. Its simulation-facing AXI
interface has thousands of ports and is not a useful package-pin top level. Pin
and clock-buffer placement belongs in the SmartNIC wrapper. In that wrapper,
generate both clocks with one MMCM/PLL and describe `l2_clk` with
`create_generated_clock -divide_by 2`.

## Results after the current timing-fix pass

| Test | WNS | TNS | Important result |
| --- | ---: | ---: | --- |
| Execute, post-route at 312 MHz | -0.078 ns | -0.236 ns | 3.154 ns datapath; 0 DSPs |
| 2 KiB, two-way L1, post-route at 312 MHz | -2.363 ns | -1,006.970 ns | 1,758 LUTs, 668 FFs, 8 RAMB36, 2 RAMB18 |
| Complete CPU clock domain, raw placed at 312 MHz | -3.598 ns | not used for acceptance | 6.348 ns memory-retirement-to-CSR path |
| Complete L2 clock domain, raw placed at 156 MHz | -0.018 ns | not used for acceptance | Meets the allowed -0.2 ns margin |
| CPU to L2 CDC | +5.501 ns | 0 | All constrained endpoints meet |
| L2 to CPU CDC | +5.929 ns | 0 | All constrained endpoints meet; held payload bound is 6.410 ns |

The complete latest design uses 64,018 LUTs, 41,711 FFs, 64 RAMB36s,
52 RAMB18s, and no DSP48s. Before these changes it used about 114,521 LUTs,
149,415 FFs, 36 RAMB18s, and 40 DSP48s. L1 arrays now infer as block RAM.

Vivado `check_timing -verbose` reports **0 combinational loops**. The original
design had 473 timing-graph loops, including four especially visible
CSR/redirect loops. Consequently the remaining timing numbers are finite and
actionable rather than loop-broken STA artifacts.

## Fixes implemented

1. Added a conventional synchronous `RAMReplacement.sv` and a CppHDL
   replacement annotation. L1 data/tag arrays now infer as RAMB primitives
   instead of very large FF/read-mux structures.

2. Changed L1 lookup scheduling so the next independent RAM address is issued
   speculatively. A stalled hit remains in LOOKUP, and refill bookkeeping is
   initialized independently of the tag hit/miss result. A fresh routed run is
   -2.363 ns while retaining one-cycle RAM reads; an earlier -0.688 ns result
   was not reproduced and must not be used for sign-off.

3. Replaced combinational RV32M divide/remainder with a restoring iterative
   divider. Final signed correction has its own cycle. Replaced the wide
   multiplier/DSP chain with an iterative multiply unit. Multiply takes 33
   held cycles and nonzero divide takes 34; divide-by-zero completes directly.

4. Branch decisions now compare operands directly instead of traversing the
   generic ALU result mux. Execute improved from -40.443 ns and 331 levels to
   -0.078 ns and a 3.154 ns routed path.

5. Broke the trap/interrupt/MMU cycles. Current-instruction redirect uses only
   registered execute state; page-fault vector selection uses registered CSR
   state; interrupt acceptance waits only for an older non-restartable memory
   owner. Register-file write-first bypass is no longer gated by the global
   front-end wait. Loop count progressed from 473 to 180 to 0.

6. Added destination-domain mailbox payload registers. CPU requests are copied
   into L2-clock registers only after the request toggle reaches synchronizer
   stage 2. L2 response data is copied into CPU-clock registers as its response
   toggle reaches stage 2. This removed the former -15.848 ns L2-to-CPU path
   through CPU/MMU logic; both crossing directions now meet.

7. Disabled the generic register-file write-first bypass for Tribe. Tribe's
   explicit forwarding remains in place, while memory-response readiness can
   no longer feed backward through every asynchronous register-file read.
   SBI's implicit a0/a1/a6/a7 dependencies now use a one-cycle interlock rather
   than a live writeback-data bypass.

8. Registered accepted interrupts and their cause/delegation metadata. The
   first cycle verifies that the older memory-stage owner can retire and holds
   the pipeline; CSR/trap redirection consumes the registered event on the next
   cycle. CPU WNS improved from -21.867 ns immediately before this cut to
   -14.501 ns after it.

9. Added explicit registered IMMU and DMMU result stages. L1 caches consume
   only registered physical addresses and faults. Added a CPU-local registered
   instruction response; decode no longer consumes a live I-cache BRAM output.
   Both I-cache and D-cache are allowed to drain while the pipeline is held,
   avoiding cache-hit/branch-stall deadlocks.

10. Disabled combinational branch-predictor lookup at 312 MHz. Predictor
    training remains, but fetch proceeds sequentially and taken branches
    redirect from registered execute state. This is a throughput tradeoff, not
    a timing exception.

11. Registered D-cache load data and then the fully assembled/store-forwarded
    raw load word. Load formatting and forwarding therefore no longer extend
    directly from L1 tag/data lookup into pipeline or register-file writes.

12. L2 lookup, refill data, uncached AXI response data, and request novelty are
    registered. Disabling clock-enable extraction on the wide L2 response bank
    moved the final raw-placed L2 result from -0.262 ns to -0.018 ns.

13. Added registered page-table-walk ownership for the shared CPU data-side L2
    port. D-cache remains the default owner; DMMU or IMMU acquires the port only
    while no D-cache L2 request exists and retains it until `mem_read_out`
    drops. This removed the former -4.242 ns D-cache-hit-to-MMU-state path and
    improved complete-CPU raw placed WNS to -3.598 ns.

## Remaining problematic paths and recommended next fixes

### 1. CPU retirement/CSR commit boundary

The current worst path is:

`ExecuteMem memory address -> data-access/wait classification -> interrupt_retire_wait -> csr_state.valid -> CSR scounteren CE`

It is 6.348 ns, 19 logic levels, and has -3.598 ns slack at 3.205 ns. Similar
paths end at many CSR data and CE pins. Do not apply a multicycle exception:
the current CSR state can commit on the next active pipeline edge.

Add an explicit registered retirement record between memory completion and
CSR. Capture `{valid, state, result/fault}` only after the older memory owner is
safe, hold the pipeline for that capture cycle, then let CSR and architectural
writeback consume the record. This prevents address compares, L1/L2 readiness,
and interrupt qualification from driving every CSR enable directly.

### 2. CPU retirement and held-pipeline control

Other paths remain near the same range. The second path is an L1 D-cache BRAM
output through hit/word selection into `ExecuteMem::mem_data_reg` at -3.596 ns.
This is the atomic/load observation path, separate from the now-registered
normal writeback load result.

Register the D-cache response consumed by ExecuteMem, including address/data
and validity, before AMO/reservation processing. Keep this register distinct
from WritebackMem's formatted load-result stage so normal loads and AMOs retain
their respective identity checks.

### 3. L2 residual margin

L2 now has -0.018 ns raw placed slack, within the accepted -0.2 ns margin. Its
worst path is request-address arithmetic and state decoding into the L2 state
register CE, not the former wide response CE. Preserve the registered lookup,
AXI-response capture, refill-data capture, and `extract_enable = "no"`
response-bank implementation. Recheck after integration because only 182 ps of
the agreed margin remains.

### 4. Remaining L1 hit/miss control

The live/chained L1 worst path is tag RAM through hit logic to the next data-RAM
address (5.032 ns datapath). An experiment that registered the complete hit
response removed chaining but still placed tag compare plus wide way/data
selection before the new register; WNS improved only from -2.363 to -2.217 ns
while adding a hit cycle. The proper cut needs two explicit phases: register
`hit` and selected way, hold the matching data-array output, then select/register
the word separately. For the shallow tag arrays, compare forced block RAM
against duplicated distributed RAM; keep the wide data arrays in block RAM.

### 5. Execute final margin

Execute misses by only 78 ps post-route and is route dominated. The remaining
path is the ordinary ALU-operation/result mux, not multiply or divide. A small
result-select register, hierarchy-local placement, or retiming should close it.
Do this after the much larger retirement path is split.

### 6. Register file implementation

Each 32x32 register file remains a 1,024-FF asynchronous-read array with bypass
muxes. A duplicated 1R1W LUTRAM implementation (two copies for two read ports)
would reduce FF count and decode routing. Preserve explicit x0 behavior and the
core's explicit forwarding/interlock behavior.

## CDC constraints

The CDCs use held bundled data plus toggle synchronizers. The constraints:

- create 3.205 ns and 6.410 ns OOC clocks;
- false-path only synchronizer stage-1 D pins;
- keep stage 2 normally timed;
- bound CPU-to-L2 data to two L2 periods, 12.820 ns;
- bound L2-to-CPU data to two CPU periods, 6.410 ns;
- do not use blanket asynchronous clock groups in the phase-aligned 2:1 build.

Do not use a broad `set_multicycle_path` on CPU/L2 signals. If the clocks are
actually unrelated board inputs, use the script's asynchronous mode instead,
but retain destination payload registers and review `report_cdc`. Vivado's
generic CDC classifier still calls the hand-coded bundled-data protocol
"unknown" because it is not an XPM macro; the finite timing bounds and the
toggle/payload structure must therefore remain part of sign-off review.

## Verification and reproduction

Direct C++ checks pass for CSR redirect selection, signed/unsigned iterative
multiply/divide corner cases, and request/response mailbox payload capture.
All 18 CppHDL L1 configurations and all six Verilated L1 configurations pass.
The rebuilt native `csr_time`, `fence`, `trap_ra`, and `sbi_return` CPU tests
pass with the fetch and final-load-result stages. An actual generated-RTL
Verilated `trap_ra` run also passes. Aggregate BUILD_TESTING remains blocked by
missing repository sources `cpu_sfence.c`, `multicore_start.S`, and
`multicore_test.cpp`, not by the RISC-V compiler.

```sh
bash tribe_cpu/fpga/generate_tribe_rtl.sh
env TRIBE_CPU_PERIOD_NS=3.205128 \
    TRIBE_L2_PERIOD_NS=6.410256 \
    TRIBE_CDC_MODE=timed \
    /tools/2026.1/Vivado/bin/vivado -mode batch \
    -source tribe_cpu/fpga/run_tribe_timing.tcl
```

For quick isolated measurements use `analyze_execute_312.tcl`,
`analyze_l1_312.tcl`, and `analyze_l2_156.tcl`. Use
`check_tribe_loops.tcl` on the synthesized DCP; zero reported loops is a hard
requirement.

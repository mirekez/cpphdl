# CVA6 context-preserving replay

This is a diagnostic cut-out, not a replacement implementation of CVA6.
It regenerates the original 2-slave/10-master `axi_xbar_intf`, its crossbar,
demuxes, muxes, arbiters and AXI interface packing through hdlcpp and cpphdl.
The address map is extracted from the original testharness. Both simulators
check every boundary output against traffic captured from full native matmul.

## Preserve compiler context

Add `--replay-export=/absolute/path/full-context.txt` to the existing full-model
cpphdl comb-generation command. Export does not change generated code.
The file records each reachable producer's relative instance path, module
name, comb/port name and eager/demand classification before graph optimization.

A cut-out imports the classification with:

```
--replay-context=/absolute/path/full-context.txt
--replay-source=i_axi_xbar
--replay-target=dut.dut
```

Paths are relative to the respective hierarchy roots. Import adds demand
constraints before the usual dependency closure. It never adds memoization
rights, inserts artificial loops, or hardcodes evaluation multiplicities.
The retained RTL consumers determine the actual number of evaluations.
The source and destination must use the same scheduling mode. Malformed,
duplicate, wrong-mode, wrong-module and completely unmatched contexts fail.

## Native aggregate storage

Add `--native-packed` to `check_context_replay.py` to enable the generated
aggregate representation. The script passes `-DCPPHDL_NATIVE_PACKED` to both
cpphdl analysis and every C++ model/driver translation unit. Reusing a build
with a different layout setting is rejected. This does not remove replay
context, skip repeated inputs, or replace any RTL block.

hdlcpp marks non-union structs whose fields have known logical layouts.
cpphdl's runtime then stores eligible packed arrays as addressable elements,
serializing only at bit-vector boundaries. Generated conversions compare field
names, bit offsets and widths before copying fields directly; incompatible
layouts and custom packers retain pack/unpack conversion. Metadata inherited
by a derived class is not a proof of that class's layout.

The flag is an explicit C++ ABI choice (C++20 or newer). Rebuild all generated
sources and callers together; native aggregate arrays expose `pack()` for
serialized bits rather than the legacy `.data.bytes` representation. Ordinary
untagged arrays and the default, flag-free C++17 runtime are unchanged. There
are no CVA6-specific type allowlists or handwritten hardware implementations.

This is **not a serialized complete schedule**. Module parameters and source
revisions must also match; the context format checks module names, not a
parameter/body fingerprint. Input manifests, output comparisons and independent
evaluation counts are required. Cutting away consumers can still change demand
multiplicity, observation order and native compiler optimization opportunities.

The register-file regression also accepts `--mode optimize-combs-l1`,
`--replay-context`, and `--replay-source` in `check_request_tree.py`; its target
is `dut`. For this CVA6 build the source path is
`i_ariane.i_cva6.issue_stage_i.i_issue_read_operands.i_ariane_regfile`.

## Capture and regenerate

Run from the cpphdl repository, substituting local tool/build paths:

```sh
python3 hdlcpp/tests/cva6/capture_bus.py \
  --native-build /home/me/cva6/work-ver \
  --verilator-root /home/me/cva6/tools/verilator-new \
  --riscv /home/me/riscv --output build/bus-capture

python3 hdlcpp/tests/cva6/check_context_replay.py \
  --cva6-source /home/me/cva6 \
  --model build/regeneration-comparison-20260914/model \
  --context build/full-context.txt --trace build/bus-capture/matmul-bus.bin \
  --output build/bus-replay \
  --verilator /home/me/cva6/tools/verilator-new/bin/verilator \
  --cxx /home/me/scalepnr/.conda/bin/clang++ --trials 5
```

The capture script copies one original native generated translation unit and
adds a read-only observer for its otherwise-local slave request bundle. It
links a separate executable; original native files are not rewritten. The
observer's generated signal names are specific to this native build and fail
explicitly if the expected local boundary cannot be found. This executable is
only a trace producer, never a timing baseline.

Each 800-byte record contains, in order, a 32-bit local active-low reset and
24/47/10/118 32-bit words for slave requests, master responses, slave responses
and master requests. Logical widths are 748/1480/292/3760 bits. Unused high
padding bits must be zero. Files currently use the capture host's little-endian
32-bit word representation; they are not a portable/versioned interchange
format. The decoder rejects empty, truncated, unreset and malformed records.

Capture occurs after low-clock evaluation, immediately before the rising edge.
It uses the crossbar's synchronized **local** reset rather than top-level reset.
The cpphdl checker settles asynchronous reset before observing outputs.

Conversion uses production elaboration metadata and original RTL, including
AXI assignment macros. Constant-index interface wiring is generated rather
than using a SystemVerilog generate-loop alias that hdlcpp does not yet lower
correctly. No arbiter, register file or crossbar behavior is hand-replaced.

## Measurement and limits

Both drivers validate all 4,052 logical output bits on every captured cycle
before timing. Trace parsing/preparation and validation are outside the timer;
input updates, model work, output reads and checksum accumulation are inside.
Clang uses `-O2` for both builds, including Verilator's normally separate
fast/slow partitions. The Verilator frontend separately uses the production
`-O3 --unroll-count 256 --vpi --threads-dpi none --no-timing` flags.
Trials alternate order and run serially on the selected
CPU. `timings.json` retains individual times, checksums and cycle counts.

`commands.json`, `inputs-sha256.json`, and `binaries-sha256.json` retain build
provenance. `--reuse-build` verifies the recorded inputs/executables before and
after timing; use it only with the original build arguments.

Keep the full-CVA6 gate. The RF and bus cut-outs do not include CSR/PMP-to-RVFI
observation, scoreboard forwarding or HPDcache replay-table producers. Matching
functional traces or a slowdown ratio alone does not establish whole-core
performance representativeness.

## Paired execution profiling

`profile_context_replay.py` profiles already-built, unstripped `BusRun` binaries;
it never regenerates CVA6 or rebuilds either simulator. Keep artifacts outside
the source repository:

```sh
python3 -B hdlcpp/tests/cva6/profile_context_replay.py \
  --cpphdl /external/native-build/run \
  --verilator /external/verilator-build/VXbarBench \
  --trace /external/capture/matmul-bus.bin \
  --output /external/new-profile-directory --cpu 2 \
  --valgrind /usr/bin/valgrind
```

Omit `--valgrind` for sampling only. An externally extracted Valgrind package
can use `--valgrind-include /external/root/usr/include` and
`--valgrind-lib /external/root/usr/libexec/valgrind`. The only compiled artifact
is a small preload profiler, written under `--output`. Linux x86-64,
libstdc++'s dynamic `steady_clock::now`, GCC, `nm`, and `c++filt` are required.
Do not use this interposer for arbitrary drivers or multithreaded models.

The measurement modes are deliberately separate:

* Five serial, alternating-order, CPU-pinned **uninstrumented** work timings.
* Two native PC sample runs at different timer periods (1 and 7 ms), each
  running the same 20 full-trace repetitions on each simulator. Module-aware
  symbolization retains unresolved samples rather than dropping them.
* Optional Callgrind execution counts and caller/callee counts for one full
  trace, with cache and branch simulation. These are instruction/reference
  counts and **simulated** misses, not hardware counters or speed measurements.

The preload intercepts only the two `steady_clock` calls originating inside
the driver's `main` symbol. Intercepting the first two process-wide clock calls
is unsafe: Verilator reads clocks during initialization. All output checking,
trace preparation, and construction remain outside the profiling gate.
Unexpected timer calls, sample overflow, failed output checks, inconsistent
cycle counts/checksums, and changed input hashes fail the run.

`commands.json` and `inputs.json` record reproduction commands and fingerprints.
`results.json` retains separate timing/sampling/Callgrind results; `summary.json`
normalizes to simulated cycles. The raw `.callgrind.1` files contain the gated
call graphs (the final automatic `.callgrind` dump is not the work interval);
`*-symbols.json` exposes exclusive costs and call edges without requiring a GUI.
Samples assigned to an evaluator include its inlined arithmetic and helpers:
they are **not** a measurement of pure scheduler or timestamp-check overhead.
Estimated category nanoseconds use uninstrumented time and sample proportions;
never compare only percentages across implementations with different runtimes.

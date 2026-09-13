# CVA6 regeneration and matmul comparison after both fixes

## Result

Full SystemVerilog conversion, cpphdl L1 optimization, and a fresh C++ build
completed. **Matmul still does not pass under cpphdl.**

Both validation runs use the same ELF and **69,315 total clocks: 10 reset clocks
and 69,305 work clocks**. Times exclude conversion, compilation, initialization,
reset, and ELF loading.

| Simulator | Work clocks | Simulation-loop time | Process wall time | Result |
| --- | ---: | ---: | ---: | --- |
| Original CVA6 Verilator example | 69,305 | 4.943764 s | 5.307513 s | `PASSED`, `tohost=0` |
| Regenerated cpphdl, both fixes | 69,305 | 53.260753 s | 53.289890 s | Timeout; no `PASSED` |

These are single validation samples, not repeated benchmark medians. Because
the outputs differ, the comparator stops before its timing trials and records
`equivalent: false`. **There is no accepted equivalent-output speed ratio.**

## What changed functionally

A separate 5,000-clock run with commit logging, but without the invasive IRQ
getter diagnostic, observes six retirements:

| Trace cycle label | Retired PC |
| ---: | --- |
| 279 | `0x10000` |
| 284 | `0x10004` |
| 289 | `0x10008` |
| 294 | `0x1000c` |
| 299 | `0x10010` |
| 304 | `0x10014` |

No further retirements appear through 5,000 clocks. The previous fresh model
observed zero retirements and remained near the first ROM fetch. Thus the fixes
improve boot progress, but do not establish full CPU correctness. The diagnostic
RVFI instruction/order fields are zero; this is not a validated instruction-by-
instruction equivalence trace.

A separate IRQ diagnostic at label 999 shows frontend NPC `0x80000004`, with an
instruction-cache fetch address of `0x80000000`, and the CPU read channel at
`RVALID=1, RREADY=0`. This points to a remaining instruction-fetch/read-response
stall after the boot-ROM jump. Those diagnostic getters can recompute values;
they are not used in the timed comparison or proof of the exact root cause.

## Regeneration and build

Artifacts: `build/fixed-equivalence-20260913/`.

- Fresh conversion: 284 SV conversion sources, 48 skipped entries, 287 generated
  files, zero failures. Field-contract discovery converged in seven passes.
- Fresh optimization: 335 concrete aliases, 126 bounded collection units plus
  the root seed; 958 instances, 767 scheduled values, 11,171 dynamic evaluators,
  12,617 dynamic states, and 377 lazy-cycle back-edges. The previous model had
  582 back-edges; fewer back-edges alone does not prove correctness.
- All **106 linked object files** were freshly compiled. No old C++ object or
  hand-written hardware replacement was used.
- Clang 21, C++23, `-O2` cycle code and the existing `-O0 -fno-inline`
  constructor policy. Linking uses `-Wl,-s` to remove symbols and conserve disk
  space, not to change simulated hardware or cycle code.
- Successful isolated optimization took 1,115.42 seconds; compilation/linking
  took 2,586.99 seconds. These are separate from the simulation measurements.
- The reference's 300 recorded RTL/include inputs still match their Verilator
  generation records by size and timestamp. Its make check required no rebuild;
  its executable hash is unchanged. This reuses the original Verilator model,
  rather than claiming a fresh Verilator RTL generation.

### Isolation and preserved baseline

A concurrent edit of the shared runtime header invalidated the first collection
PCH at unit 122. That attempt is retained in `optimization-unisolated.log`; it
was not used for the executable. Optimization was restarted from scratch with
the saved runtime headers and optimizer executable containing both fixes.
The `clang++-frozen` and `cpphdl-frozen` wrappers put those immutable headers
first on the include path for collection and compilation. Snapshot checksums
were verified after optimization and after the build.

The old executable remains byte-identical. Its obsolete PCHs were removed;
its object/dependency files were archived in
`build/full-equivalence-20260912/build-objects.tar.gz`, compared against the
originals with `tar --compare`, and only then removed from the cache directory.

## Reproduction and identifiers

```bash
python3 hdlcpp/tests/cva6/compare_matmul.py \
  --verilator /home/me/cva6/work-ver/Variane_testharness \
  --cpphdl "$PWD/build/fixed-equivalence-20260913/model/run_cpphdl_testharness_opt" \
  --output "$PWD/build/fixed-equivalence-20260913/recomparison" --cpu 2
```

The comparator pins both processes to CPU 2 and clears diagnostic overrides.
The host was shared with other workloads; these samples are not exclusive-host
performance medians. Commands, statuses, clocks, and timing data are retained in
`comparison/comparison.json` and its per-run logs.

- ELF SHA-256: `a6585f05afe272344411dd651d5412292a4b65039a108da1f44da90ea2226c8c`
- Verilator SHA-256: `cb11ead70712c9d333238904c9ae0535580b00e74dce450ec1b1f19a22f0b763`
- New cpphdl SHA-256: `b18f14570a543a26c0f66f5282babde89d7a400fedc88acee1d0594c9d0a06dc`

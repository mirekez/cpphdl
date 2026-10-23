# Parallel drive for the sampled receiver

`parallel_receive_frontend<Count>` derives from the 2 pF
[`wideband_receive_frontend`](wideband_receive_frontend.md). It adds one
`nmos_resistor_amplifier` and one `nmos_source_follower`. Their ports join the
existing input, amplifier outputs, ADC input and supplies. Each extra component
contributes its physical current and charge; no voltage is assigned over an
existing purpose declaration.

Both amplifier paths use 500 Ω drain loads and 100 fF explicit drain storage.
Both followers use the same channel model, 250 Ω return and explicit parasitic
capacitors. This preserves the nominal static topology's ratios while reducing
its drive impedance. The actual shared supply resistance, intrinsic MOS charge
and doubled input loading can change the achieved gain and settling. They are
retained in simulation and must pass the same checks.

The tradeoff is more analog drive current and three additional MOS channels per
lane. The independent path has six resistors and ten explicit capacitors.
Sampling storage stays at 2 pF per slice. This candidate follows the failed 4 pF settling/accuracy experiment. Its
continuous four-slice PSP run passes with raw codes `[127, 111, 94, 118]` and
errors `[2, 1, 0, 0]` counts. Native and Verilog-A one/four-slice conversion checks each pass independently,
followed by passing comparisons. The same OpenVAF executable is used through a
byte-identical hard link under a measured 600 MiB aggregate-RSS watchdog to
share this workstation with the router; the four selected CTests pass in 69.40 s.
The maintained sixteen-slice test also passes 32 sustained conversions in
native C++ and Verilog-A, followed by a passing comparison. Its continuous
PSP run passes all 32 conversions and 256 causal controller decisions, with
maximum two-count code error, 6.280 mV frontend aperture error, 17.762 mV
sampler acquisition error and 7.710 mV hold drift. External supply/control
adapters remain in that fixture; this is not a physical-layout or PVT pass.

The four-slice process results expose the connected-circuit tradeoffs:

| Variant | Frontend aperture error | Sampler acquisition error | Hold drift | Code errors | Result |
| --- | --- | --- | --- | --- | --- |
| Original 2 pF wideband | 3.632 mV | 14.534 mV | 7.731 mV | 2, 1, 0, 0 | Pass |
| 4 pF, single drive | 23.765 mV | 11.513 mV | 4.174 mV | 1, 3, 2, 1 | Fail |
| 2 pF, parallel drive | 2.361 mV | 17.020 mV | 7.702 mV | 2, 1, 0, 0 | Pass |

Parallel drive improves the frontend metric but leaves less acquisition margin.
Both its four-slice and full-array PSP runs reach the two-count accuracy limit.
Further process/supply/noise/mismatch checks must determine whether that
remaining margin is sufficient. The 3 pF variant tests additional hold and
acquisition margin at a higher capacitor area.

The closed PSP binder requires exactly two shared-input amplifier paths and two
followers on the observed amplifier/ADC wires. It rejects additional devices,
changed channel parameters, missing charge, wrong shared nodes and incorrect
path counts. It uses actual IHP transistor models and finite-impedance host
controls. The reference remains the independently measured DC supply surface,
with the original two-count accuracy bound and 20 mV settling/hold bounds.

```sh
python3 analog/1G/IHPSG13G2/tests/receive_conversion_process.py \
  --bench analog/build/1G/IHPSG13G2/analog_1g_ihp_receive_parallel_conversion \
  --fixture-source analog/1G/IHPSG13G2/tests/receive_parallel_conversion.cpp \
  --pdk analog/pdk/IHPSG13G2 \
  --ngspice analog/out/veriloga-toolchain/install/bin/ngspice \
  --work analog/build/1G/IHPSG13G2/tests/results/receive_parallel_closed_4 \
  --slices 4 --input-drivers 2 --backend xspice
```

C++/Verilog-A generic tests and comparison are registered for acquisition and
one/four/sixteen-slice closed conversion. Closed process tests use the matching
explicit path count. The separate acquisition-only corner binder currently
supports the original single path; it must be extended and qualified before a
parallel-drive acquisition corner sweep can run. Full-chip clock/control,
noise, mismatch, extracted layout and cable integration remain separate work.

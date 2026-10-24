# Receiver with acquisition and hold margin

`balanced_receive_frontend<Count>` derives from
[`parallel_receive_frontend<Count>`](parallel_receive_frontend.md). It retains
both physical input drive paths, the inherited IC ports, 100 fF shared amplifier
storage and 500 Ω comparator preamplifier loads. Each sample capacitor is 3 pF.
The purpose is to hold more charge while preserving acquisition time.

The initial four-slice process experiment passes the original checks: codes
`[126, 111, 94, 118]`, errors `[1, 1, 0, 0]` counts, 2.008 mV maximum frontend
aperture error, 7.602 mV acquisition error and 5.478 mV hold drift. This is more
acquisition margin than the 2 pF parallel-drive run's 17.020 mV error. The
standalone prototype and its source/tool hashes remain under `out/`; the class
and fixtures now reside here for maintained regression testing. The maintained one/four-slice C++ and Verilog-A conversion tests each pass,
followed by passing comparisons. Native closed-loop operation also passes
32 consecutive conversions across all sixteen slices: maximum code error is
one count, frontend aperture error 12.685 mV, sampler acquisition error
1.530 mV and hold drift 2.341 mV. The maintained continuous PSP run also passes
all 32 conversions and 256 causal controller decisions: maximum code error
two counts, frontend aperture error 7.389 mV, sampler acquisition error
9.554 mV and hold drift 5.474 mV. The minimum achieved supplies are 3.150 V
and 1.127 V with the fixture's finite supply impedances.

Native acquisition checks pass for one, four and sixteen slices, with 4 pF
and 40 pF extra load cases and timestep refinement. The negative control uses
400 pF because the stronger buffer also meets the unchanged positive limits
at 40 pF. This load profile does not change the voltage/accuracy limits or
establish process-device agreement. The independent Verilog-A acquisition flow
and its C++ comparison also pass this complete load profile. Sixteen-slice
Verilog-A closed conversion and the separate C++/Verilog-A comparison pass too.

This is the leading integration candidate because its full-array acquisition
margin exceeds the 2 pF parallel-drive result's 17.762 mV error. Both versions
meet the same two-count bound; the larger capacitor does not establish noise,
mismatch or corner qualification by itself.

Relative to 2 pF storage this adds 16 pF per sixteen-slice lane, while using less
storage than the rejected single-drive 4 pF variant. Physical capacitor area
must be obtained through the [MIM layout generator](capacitor-layout.md), then
checked in the complete layout. The extra drive current remains part of the
supply budget; storage and transistor count alone do not determine die area.

```sh
cmake --build analog/build --target analog_1g_ihp_receive_balanced \
  analog_1g_ihp_receive_balanced_conversion
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_receive_balanced' -j 1
```

Generic acquisition and closed-conversion tests are registered in C++,
Verilog-A and comparison flows. The closed process tests use two explicitly
verified input drive paths. The acquisition-only corner binder still supports
one path and does not qualify this class. Noise, mismatch, physical clock/control
translation, layout, extracted performance and full PHY integration remain open.

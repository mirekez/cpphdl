# Faster shared-amplifier receiver

`wideband_receive_frontend<Count>` derives from
[`receive_frontend<Count>`](receive_frontend.md). It retains the same member
ports, 2 pF sampling capacitors, buffered decision latches and 1 kΩ SAR ladders.
The shared amplifier's two explicit output capacitors change from 300 fF to
100 fF. Each comparator preamplifier uses 500 Ω drain loads while retaining
its 300 fF output capacitors. The MOS devices and their intrinsic charge remain in the process model.
There is no ideal gain or settling override.

The purpose is to deliver the amplified input before the sampling aperture.
A fixed-history four-slice PSP diagnostic measured 25.398 mV maximum aperture
error with 300 fF. At 100 fF it fell to 3.795 mV, while sampler acquisition
remained below 13.104 mV. This diagnostic justified implementing the derived
class; it was not a closed-conversion pass. Reducing these two capacitors also
reduces explicit capacitor area by 0.4 pF per receive lane.

The initial version with 1 kΩ preamplifier loads passed all twelve generic
C++/Verilog-A acquisition and closed-conversion CTests, including 32 consecutive
conversions with sixteen-slice reuse. Its first continuous PSP conversion passed,
but its four-slice PSP run returned `[128, 112, 95, 118]` and missed the two-count
accuracy limit on one conversion. Its other electrical checks passed.

Probing that circuit exposed a positive 17.215 mV preamplifier difference at
the 20 ns evaluation edge, although the held input was below the reference.
The difference became negative later, after regeneration had captured the
wrong bit. Reducing the drain loads to 500 Ω made it negative 13.276 mV at
the same edge and corrected the first SAR decision. Reducing preamplifier
capacitance alone did not correct the bit; increasing sampling storage to 4 pF
with the original 1 kΩ preamplifier failed the frontend settling and conversion limits. These diagnostics retain
the original limits and motivate the current load-resistance change.

The 500 Ω revision passes all twelve generic C++/Verilog-A acquisition,
closed-conversion and comparison CTests. The sixteen-slice test performs 32
conversions with slice reuse; both solvers return the same raw and normalized
codes. Each flow independently meets the original electrical and accuracy
checks before comparison.

Nominal PSP acquisition passes for one, four and sixteen slices. Their maximum
acquisition errors are 10.210, 6.507 and 6.234 mV, respectively; maximum hold
drift is 0.534, 2.536 and 2.185 mV. The continuous four-slice PSP conversion
returns `[127, 111, 94, 118]`, with errors `[2, 1, 0, 0]` ADC counts. All checks
pass, including 3.632 mV frontend aperture error, 14.534 mV sampler acquisition
error and 7.731 mV hold drift. The first conversion meets the two-count bound
at its limit; this is not a noise or mismatch margin qualification.

The sixteen-slice continuous PSP run completed all 32 conversions and failed
accuracy on two conversions: three ADC counts of error against the unchanged
two-count limit. All other electrical, hold, control-delivery and normalization
checks passed. Its loaded rail falls outside the original single-supply DC
reference's 20 mV band. A separate 25-point DC characterization across differential
input and achieved internal supply confirmed the accuracy failure; it did not
change the transient or relax its checks. The original failed evidence is retained.

The extended acquisition sweep passes all 45 combinations of five MOS corners,
−40/27/125 °C and one/four/sixteen slices at nominal external supplies. This
establishes acquisition/hold performance for that experiment; it does not
erase the separate closed-conversion accuracy failure above. The
separate [`precise_wideband_receive_frontend`](precise_wideband_receive_frontend.md)
variant tests 4 pF storage with the faster 500 Ω preamplifier. It does not inherit
a qualification pass. Extracted-layout and cable qualification are also separate
work. Passing generic equations does not establish process-device agreement.

```sh
cmake --build analog/build --target analog_1g_ihp_receive_wideband \
  analog_1g_ihp_receive_wideband_conversion
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_receive_wideband_conversion_' -j 2
```

The PSP tests use the continuous [XSPICE adapter](xspice-cosimulation.md).
Nominal acquisition must pass for one, four and sixteen slices before the
five-corner/three-temperature acquisition sweep. That sweep holds the external
supplies at 3.3/1.2 V; it does not replace supply, noise, mismatch, clock/control
translation or extracted physical integration checks.

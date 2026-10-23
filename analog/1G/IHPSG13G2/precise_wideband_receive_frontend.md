# Receiver with increased sampling storage

`precise_wideband_receive_frontend<Count>` derives from
[`wideband_receive_frontend<Count>`](wideband_receive_frontend.md). It uses the
same inherited IC ports and connected amplifier, buffer, MOS sampling switches,
SAR DAC, comparator, latch and level interface. Each sampling capacitor is 4 pF
instead of 2 pF. The shared amplifier remains at 100 fF and the comparator
preamplifiers retain 500 Ω loads with 300 fF output capacitors.

The purpose is to retain more sampled charge during conversion, reducing the
voltage change caused by comparator and switch charge. The added capacitance
also loads the shared buffer and must still acquire within the same sampling
window. Tests retain the two-count ADC accuracy bound and all existing frontend,
acquisition, hold, supply, phase, delivery and normalization checks.

The storage increase is 32 pF per sixteen-slice lane, or 256 pF across eight
receive lanes in a two-port four-pair repeater. This is a circuit-level tradeoff;
a capacitor layout and extracted circuit are needed to establish its physical
area and parasitic cost. It does not justify increasing every capacitor.

The 4 pF variant fails its continuous four-slice PSP test. It returns
`[124, 113, 96, 117]`, with errors `[1, 3, 2, 1]` counts. Its maximum frontend
aperture error is 23.765 mV against the 20 mV limit. Acquisition error is
11.513 mV and hold drift is 4.174 mV; those checks and supply, decision,
normalization and clipping checks pass. The added storage improves hold but
increases shared-node settling error. This is a retained failed candidate.

Native generic acquisition and one/four-slice conversion checks pass; the queued generic Verilog-A flows were stopped after the process test rejected
this candidate. The completed native results and the failed process waveform
are retained; no full generic comparison pass is claimed. A completed 2 pF sixteen-slice PSP trajectory has two
three-count errors out of 32 conversions. The earlier 4 pF diagnostic used the
slower 1 kΩ preamplifier and does not determine this variant's behavior.

The closed process test uses 25 DC operating points: five differential inputs
at each of five source supplies from 3.0 to 3.6 V. Interpolation uses the solved
internal supply at each input, retaining the finite source resistance and
loading. Out-of-range input, common mode or supply fails qualification. The
reference is computed from the same circuit at DC, not fitted to its ADC codes.

```sh
cmake --build analog/build --target analog_1g_ihp_receive_precise_wideband \
  analog_1g_ihp_receive_precise_wideband_conversion
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_receive_precise_wideband' -j 1
```

C++ and generated Verilog-A must pass the same absolute checks independently,
then pass solver comparison. IHP PSP process tests are additional checks and
retain their own results. Host clock/control adapters remain simulation devices;
this fixture does not establish physical clock translation or full-chip closure.

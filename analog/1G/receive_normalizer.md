# ADC-to-PCS sample normalization

[`ReceiveSampleNormalizer`](receive_normalizer.hpp) is a digital Module with
member ports. The physical ADC and PCS use different numerical conventions:
with a 3.3 V ADC range and 0.25 V per PAM level, one level spans about 19.3 ADC
counts. The receive PCS expects 32 counts per level, with zero at 128.

The stage applies a programmable offset and unsigned Q8.8 gain, rounds exact
half-counts away from zero, adds 128 and saturates to the eight-bit range.
`offset_in = 128` and `gain_in = 424` give a nominal gain of 1.65625. Gain and
offset are calibration inputs; this class does not estimate them. ADC
quantization and electrical errors remain visible in the resulting samples.

| Port | Meaning |
| --- | --- |
| `sample_in` | Raw unsigned eight-bit ADC sample |
| `offset_in` | Measured ADC code corresponding to zero signal |
| `gain_in` | Sixteen-bit unsigned Q8.8 scale |
| `valid_in`, `enable_in` | Accept a sample when both are high |
| `sample_out`, `valid_out` | Registered normalized sample and validity |
| `clipped_out` | This valid sample required output saturation |

There are eight register stages and one-sample-per-clock throughput. An input
accepted on edge n appears after edge n+7. Invalid samples travel with the
pipeline and deassert valid/clipped at its output while retaining the preceding
sample. Reset or disable immediately forces neutral code 128 and flushes all
in-flight validity. Offset and gain are captured with each sample.

The datapath registers raw inputs, centering/calibration, eight signed 9x2
products, four recombined 9x4 products, two sums, integral/fraction separation,
rounding and clipping. The complete mathematical product fits signed 25 bits;
the integral stage splits its carry path into a nine-bit low sum and parallel
upper-word increment/decrement alternatives. Registering raw inputs separates external
hold repair from subtraction. Splitting each four-bit multiplier digit avoids
the long multiplier path exposed by routed timing.

The first eight-stage implementation maps to 1,739 IHP cells and passes the slow-cell
4 ns setup estimate with +0.86 ns slack. The final routed component passes
setup with +0.15 ns and hold with +0.06 ns slack, using 2,193 extracted nets.
It passes exact native/post-route functional checks, strict-port LVS with
42 matched circuits (48 top ports, 23,090 instantiated devices), and all
868 full-PDK DRC categories with zero violations. Metal2 density is 36.21%.
The proof is `IHPSG13G2/results/normalizer_runs/ReceiveSampleNormalizer-cgms0ht5/physical_module.json`;
its source snapshots and hashes identify the exact implementation and tools.
The rounding stage adds the disjoint constant 128 and one-bit rounding
carry to the integral's low byte only. The current clipping logic decodes the
byte carry directly: integral bit 7 is set, or the low seven bits equal 127
and rounding adds one. It checks the upper word's sign and all-zero/all-one
patterns without signed subtraction. This removes the byte adder and signed
range arithmetic from the clipping path while preserving arithmetic and
latency. The earlier integral carry-select implementation passes 16,261 identical native, RTL
and mapped-cell contract clocks. Its routed qualification is in progress; the earlier
GDS result above identifies the preceding implementation exactly.
The current partial-product recombination also uses carry selection. In
`a + 16*b`, the low nibble passes through, the middle eight bits add, and
the upper five-bit increment/decrement alternatives compute in parallel.
This replaces the thirteen-bit carry chain that missed routed lane setup
by 40 ps. Latency, rounding and clipping are unchanged. The expanded unit
suite passes 409,477 identical native, RTL and mapped-cell clocks, including
every 16-bit gain at six signed input differences. Its slow-cell setup
estimate was +0.91 ns at 4 ns. That composed lane route failed setup by
0.173278 ns from the integral sign bit to the high-clipping register; hold
passed at +0.053446 ns and the complete gate trajectory remained exact.
The direct clipping decode now passes the same 409,477 unit clocks in native,
RTL and mapped cells, the full lane trajectory in all three digital flows,
and 60,000 native reset/enable boundary clocks. The slow-cell setup estimate
remains +0.91 ns. The composed lane now passes extracted setup at
+0.077566847 ns and hold at +0.053977489 ns in the unchanged 500 µm floorplan,
and its complete routed gate trace matches. DRC/LVS remain in progress.
Earlier standalone physical results above retain their original source scope.

The first eight-stage route failed by 0.77 ns; unequal clock-repair chains
contributed about 0.68 ns of skew on its worst path. That iteration retains
exact native/postroute functional evidence and failed timing reports. Full
DRC/LVS was stopped before qualification. The passing run keeps the same
datapath and uses 100 µm clock-buffer spacing instead of 25 µm, with unchanged
setup, hold, slew, fanout and capacitance checks.
Earlier six- and seven-stage layouts failed setup by 0.52 and 0.46 ns;
neither is accepted as a 250 MHz implementation. Both passed strict-port LVS.
The six-stage layout passed DRC, while the seven-stage layout had one global
Metal2 density failure: 34.022%, below the required 35%. The final layout uses
the PDK's 0.42 µm Metal2 fill spacing, then legal 1 µm remainder fill squares
on a 1.425 µm lattice. The remainder pass rebuilds metal, transistor and
explicit fill exclusions before insertion. Geometry guards and the full PDK
deck check its dimensions, spacing and exclusions. Routed timing uses nominal
OpenRCX RC with slow/fast cells; fill coupling has not been extracted.

The native unit contract covers every input code with five offsets and twelve
gains, signed rounding, clipping, gaps, reset and disable. Its independent
oracle uses real-valued scaling and `lround`. Nominal physical ADC PAM codes
must land within two counts of the PCS constellation. Native, generated RTL
and IHP mapped eight-stage implementations previously passed 10,861 identical
contract cycles, including resets with in-flight samples. The expanded suite
adds gains around half-count/clipping boundaries for the shorter rounding path
and exhausts every gain at differences −255, −128, −1, +1, +127 and +255
for the partial-product carry selection.
[ADCReceiveController](adc_receive.md)
now composes this stage with the SAR sequencer. Full receive filtering, clock
recovery and chip integration remain separate work.

```sh
cmake --build analog/build --target analog_1g_normalizer
ctest --test-dir analog/build -R 'analog.1g.normalizer\.' --output-on-failure
```

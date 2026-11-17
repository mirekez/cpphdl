# One-slice physical closure experiments

The [delivered gain-three / 6.5 pF candidate](candidates/gain3_hold65/pnr.md)
is the complete 15-case reference. Its GDS passes full IHP DRC, strict LVS and
extraction, but the worst hold variation is 26.943 mV against 20 mV and the
slow/hot 1.15 V conversion has a seven-count error against two counts.
The tests below keep the same 50 Ω input source, 8 ns acquisition, 2 + 2 ns
bit timing, 20 MS/s sample spacing and electrical acceptance limits.

The following are **completed, GDS-extracted connected conversions**. Every
listed geometry passed full IHP DRC, strict LVS, OpenROAD placement import and
Magic extraction. `Invalid` counts core-domain decisions that were not at a
valid rail by the 25 ps-before-end read point. These are individual cases, not
full-range or full-corner qualifications.

```text
Geometry / corner / Vin                 Acq      Hold pp  Code/ideal  Error  Invalid  Result
------------------------------------  -------  -------  ----------  -----  -------  ------
Precharge x4, SS / 125 C, 0.85 V        9.336    19.181   63/65       2      0        PASS
Precharge x4, SS / 125 C, 1.15 V       11.085    23.524   96/89       7      1        FAIL
Left-node 3 fF trim, SS, 0.85 V        9.298    18.856   63/65       2      0        PASS
Left-node 3 fF trim, SS, 1.15 V       11.047    22.706   96/89       7      1        FAIL
Precharge x4 + half clock, TT, 0.85 V  11.355    21.484   63/65       2      0        FAIL
Precharge x4 + TRACK route, TT, 0.85 V 11.137    20.932   63/65       2      0        FAIL
Precharge x4 + IF20, TT, 0.85 V        9.820    19.808   63/65       2      0        PASS
Precharge x4 + IF20, SS, 1.15 V       11.079    23.147   92/89       3      1        FAIL
Precharge x4 + 2x pullup, TT, 0.85 V  9.825    19.816   63/65       2      0        PASS
Precharge x4 + 2x pullup, SS, 1.15 V 11.085    23.189   89/89       0      0        FAIL
Above + 2x clock PMOS, TT, 0.85 V    7.802    19.944   67/65       2      0        PASS
Above + 2x clock PMOS, FF, 1.75 V    8.059    23.438  137/135      2      0        FAIL
```

Acquisition and hold are in mV. Both half-clock sizing and reinforced TRACK
routing were rejected by the TT hold result. The reinforced route nevertheless
reduced extracted TRACK-to-D0-gate resistance from 76.84 to 49.89 Ω. A first
routing attempt shorted TRACK to IN where both Metal4 escapes had been widened;
LVS and extraction rejected that geometry. The corrected route passed both.
This illustrates why a DRC pass alone cannot establish connectivity.
`IF20` means a 20 fF MIM on the core interface intermediate node in place
of the reference 80 fF element.
The clock-PMOS rows add twofold local complement-driver PMOS width to the
fourfold-precharge/twofold-pullup geometry. That change improves the
fast/cold 1.75 V hold span relative to its 6.5 pF control (28.295 to 23.438
mV) but still misses the 20 mV requirement.

The full **five successive inputs per corner** for the
[physically placed precharge x4 / core-pullup x2 / 6.5 pF candidate](candidates/gain3_hold65_precharge4_pullup2/pnr.md)
have completed with verified
Analog++ receipts. Acquisition is at most 20 mV, hold peak-to-peak at most
20 mV, code error at most two counts, every read point valid and decision
setup at least 25 ps. `Result` applies all those gates plus pin-supply minima.

```text
Corner  Vin    Acq mV  Hold mV  Code/ideal  Err  Invalid  Setup ps  Result
------  -----  ------  -------  ----------  ---  -------  --------  ------
TT      0.85    9.825   19.816    63/65      2      0       790.0   PASS
TT      1.15   10.002   24.978    89/89      0      0       550.0   FAIL
TT      1.45   10.740   25.003   112/112     0      0       670.0   FAIL
TT      1.75   10.946   25.942   137/135     2      0       410.0   FAIL
TT      2.05   11.189   24.386   159/159     0      0       710.0   FAIL
SS      0.85    9.336   19.181    63/65      2      0       330.0   PASS
SS      1.15   11.085   23.189    89/89      0      0        30.0   FAIL
SS      1.45   13.315   23.216   112/112     0      0       210.0   FAIL
SS      1.75   14.902   23.939   136/135     1      1         0.0   FAIL
SS      2.05   17.480   22.609   159/159     0      0       330.0   FAIL
FF      0.85   10.294   20.405    63/65      2      0      1050.0   FAIL
FF      1.15   10.250   27.367    89/89      0      0       850.0   FAIL
FF      1.45   10.621   27.269   112/112     0      0       930.0   FAIL
FF      1.75   10.315   28.267   137/135     2      0       550.0   FAIL
FF      2.05    9.334   26.695   159/159     0      0       970.0   FAIL
```

The core-pullup change removes the original SS 1.15 V seven-count error,
but it leaves 13 of 15 conversions failing, chiefly on hold variation. The
SS 1.75 V final bit is invalid even though its code error is one count.

The 20 fF interface MIM fixes the original SS bit-2 delay, but its bit-5
decision is still invalid and the resulting code error is three counts. The
twofold core pullup fixes both late decisions and the **code error is zero**
at SS / 125 C, 1.15 V; that conversion still fails the hold requirement by
3.189 mV. The full five-input TT/SS/FF results below establish its broader
failure scope.

**Unplaced input-reservoir diagnostic.** A synthetic 2 pF capacitor from IN
to VSS was inserted into the pullup variant's exact extracted RC binding.
Its TT 0.85 V result improved acquisition 9.825 to 8.803 mV, while hold
variation changed only 19.816 to 19.776 mV. In matched first-sample FF
1.75 V runs, hold changed 28.295 to 27.987 mV and still failed. The
reservoir substantially reduced the instantaneous upstream input dip but
did not remove the held-node charge disturbance. It has no matching GDS and
is not a placed candidate.

![Measured sampling-edge kickback](diagnostics/sampling_edge_dip.png)

The original placed sampling edge drives the 50 Ω upstream input sharply below
0.85 V and pulls the held node down as tracking ends. Stronger switch-clock
routing and the tested dummy-size changes have not removed the hold failure.

![Placed switch-gate clock skew](diagnostics/sampling_clock_skew.png)

At the 28.05 ns input trough, the NMOS switch gate is still at 1.67 V while
the local PMOS complement has undershot to -0.28 V. The latter does not rise
until after the input begins recovering. This is the measured edge used to
motivate the stronger complement-PMOS sizing trial; the trial requires its
own completed placement and conversion evidence.

![Fast/cold hold-window failure](diagnostics/fast_cold_hold_failure.png)

In the full placed FF / -40 C matrix, the 1.75 V sample falls to 1.732879 V
immediately after track opens and later peaks at 1.759822 V during SAR bit
evaluation. The 26.943 mV span exceeds the 20 mV limit by 6.943 mV. The
green band in the plot shows the allowed span measured from the actual
minimum; no input-relative DC error is substituted for this hold metric.

![Slow/hot decision path diagnostic](diagnostics/slow_hot_decision_delay.png)

The second plot is a marked **unplaced** fourfold-precharge diagnostic on the
reference extracted RC. At the slow/hot 1.15 V bit-2 trial, the comparator is at
the correct rails and the retained latch output has fallen to 0.089 V. The
interface intermediate node is only 0.709 V; the 1.2 V core output is 0.676 V
at 90.975 ns, between the valid-low and valid-high thresholds. The placed
precharge candidate likewise records one invalid decision and code 96.

The faster interface fixes the tested slow/hot 1.15 V code error; hold
variation and one 1.75 V read-point delay remain open in the completed
five-input TT/SS/FF matrix. Waveform, noise, stress and reliability checks
remain open for any promoted geometry. The one-slice result is **not ready
for replication**.

## Ten-picofarad storage trial

One legal IHP `cap_cmim` PCell is limited to 8 pF.
`parallel_capacitor_layout.py` now places two legal 5 pF MIM PCells in
parallel and joins like plates on Metal5 and TopMetal1. Its first 1.0 um
TopMetal1 bridge failed the PDK's 1.64 um `TM1.a` minimum; the corrected 2.0
um bridge passes the full DRC deck with zero violations, strict LVS, OpenROAD
import and Magic extraction. The process lowerer sees 66 physical MIM units,
including the two storage plates, and all 11 back-annotation guards pass.
The dedicated two-cell layout regression checks nominal storage, parallel
connectivity and bridge width.

A **synthetic**, unplaced 3.5 pF addition to the 6.5 pF extracted network
measures 15.161 mV hold variation at FF / -40 C, 1.75 V, versus 23.438 mV
with the same strengthened clock and 6.5 pF. It passes acquisition and code
limits in that single first-sample case. The synthetic result does not qualify
the placed geometry. A warm-start 2.05 V sample also cannot establish the
required 8 ns acquisition after an input change.

The [placed 10 pF candidate](candidates/gain3_hold10_pullup2_clockp2/pnr.md)
has completed the targeted connected conversions below. The 2.05 V slow/hot
row follows a 1.75 V sample and includes the required input transition.

```text
Geometry / corner / Vin                 Acq mV  Hold mV  Code/ideal  Err  Invalid  Result
--------------------------------------  ------  -------  ----------  ---  -------  ------
10 pF, TT / 27 C, 0.85 V                 6.095   15.152       68/65    3        0  FAIL
10 pF, FF / -40 C, 1.75 V                6.309   18.158     136/135    1        0  PASS
10 pF, SS / 125 C, 1.75 V                6.048   15.945     137/135    2        0  PASS
10 pF, SS / 125 C, 2.05 V after step    35.573   15.510     158/159    1        0  FAIL
10 pF + 2x sample, FF / -40 C, 1.75 V    9.675   26.855     137/135    2        0  FAIL
10 pF + 2x sample, SS / 125 C, 1.75 V    8.842   22.523     137/135    2        0  FAIL
10 pF + 2x sample, SS, 2.05 V step      14.389   24.346     159/159    0        0  FAIL
10 pF + 2x sample/dummy, FF, 1.75 V      9.013   21.531     136/135    1        0  FAIL
10 pF + 2x sample/dummy, SS, 1.75 V      8.336   19.608     136/135    1        0  PASS
10 pF + 2x sample/dummy, SS, 2.05 step  14.243   18.367     159/159    0        0  PASS
Above + 4x clock PMOS, TT, 0.85 V       13.653   27.323       63/65    2        0  FAIL
Above + 4x clock PMOS, FF, 1.75 V       12.539   28.117     136/135    1        0  FAIL
Above + 4x clock PMOS, SS, 1.75 V       11.109   24.014     136/135    1        0  FAIL
Above + 4x clock PMOS, SS, 2.05 step    15.964   22.707     159/159    0        0  FAIL
10 pF + 2x sample/3x dummy, TT, 0.85 V   9.428   31.835       68/65    3        0  FAIL
10 pF + 2x sample/3x dummy, FF, 1.75 V   9.798   24.272     136/135    1        0  FAIL
10 pF + 2x sample/3x dummy, SS, 1.75 V   9.152   22.397     137/135    2        0  FAIL
10 pF + 2x sample/3x dummy, SS, step    15.325   18.251     159/159    0        0  PASS
10 pF + 2x sample, dummy N3/P2, FF      11.533   38.848     137/135    2        0  FAIL
10 pF + 2x sample, dummy N2/P3, FF      16.944   28.773     135/135    0        0  FAIL
10 pF + 1.5x sample/dummy, FF, 1.75      7.228   16.578     136/135    1        0  PASS
10 pF + 1.5x sample/dummy, SS, 1.75      6.713   15.241     136/135    1        1  FAIL
10 pF + 1.5x sample/dummy, SS, step     19.074   14.341     159/159    0        1  FAIL
10 pF + 1.5x sample/dummy, TT, 0.85      6.965   17.081       63/65    2        0  PASS
10 pF + PMOS-only 2x, FF, 1.75           7.500   19.631     137/135    2        0  PASS
10 pF + PMOS-only 2x, SS, 1.75           7.099   17.551     137/135    2        0  PASS
10 pF + PMOS-only 2x, SS, step          16.846   17.379     159/159    0        0  PASS
10 pF + PMOS-only 2x, TT, 0.85           6.676   16.797       69/65    4        0  FAIL
1.5x sample/dummy + 3x pullup, TT 0.85   7.443   17.542       63/65    2        0  PASS
1.5x sample/dummy + 3x pullup, FF 1.75   7.712   19.182     135/135    0        0  PASS
1.5x sample/dummy + 3x pullup, SS 1.75   7.160   17.154     135/135    0        1  FAIL
1.5x sample/dummy + 3x pullup, SS step  19.548   16.238     159/159    0        1  FAIL
1.5x sample + 2x interface, TT 0.85      7.935   18.920       63/65    2        0  PASS
1.5x sample + 2x interface, FF 1.75      8.218   20.847     136/135    1        0  FAIL
1.5x sample + 2x interface, SS 1.75      7.633   18.860     136/135    1        1  FAIL
1.5x sample + 2x interface, SS step     19.993   17.922     159/159    0        0  PASS
1.5x sample + 20 fF IF, TT 0.85          9.621   17.981       68/65    3        0  FAIL
1.5x sample + 20 fF IF, SS 1.75          8.104   15.822     137/135    2        0  PASS
1.5x sample + 20 fF IF, SS step         19.807   14.924     159/159    0        0  PASS
1.5x sample + 20 fF IF, FF 1.75          9.029   17.255     137/135    2        0  PASS
1.5x sample + 40 fF IF, TT 0.85          9.622   17.983       68/65    3        0  FAIL
1.5x sample + 40 fF IF, SS 1.75          8.105   15.825     137/135    2        0  PASS
1.5x sample + 40 fF IF, SS step         19.808   14.927     159/159    0        0  PASS
1.5x sample + 40 fF IF, FF 1.75          9.030   17.261     137/135    2        0  PASS
1.5x sample + 50 fF IF, TT 0.85          7.014   17.154       67/65    2        0  PASS
1.5x sample + 50 fF IF, FF 1.75          7.278   16.754     136/135    1        0  PASS
1.5x sample + 50 fF IF, SS 1.75          6.759   15.348     137/135    2        0  PASS
1.5x sample + 50 fF IF, SS step         19.119   14.451     159/159    0        0  PASS
Above + 3x core pullup, TT 0.85           7.440   17.532       63/65    2        0  PASS
Above + 3x core pullup, SS 0.85           7.041   16.853       63/65    2        0  PASS
Above + 3x core pullup, FF 1.75           7.710   19.169     135/135    0        0  PASS
Above + 3x core pullup, SS 1.75           7.158   17.145     135/135    0        1  FAIL
Above + 3x core pullup, SS step          19.546   16.228     159/159    0        1  FAIL
3x pullup + 1.25x input, TT 0.85          7.440   17.532       63/65    2        0  PASS
3x pullup + 1.25x input, SS 0.85          7.041   16.853       63/65    2        0  PASS
3x pullup + 1.25x input, FF 1.75          7.710   19.169     135/135    0        0  PASS
3x pullup + 1.25x input, SS 1.75          7.158   17.145     135/135    0        1  FAIL
3x pullup + 1.25x input, SS step         19.546   16.228     159/159    0        1  FAIL
3x pullup + 1.40x input, TT 0.85          7.440   17.532       63/65    2        0  PASS
3x pullup + 1.40x input, SS 0.85          7.041   16.853       63/65    2        0  PASS
3x pullup + 1.40x input, FF 1.75          7.710   19.169     135/135    0        0  PASS
3x pullup + 1.40x input, SS 1.75          7.158   17.145     136/135    1        1  FAIL
3x pullup + 1.40x input, SS step         19.538   16.230     159/159    0        1  FAIL
3x pullup + 1.45x input, TT 0.85          7.440   17.532       63/65    2        0  PASS
3x pullup + 1.45x input, SS 0.85          7.041   16.853       63/65    2        0  PASS
3x pullup + 1.45x input, FF 1.75          7.710   19.169     135/135    0        0  PASS
3x pullup + 1.45x input, SS 1.75          7.158   17.145     136/135    1        0  PASS
3x pullup + 1.45x input, SS step         19.538   16.230     159/159    0        1  FAIL
4x pullup + 2.00x input, TT 0.85          6.956   16.800       70/65    5        0  FAIL
4x pullup + 2.00x input, SS 0.85          6.578   16.468       71/65    6        0  FAIL
4x pullup + 2.00x input, FF 1.75          7.221   16.292     137/135    2        0  PASS
4x pullup + 2.00x input, SS 1.75          6.707   15.138     137/135    2        1  FAIL
4x pullup + 2.00x input, SS step         19.047   14.136     159/159    0        0  PASS
3.5x pullup + 1.70x input, TT 0.85       6.956   16.800       70/65    5        0  FAIL
3.5x pullup + 1.70x input, SS 0.85       6.578   16.468       71/65    6        0  FAIL
3.5x pullup + 1.70x input, FF 1.75       7.221   16.292     137/135    2        0  PASS
3.5x pullup + 1.70x input, SS 1.75       6.707   15.137     137/135    2        1  FAIL
3.5x pullup + 1.70x input, SS step      19.048   14.147     159/159    0        0  PASS
30 fF latch, reordered, TT 0.85         11.365   24.872       68/65    3        0  FAIL
30 fF latch, reordered, SS 1.75          9.277   20.623     137/135    2        0  FAIL
30 fF latch, reordered, SS step         20.677   19.780     159/159    0        0  FAIL
1.5x sample + 60 fF IF, TT 0.85          7.448   18.341       63/65    2        0  PASS
1.5x sample + 60 fF IF, FF 1.75          7.716   20.005     136/135    1        0  FAIL
1.5x sample + 60 fF IF, FF 10 ps        7.690   20.194     136/135    1        0  FAIL
1.5x sample + 60 fF IF, SS 1.75          7.159   17.793     137/135    2        0  PASS
1.5x sample + 60 fF IF, SS step         19.538   16.878     159/159    0        0  PASS
1.5x sample + 1.5x interface, TT 0.85   14.953   38.424       63/65    2        0  FAIL
1.5x sample + 1.5x interface, SS 1.75   12.744   28.980     137/135    2        0  FAIL
1.5x sample + 1.5x interface, FF 1.75   14.176   34.506     136/135    1        0  FAIL
1.5x sample + 1.5x interface, SS step  23.926   27.928     159/159    0        0  FAIL
```

Twofold sampling-switch strength restores the SS 1.75-to-2.05 V acquisition
but its charge injection fails hold. Balanced twofold dummies pass both stepped
SS samples and miss FF hold by 1.531 mV. Larger or asymmetric dummies, a
fourfold complement PMOS and stronger ratioed core-interface devices worsen
hold. The intermediate 1.5-fold sampling pair passes TT and FF first samples
and both SS acquisition/hold values; SS bit 4 and final bit 7 are invalid at
the read points. Threefold core pullup does not fix them. Doubling both
interface devices fixes SS bit 4, but leaves bit 7 invalid and misses FF hold
by 0.847 mV. The 1.5-fold interface fails hold by at least 8.980 mV at SS.
Reducing the interface-node MIM to 20 fF fixes both SS samples and FF,
but TT code error rises to three counts. The 40 fF trial retains that TT
error; 60 fF returns the TT code to range but its 20 ps FF hold result is
20.0052 mV, just 0.0052 mV over the limit. Its stepped SS case passes; a
verified 10 ps FF refinement instead measures 20.194 mV and also fails. The
50 fF midpoint passes the four targeted conversions, but its full SS matrix
already shows code 70 versus ideal 65 at 0.85 V with two invalid late bit
decisions. Its complete [15-case matrix](candidates/gain3_hold10_sample15_dummy15_if50_clockp2/pnr.md)
passes all five TT and FF samples and three of five SS samples. A threefold
core pullup on that 50 fF layout repairs the targeted
SS 0.85 V sample and retains passing TT 0.85 V and FF 1.75 V results. Its
stepped SS test fails on one invalid decision in both samples; the 1.75 V
bit-4 core output is only 0.181 V despite a valid high comparator rail. A
placed 25% stronger interface input raises that output to 0.587 V but still
fails the valid-rail check. The 40% trial reaches 0.824 V against a 0.831 V
valid-high threshold and also fails. The threefold-pullup layout passes 14
of 15 sequential conversions; its SS 2.05 V final bit is invalid and
acquisition has only 0.100 mV margin. A [45% input-width trial](candidates/gain3_hold10_sample15_dummy15_if50_pullup3_input145_clockp2/pnr.md)
fixes the SS 1.75 V bit-4 read and passes the three other targeted first
samples, but its 2.05 V stepped sample still has an invalid final decision.
Its complete 15-case matrix passes 14 conversions and fails the SS 2.05 V
final decision with only 0.100 mV acquisition margin. All three 10 ps first
conversions pass scalar gates, but 20 ps versus 10 ps observed-waveform
convergence fails at TT, SS and FF. The completed 10 ps to 5 ps TT refinement
passes scalar and 20 mV peak waveform limits but fails the 1 mV RMS waveform
limit at 1.345 mV. The later 2.5 ps full TT conversion passes scalar gates
but fails the 5 ps to 2.5 ps peak waveform check; a 1.25 ps startup-only
diagnostic also fails 2.5 ps to 1.25 ps waveform peak and RMS checks.
A [larger balanced interface](candidates/gain3_hold10_sample15_dummy15_if50_pullup4_input200_clockp2/pnr.md)
advances the SS 1.75 V bit-4 high read but fails TT and SS 0.85 V
accuracy by five and six code counts respectively. Its FF 1.75 V and stepped
SS 2.05 V samples pass, but the stepped SS 1.75 V sample has an invalid
bit-6 read. An [intermediate interface sizing](candidates/gain3_hold10_sample15_dummy15_if50_pullup350_input170_clockp2/pnr.md)
has the same failure pattern with the same optimized component order. Those
two placed variants both reorder 119 of 137 component positions relative to
the 45% baseline, so their analog accuracy differences include changed
routing. A separate reduction of retained latch storage from 50 fF to 30 fF
was tested after a probe showed the latch output had not completed its fall
at the failing SS 2.05 V read. Its automatically reordered layout fails TT
code/hold, SS 1.75 V hold, and SS 2.05 V acquisition. The fixed-order
[larger-interface](candidates/gain3_hold10_sample15_dummy15_if50_pullup4_input200_fixed_clockp2/pnr.md)
and [reduced latch-load](candidates/gain3_hold10_sample15_dummy15_if50_pullup3_input145_latch30_fixed_clockp2/pnr.md)
layouts pass full physical checks and their TT/SS/FF first samples; both
still fail the SS stepped 2.05 V bit-7 validity read even though their
integer code equals ideal. A [fixed-order stronger latch-driver trial](candidates/gain3_hold10_sample15_dummy15_if50_pullup4_input200_nand2buf4_fixed_clockp2/pnr.md)
passes physical checks but its completed slow/hot 1.75 V and stepped 2.05 V
conversions each have one invalid decision despite exact codes. The completed
[internal-node probe](candidates/gain3_hold10_sample15_dummy15_if50_pullup4_input200_fixed_clockp2/evidence/ss-step-probe/probe-receipt.json)
shows Q-bar rising only from 110.5 to 110.9 ns before the 110.975 ns read;
Q and the core output fall too late. Per-bit CLEAR pulses within the existing
DAC-settle interval now pass complete slow/hot 1.75 V to 2.05 V stepped tests
on both fixed-order larger-interface and 45% interface-input geometries.
The first has 50 / 190 ps setup and 0.456 mV final acquisition margin; the
second has 30 / 170 ps setup and 0.462 mV acquisition margin. The larger
interface's complete pulse-controlled TT/SS/FF five-input matrices pass all
15 scalar conversions; the smaller placement's complete matrices also pass
15 / 15, with 0.100 mV final slow/hot acquisition margin.
The ordinary fixed-order larger-interface matrix completed at 14 / 15
conversions and the pulse-controlled version still has only 0.094 mV final
SS acquisition margin. A new [1.6× sampling/dummy trial](candidates/gain3_hold10_sample16_dummy16_if50_pullup4_input200_fixed_clockp2/pnr.md)
changes only four MOS widths and passes full physical checks; its extracted
connected TT/FF first-cycle and slow/hot stepped tests now pass. The
stepped final 2.05 V acquisition improves from 19.544 to 18.054 mV, but the
first 1.75 V decision setup falls from 50 to 30 ps against a 25 ps minimum.
The complete TT/SS/FF five-input matrices pass 15 / 15 scalar checks. The SS
final acquisition is 18.321 mV, leaving 1.679 mV margin, while FF 1.75 V hold
variation is 19.687 mV, leaving only 0.313 mV. A shorter 0.5 ns CLEAR-pulse
test passes the same stepped inputs but does not improve the limiting 30 ps
bit-4 setup. The HV screen still fails.
A [1.55× midpoint](candidates/gain3_hold10_sample155_dummy155_if50_pullup4_input200_fixed_clockp2/pnr.md)
with exactly the same component order has passed full IHP DRC, LVS,
OpenROAD import, extraction and back-annotation. Its targeted TT 0.85 V,
FF 1.75 V, and SS 1.75 V to 2.05 V cases pass; the stepped SS final
acquisition is 18.761 mV with 1.239 mV margin and first-decision setup is
50 ps. Its complete TT/SS/FF matrices pass 15 / 15 nominal scalar conversions;
SS final acquisition is 19.071 mV and FF worst hold is 19.419 mV. A separate
[stress-capacitance placement](candidates/gain3_hold10_sample155_dummy155_if50_pullup4_input200_fixed_clockp2_stresscap/pnr.md)
passes physical checks and back-annotation. Its complete TT 0.85 V case
passes scalar conversion checks and reduces the preamplifier DAC-side peak
|Vgs| from 3.635 to 3.125 V, but the retained latch's second input-buffer
peak |Vgs| remains 3.634 V
and fails the 3.3 V screen. The [0.5 fF latch Miller trial](candidates/gain3_hold10_sample155_dummy155_if50_pullup4_input200_fixed_clockp2_latchhalf/pnr.md)
also passes physical checks and TT scalar conversion but leaves HV |Vgs| at
3.633 V. A [1.75× sampling / 1.90× dummy placement](candidates/gain3_hold10_sample175_dummy190_if50_pullup4_input200_fixed_clockp2/pnr.md)
passes physical checks and brings combined-worst-passive SS acquisition to
19.329 mV, but both bit-7 reads are invalid and FF hold fails at 20.668 mV.
The midpoint's complete [best/best](candidates/gain3_hold10_sample155_dummy155_if50_pullup4_input200_fixed_clockp2/evidence/ss-passive-best-step-bitclear/archive.json)
and [worst/worst](candidates/gain3_hold10_sample155_dummy155_if50_pullup4_input200_fixed_clockp2/evidence/ss-passive-worst-step-bitclear/archive.json)
IHP passive-corner stepped receipts both fail at 2.05 V: the former has one
invalid final bit-7 core read, while the latter has 22.355 mV acquisition
against the 20 mV limit. MIM worst alone gives 22.350 mV and R3 worst alone
gives 18.773 mV, identifying MIM capacitance as the dominant acquisition driver
in the tested passive range; both isolated cases have one invalid first read.
A PMOS-only sampling switch passes both
SS samples and FF but has a four-count TT code error. No variant is yet
ready for replication.

## Acquisition failure parameter map, exact one-slice evidence

The 22.350 mV result is the **same 1.55× GDS** with SS MOS / 125 °C, R3 typical,
MIM worst, a 1.75-to-2.05 V step, and per-bit CLEAR. The [passive-corner
report](diagnostics/passive_corners.md) records the PDK values and hash-verified
case receipts. Results on other geometries or passive corners are labeled
separately so they are not treated as a pass of this exact case.

```text
Change / bound case                    SS final acquisition     Other gate before change         Other gate after change
-------------------------------------  -----------------------  -------------------------------  -----------------------------------
1.55x, R3 typ / MIM typ, step          18.761 mV, PASS          reference (no change)            HV |Vgs| >=3.635 V, FAIL
Same GDS, R3 worst / MIM typ           18.773 mV, PASS          0 invalid reads                  1 invalid 1.75 V read, FAIL
Same GDS, R3 typ / MIM worst           22.350 mV, FAIL          0 invalid reads                  1 invalid 1.75 V read, FAIL
Same GDS, R3 worst / MIM worst         22.355 mV, FAIL          0 invalid reads                  0 invalid reads
1.60x, typical passives, full SS       18.321 mV, PASS          FF hold 19.419 mV, PASS          FF hold 19.687 mV, PASS
1.75x / 1.90x, worst passives          19.329 mV, PASS          FF hold 19.408 mV; invalid 0    FF hold 20.668 mV; invalid 2
Earlier PMOS-only 2x, typical          16.846 mV, PASS          TT code error 3, FAIL*           TT code error 4, FAIL
```

The isolated MIM-worst increase accounts for 3.589 mV of the 3.594 mV
combined-worst acquisition increase relative to typical passives on the same
GDS. This measured comparison identifies the effective hold capacitance as
the dominant tested passive driver of tracking lag. A wider switch helps
typical-passive acquisition but has worsened FF hold variation; no tested GDS
passes the MIM-worst acquisition, FF hold and HV stress checks together.

*The PMOS-only row belongs to an older placed 10 pF campaign. Its three-count
TT baseline is not a same-GDS comparison. The 1.55× and 1.60× FF hold values
come from their complete five-input matrices; the 1.75× FF values are both
first-cycle measurements. The 1.75× row's invalid-read counts compare the
1.55× and 1.75× combined-worst-passive SS steps. HV stress remains above 3.3 V in the 1.55× and
1.60× trials and is being retested on the latch variant.

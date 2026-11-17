# IHP SG13G2 SAR slice qualification history

The candidate measurements and historical PHY requirements below are
process-specific. Current reusable-model behavior is documented in
[the generic slice report](../adc_sar_slice.md).

## Candidate and placed-slice results

The [gain-three / 6.5 pF candidate](candidates/gain3_hold65/pnr.md)
is the complete earlier reference. The newer [10 pF, 50 fF interface,
45% wider input trial](candidates/gain3_hold10_sample15_dummy15_if50_pullup3_input145_clockp2/pnr.md)
passes full IHP DRC and LVS and 14/15 GDS-extracted TT/SS/FF sequential
conversions with the original once-per-sample CLEAR schedule. Its SS 2.05 V
final decision is invalid, despite the correct integer code; acquisition is
19.900 mV against a 20 mV limit. A [per-bit CLEAR control experiment](diagnostics/bitclear.md)
passes a fresh slow/hot 1.75-to-2.05 V sequence on both that placement and a
[larger fixed-order interface](candidates/gain3_hold10_sample15_dummy15_if50_pullup4_input200_fixed_clockp2/pnr.md),
with zero invalid decisions. The larger interface's complete five-input
TT/SS/FF per-bit-CLEAR matrices now pass all 15 scalar conversion checks,
although the final slow/hot acquisition has only 0.094 mV margin. The
smaller placement also passes all 15 scalar checks with 0.100 mV final
slow/hot acquisition margin. A [fixed-order 1.55× sampling midpoint](candidates/gain3_hold10_sample155_dummy155_if50_pullup4_input200_fixed_clockp2/pnr.md)
has passed physical DRC/LVS and extraction. Its complete TT/SS/FF matrices pass
all **15 / 15** nominal scalar conversions. The worst SS acquisition is
19.071 mV, with 0.929 mV margin; the worst FF hold variation is 19.419 mV,
with 0.581 mV margin.
Its separately bound IHP passive extremes both fail the 2.05 V stepped case:
best/best has one invalid bit-7 read, and worst/worst acquisition is 22.355 mV.
MIM worst alone gives 22.350 mV, locating most of the added tracking lag in
the capacitor corner. A stronger 1.75× sampling / 1.90× dummy variant reduces
combined-worst acquisition to 19.329 mV but has two invalid bit-7 reads and
20.668 mV FF hold variation. A 0.5 fF latch Miller variant passes the physical
decks and TT scalar conversion but still reaches 3.633 V HV |Vgs|.
The 1.6× switch variant has passed all 15 five-input TT/SS/FF scalar
conversions with 1.679 mV final SS acquisition margin but only 0.313 mV
FF 1.75 V hold margin. Its HV terminal stress still fails the 3.3 V screen.
The 2.5 ps to 1.25 ps startup waveform comparison exceeds its peak-difference
limit; a finer 0.625 ps to 0.3125 ps startup-only check passes, but no full
conversion waveform convergence follows from it. HV gate stress exceeds the 3.3 V
screen. The slice is not qualified for replication. Measured sizing and
routing trials are in [experiments.md](experiments.md).

Current **placed IHP one-slice** scalar result for the 1.55× fixed-order
midpoint with per-bit CLEAR, from its verified 15-conversion TT/SS/FF matrix:

```text
Check                         Requirement                 Current value                 Status
----------------------------  --------------------------  ----------------------------  ------
IHP full DRC                  0 violations                0 / 868 categories            PASS
Hierarchical LVS              all devices/nets match      145 circuits / 226 devices    PASS
Connected PVT conversions     15 / 15 scalar passes       15 / 15                       PASS
Worst acquisition             <=20 mV                    19.071 mV, SS / 2.05 V        PASS
Worst hold variation          <=20 mV                    19.419 mV, FF / 1.75 V        PASS
Worst code error              <=2 counts                 2 counts                     PASS
Invalid decisions             0                           0 across 120 bit reads        PASS
Minimum decision setup        >=25 ps                    150 ps in full matrix         PASS
Minimum IO / core supplies    >=3.00 / >=1.08 V          3.244 / 1.170 V             PASS
Maximum HV gate stress        <=3.3 V screen             >=3.635 V in targeted run    FAIL
Passive-corner 2.05 V step    Same scalar limits          best: invalid; worst: 22.355 mV FAIL
Waveform convergence          <=20 mV peak, <=1 mV RMS  not established on this GDS  OPEN
Intrinsic noise/mismatch      product limit + PVT model  not established              OPEN
Full-cycle power/current      product budget             measured, budget absent      OPEN
Placed CLEAR driver/route     extracted timing and load  external fixture only        OPEN
```

The nominal final acquisition margin is 0.929 mV. Separate IHP passive
extremes fail on this same GDS; nominal scalar PASS does not qualify it for
replication. The full per-conversion table and hash-verified receipts are in
the [midpoint physical report](candidates/gain3_hold10_sample155_dummy155_if50_pullup4_input200_fixed_clockp2/pnr.md).

An initial range test exposed a timing requirement: allocating only 1 ns to DAC
settling before comparator evaluation produces a five-count error at 0.8875 V.
The rated schedule now allocates 2 ns to settling and 2 ns to evaluation within
the same 4 ns period. The inadequate-settling schedule remains a negative test;
the two-count limit is unchanged. Historical PHY passes do not qualify this new
topology/export path. Full process and layout qualification remain separate.

## Full verification table and historical evidence

The following is the complete 59-row table previously reported for the PHY.
Its historical results are retained to avoid losing requirements. **They do not
become passes for the new library.** The final column describes where each test
belongs now. “Integration” means the generic single-slice library cannot prove
that property without the named connected circuit, physical evidence or
specified budget. Missing generic tests remain explicitly open.

```text
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| #   | Category / test         | Requirement                 | Historical PHY result /       | Current library value / remaining   |
|     |                         |                             | status                        | work                                |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 1   | Scope — Qualified       | One electrical slice,       | Doubled sampler/dummy         | New independent slice; historical   |
|     | circuit                 | profile 2                   | switches; 1 pF storage; one   | receipts apply only to the old PHY. |
|     |                         |                             | launch per analog run;        |                                     |
|     |                         |                             | DEFINED                       |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 2   | Coverage — Process /    | TT, SS, FF, SF, FS;         | 30 cases at gains 4 and 8;    | Generic sensitivity suite; actual   |
|     | temperature             | -40/27/125 C                | all pass; PASS                | PSP corner matrix remains           |
|     |                         |                             |                               | integration work.                   |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 3   | Coverage — Gain         | Gains 4 through 8           | Gains 5/6/7 tested at TT/27   | Integration: external PGA, outside  |
|     | settings                |                             | C; endpoints covered over     | this primitive.                     |
|     |                         |                             | process/temperature; PASS —   |                                     |
|     |                         |                             | SELECTED MATRIX               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 4   | Coverage — Input        | Five PAM-5 pattern offsets  | All five at gain 8, TT/27 C,  | Library range/conversion stimuli;   |
|     | patterns                |                             | SS/125 C, FF/-40 C; PASS —    | PHY PAM patterns remain             |
|     |                         |                             | SELECTED MATRIX               | integration.                        |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 5   | Coverage — Supply sweep | Paired I/O/core sources:    | Low/high pairs at TT/SS/FF    | 18/18 supply-pair conversions pass; |
|     |                         | 3.1/1.1, 3.3/1.2, 3.35/1.32 | stress points; not every      | source settings 3.0/3.3/3.35 V x    |
|     |                         | V                           | independent supply            | 1.08/1.2/1.35 V.                    |
|     |                         |                             | combination; PASS — SELECTED  |                                     |
|     |                         |                             | MATRIX                        |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 6   | Voltage — Acquisition   | <=20 mV at approximately 1  | Worst 14.852 mV; PASS         | Rated records: maximum 0.099590 mV; |
|     | error                   | ns after capture            |                               | PASS against 20 mV.                 |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 7   | Voltage — Hold          | Peak-to-peak held voltage   | Worst 15.922 mV; PASS         | Rated records: maximum 5.980395 mV; |
|     | variation               | <=20 mV over the checked    |                               | PASS against 20 mV.                 |
|     |                         | hold window                 |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 8   | Voltage — Absolute      | No separate limit enforced  | Worst 20.748 mV; includes     | Rated records: maximum 5.502289 mV; |
|     | held-target error       |                             | acquisition offset plus       | MEASURED, no separate gate.         |
|     |                         |                             | subsequent disturbance;       |                                     |
|     |                         |                             | MEASURED — NOT GATED          |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 9   | Voltage — Capture       | 0.85 <= capture input       | Observed 1.1866 to 1.7164 V;  | 33 input stimuli span 0.85-2.05 V;  |
|     | headroom                | <=2.05 V                    | PASS                          | 33/33 conversion passes. Separate   |
|     |                         |                             |                               | headroom acceptance remains open.   |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 10  | Voltage — Held-node     | 0.85 <= held voltage <=2.05 | Observed 1.1721 to 1.7352 V;  | Recorded held behavior; separate    |
|     | headroom                | V                           | PASS                          | endpoint acceptance remains open.   |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 11  | Voltage — Gain-output   | 0 <= output <=              | 51/51 process traces pass;    | Integration: PGA outside slice.     |
|     | rail containment        | instantaneous VDD           | PASS                          |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 12  | Voltage — Delivered I/O | 3.00 to 3.35 V throughout   | 51/51 process traces pass;    | 3.0/3.3/3.35 V source settings      |
|     | supply                  | observation                 | source settings listed        | tested; 18/18 conversions pass.     |
|     |                         |                             | separately; PASS              | Delivered-rail containment not      |
|     |                         |                             |                               | separately qualified.               |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 13  | Voltage — Delivered     | 1.08 to 1.35 V throughout   | 51/51 process traces pass;    | 1.08/1.2/1.35 V source settings     |
|     | core supply             | observation                 | PASS                          | tested; 18/18 conversions pass.     |
|     |                         |                             |                               | Delivered-rail containment not      |
|     |                         |                             |                               | separately qualified.               |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 14  | Voltage — Precharge     | Track >=90% of VDD;         | 51/51 pass after the          | 2/5/20 ns library warmups: 6/6      |
|     |                         | held/input difference <=20  | specified 16 us warmup; PASS  | conversions pass; this is not the   |
|     |                         | mV                          |                               | historical PHY precharge gate.      |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 15  | Voltage — Input         | Differential sense versus   | 51/51 process cases pass;     | Integration: MDI front end outside  |
|     | delivery                | 0.02125 x line              | PASS                          | slice.                              |
|     |                         | differential: <=2 mV error  |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 16  | Voltage — Dynamic       | Gain-output excursion >=30  | 51/51 process cases pass;     | Continuous 1.5625 MHz sine, 1.45 V  |
|     | excitation              | mV                          | PASS                          | center, 0.45 V peak; 64/64          |
|     |                         |                             |                               | conversions pass. PGA outside       |
|     |                         |                             |                               | slice.                              |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 17  | Accuracy — Maximum code | abs(actual - floor(capture_ | Worst 2 counts; no conversion | Ordinary rated records: maximum 1   |
|     | error                   | voltage*256/VDD)) <=2       | exceeds the limit; PASS       | count; 186/186 meet the <=2-count   |
|     |                         | counts                      |                               | limit. Transition probes also pass. |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 18  | Accuracy — Code-error   | Characterization; not BER   | 0 counts: 32; 1 count: 17; 2  | Ordinary rated records: 158 exact,  |
|     | distribution            | or a statistical error-rate | counts: 2; >2: 0; total 51;   | 28 one-count, 0 two-count, 0 >2;    |
|     |                         | estimate                    | MEASURED                      | total 186. Not BER.                 |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 19  | Accuracy —              | Exact configured            | 51/51 process cases pass;     | Integration: digital normalizer     |
|     | Normalization           | offset/gain arithmetic and  | PASS                          | outside slice.                      |
|     |                         | rounding                    |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 20  | Accuracy — Clipping     | No clipped normalized       | 0 clipped results in 51       | Integration: normalized-output      |
|     |                         | output                      | process conversions; PASS     | clipping outside slice.             |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 21  | Timing — Controller     | Fixed 4 ns period           | 250 MHz controller clock;     | Rated 4 ns bit clock passes. 2/6 ns |
|     | clock                   |                             | this is not a demonstrated    | sampled cases also pass; 1 ns and   |
|     |                         |                             | per-slice sample rate; PASS — | 0.2 ns cases fail as expected.      |
|     |                         |                             | FIXED TIMING                  |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 22  | Timing — Tracking       | Commanded 8 ns              | Acquisition contract passes   | Connected nominal track 8 ns;       |
|     | interval                |                             | at this one interval; minimum | sampler sweep 0.1/0.5/1/2/4/8 ns    |
|     |                         |                             | required duration unknown;    | over 25/50/500/5000 ohm.            |
|     |                         |                             | PASS — FIXED TIMING           |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 23  | Timing — Checked hold   | From approximately +1 ns to | Hold/headroom checks pass;    | Connected conversion window;        |
|     | window                  | +36 ns after capture        | maximum safe hold time not    | independent long-duration leakage   |
|     |                         |                             | established; PASS — FIXED     | test.                               |
|     |                         |                             | TIMING                        |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 24  | Timing — Comparator     | Exactly eight decisions;    | 408 decisions across 51       | Ordinary rated records: 1,488       |
|     | delivery                | valid low <0.36 V or high   | process conversions; PASS     | decisions; 0 invalid. Transition    |
|     |                         | >0.84 V                     |                               | probes: 9,024 valid decisions.      |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 25  | Timing — Comparator     | Same valid logic state      | All checked decision windows  | Rated records pass all +/-25 ps     |
|     | stability               | throughout +/-25 ps of each | pass; PASS                    | decision-stability windows.         |
|     |                         | sampling edge               |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 26  | Timing — Comparator     | No separate timing-budget   | Worst 216.217 ps at SS/125 C, | Ordinary rated records: minimum     |
|     | settling margin         | threshold beyond decision   | 3.1/1.1 V; not full register  | 1285.000000 ps; no extra            |
|     |                         | stability                   | setup/hold slack; MEASURED —  | timing-budget gate or routed        |
|     |                         |                             | NOT FULL SIGNOFF              | signoff.                            |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 27  | Timing — Raw ADC result | 13 controller clocks from   | 52 ns from launch; 40 ns from | Library schedule defined by track + |
|     | latency                 | launch                      | capture command; PASS         | eight bit periods; PHY latency is   |
|     |                         |                             |                               | integration.                        |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 28  | Timing —                | 21 controller clocks from   | 84 ns; PASS                   | Integration: normalizer outside     |
|     | Normalized-result       | launch                      |                               | slice.                              |
|     | latency                 |                             |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 29  | Timing — Clock sequence | Complete clock sequence,    | 51/51 process cases pass;     | Integration: bank tags/controller   |
|     | / slice identity /      | correct result tags, no     | PASS                          | outside primitive.                  |
|     | controller fault        | fault                       |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 30  | Digital timing — Mapped | 4 ns clock, slow 1.08 V/125 | 0.92 ns setup slack; before   | Integration: requires mapped        |
|     | controller setup        | C cells, declared loads     | placement, ideal clock; PASS  | controller and its timing           |
|     |                         |                             | — CELL-ONLY                   | environment.                        |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 31  | Tool validation —       | Independent functional      | 18 conversions and 9          | New library export identities       |
|     | Generic C++ / Verilog-A | contracts plus paired       | comparisons pass; PASS        | checked; independent new VA         |
|     |                         | comparisons                 |                               | simulation remains open.            |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 32  | Tool validation —       | Independent contracts; <=5  | TT/SS/FF gain 8 pass; maximum | New explicit process export;        |
|     | Native PSP / ngspice    | mV compared analog metrics  | code difference 0; largest    | independent new native/reference    |
|     | PSP                     | and <=1 count difference    | metric difference 0.243 mV;   | comparison remains open.            |
|     |                         |                             | PASS                          |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 33  | Tool validation —       | <=5 mV target difference;   | Gain 4: 61.534 mV / 5 counts; | Historical mismatch retained; no    |
|     | Generic / PSP           | <=1 count difference        | Gain 8: 126.853 mV / 10       | equivalence claim for generic       |
|     | approximation audit     |                             | counts; use actual PSP for    | equations.                          |
|     |                         |                             | process prediction; FAIL —    |                                     |
|     |                         |                             | DIFFERENT DEVICE EQUATIONS    |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 34  | Tool / controller       | Controller                  | 27/27 distinct suites pass in | Current campaign: 38/38 ADC suites  |
|     | validation — Native,    | reset/flush/calibration     | their latest run; this does   | pass, none omitted; exhaustive      |
|     | RTL, mapped and         | plus binding,               | not prove repeated analog     | transitions and noise rerun.        |
|     | negative-control        | stale-evidence, numerical   | conversions; PASS             | Separate Analog++ transient checks: |
|     | regression suites       | and decision-count guards   |                               | 10/10 pass. Hashed evidence linked  |
|     |                         |                             |                               | above.                              |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 35  | Tool validation —       | Native/refined/reference DC | 48 one-/three-/six-finger     | Historical backend evidence; does   |
|     | Process-device and      | and transient checks        | device cases plus 8 inverter  | not qualify new full slice.         |
|     | inverter checks         |                             | cases; PASS                   |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 36  | Tool validation —       | Independent analytic        | Late tiny steps, femtosecond  | Hold-leakage RC error 30.347200 uV  |
|     | Numerical corner cases  | RC/charge solutions and     | edges, cancelling charges,    | (<50 uV). Independent Johnson-noise |
|     |                         | exact source breakpoints    | rollback and idle-driver      | RC RMS error 3.063722%; PASS        |
|     |                         |                             | events covered; PASS          | against 12%. New local-edge/startup |
|     |                         |                             |                               | refinement agrees with an           |
|     |                         |                             |                               | independent 10 fs RC within 70.3202 |
|     |                         |                             |                               | uV; continuation and source knots   |
|     |                         |                             |                               | preserved.                          |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 37  | Tool validation — Full  | Qualify the final ADC model | Final ADC comparisons qualify | Nominal full 0-61 ns, all 61 nodes: |
|     | ADC timestep / all-node | over multiple timesteps and | selected metrics and codes;   | 15.915566 mV both-grid peak (<20);  |
|     | waveform convergence    | full waveforms              | full-waveform convergence is  | RMS 0.097986 mV (<1). Fine BDF2 +   |
|     |                         |                             | not established; PARTIAL      | local fs refinement PASS. Legacy 10 |
|     |                         |                             |                               | ps fast waveforms remain            |
|     |                         |                             |                               | unconverged.                        |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 38  | Voltage — Full          | Cover the full declared     | No qualifying result for      | 33/33 pass; 28 exact, 5 one-count   |
|     | specified               | operating range, not only   | optimized profile 2; NOT      | errors over 0.85-2.05 V. Separate   |
|     | input/common-mode range | the observed 1.1866–1.7164  | TESTED                        | transition search added.            |
|     | sweep                   | V capture values            |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 39  | Voltage / bandwidth —   | Establish analog input      | No qualifying result for      | Frequency 0.3125-9.0625 MHz;        |
|     | Input frequency, slew   | bandwidth and required      | optimized profile 2; NOT      | amplitude 0.05/0.25/0.55 V; source  |
|     | and source-impedance    | external drive              | TESTED                        | 25/50/500/5000 ohm. 704             |
|     | sweeps                  |                             |                               | conversions; stressed failures      |
|     |                         |                             |                               | retained. Spectral budgets/PVT      |
|     |                         |                             |                               | open.                               |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 40  | Voltage / loading —     | Bound disturbances          | No qualifying result for      | Connected DC source-R sweep: peak   |
|     | Sampling kickback and   | delivered to the preceding  | optimized profile 2; NOT      | upstream excursion 46.207389 mV;    |
|     | load robustness         | stage and sensitivity to    | TESTED                        | returned charge measured. Explicit  |
|     |                         | loading                     |                               | generic coupling only; intrinsic    |
|     |                         |                             |                               | IHP charge and kickback budget      |
|     |                         |                             |                               | open.                               |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 41  | Voltage — Supply ripple | Specify disturbance         | No qualifying result for      | IO/core ripple sweeps at            |
|     | / droop / PSRR          | amplitude, frequency and    | optimized profile 2; NOT      | 0.625/3.125/8.125 MHz; IO amplitude |
|     |                         | allowable degradation       | TESTED                        | 5/25/50 mV. Hold/code PSRR          |
|     |                         |                             |                               | measured, unresolved responses      |
|     |                         |                             |                               | labelled; selected 10/5 ps checks   |
|     |                         |                             |                               | pass. Product budget/open physical  |
|     |                         |                             |                               | PSRR remain.                        |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 42  | Current / power —       | Budgets not yet defined and | No qualifying result for      | Complete startup and                |
|     | Average, peak and       | measured for this optimized | optimized profile 2; NOT      | track/capture/convert/recovery rail |
|     | startup current; energy | slice                       | TESTED                        | energy/current measured. BDF2 power |
|     | per conversion          |                             |                               | references pass <=1% energy/current |
|     |                         |                             |                               | and <=5% peak refinement. Product   |
|     |                         |                             |                               | budgets unspecified.                |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 43  | Current / power — Hold  | Separate leakage            | No qualifying result for      | 10 nS synthetic leakage; 1/10/100   |
|     | leakage versus duration | characterization from       | optimized profile 2; NOT      | us decay agrees with analytic RC    |
|     | and temperature         | fixed-window hold variation | TESTED                        | within 30.347200 uV (<50 uV gate).  |
|     |                         |                             |                               | Temperature calibration OPEN.       |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 44  | Accuracy — Offset /     | Specify limits across the   | No qualifying result for      | Nominal transitions measured;       |
|     | gain error; calibration | operating input range and   | optimized profile 2; NOT      | endpoint-fit slope 12.816820        |
|     | residual                | PVT                         | TESTED                        | mV/code, extrapolated offset        |
|     |                         |                             |                               | 8.460437 mV. Calibration/offset     |
|     |                         |                             |                               | budgets OPEN.                       |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 45  | Accuracy — DNL / INL /  | Sweep code transitions      | No qualifying result for      | One-slice nominal ADC: 94           |
|     | monotonicity / missing  | across the specified        | optimized profile 2; NOT      | transitions, 93 complete bins; zero |
|     | codes                   | operating range             | TESTED                        | missing codes; up/down repeats      |
|     |                         |                             |                               | PASS. Max endpoint DNL 0.191415,    |
|     |                         |                             |                               | INL 0.129013 LSB. Magnitude budgets |
|     |                         |                             |                               | and PVT/layout OPEN. Exhaustive     |
|     |                         |                             |                               | transition suite rerun in current   |
|     |                         |                             |                               | full campaign.                      |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 46  | Dynamic accuracy — SNR  | Define bandwidth, sample    | No qualifying result for      | Coherent 64-sample                  |
|     | / SNDR / ENOB / SFDR /  | rate, stimulus and required | optimized profile 2; NOT      | frequency/amplitude/source-R sweeps |
|     | THD                     | limits                      | TESTED                        | now report SNR/SNDR/ENOB/SFDR/THD   |
|     |                         |                             |                               | per point. ENOB is at stimulus      |
|     |                         |                             |                               | amplitude. Product limits and       |
|     |                         |                             |                               | actual IHP/PVT/layout remain open.  |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 47  | Noise / statistics —    | Define noise, yield and     | No qualifying result for      | Current noise suites rerun:         |
|     | Thermal/flicker/kT-C    | confidence targets          | optimized profile 2; NOT      | 57.324896 uV blind ADC RMS vs       |
|     | noise and mismatch      |                             | TESTED                        | 64.374047 uV ideal kT/C. PSD        |
|     | Monte Carlo             |                             |                               | fixture remains synthetic;          |
|     |                         |                             |                               | intrinsic IHP noise and product     |
|     |                         |                             |                               | budget open.                        |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 48  | Timing — Minimum        | Find the shortest passing   | No qualifying result for      | Connected track sweep 0.25-8 ns,    |
|     | acquisition time sweep  | track interval across       | optimized profile 2; NOT      | 50/500/5000 ohm; nominal plus       |
|     |                         | PVT/input conditions        | TESTED                        | generic slow/hot and fast/cold at   |
|     |                         |                             |                               | 50 ohm. Four alternating inputs per |
|     |                         |                             |                               | point. Passing/failing discrete     |
|     |                         |                             |                               | bounds above; actual foundry PVT    |
|     |                         |                             |                               | open.                               |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 49  | Timing — DAC settling   | Establish individual timing | No qualifying result for      | Independent 4x5 settle/evaluate     |
|     | and comparator          | budgets, not just           | optimized profile 2; NOT      | grid; 80 conversions, 56 failed     |
|     | evaluation-time sweeps  | end-to-end code acceptance  | TESTED                        | stress conversions. All nominal 2+2 |
|     |                         |                             |                               | ns cases pass. Passing combinations |
|     |                         |                             |                               | listed above.                       |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 50  | Timing — Near-threshold | Measure resolution-time     | No qualifying result for      | Eight nonzero overdrives            |
|     | comparator overdrive /  | tails and decision          | optimized profile 2; NOT      | +/-0.1/1/10/100 mV resolve in       |
|     | metastability           | reliability                 | TESTED                        | 40-100 ps; zero remains unresolved. |
|     |                         |                             |                               | Stochastic tails OPEN.              |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 51  | Timing — Clock-period   | Find operating margin and   | No qualifying result for      | Independent connected               |
|     | sweep / maximum         | failing boundary            | optimized profile 2; NOT      | settle/evaluate grid now exposes    |
|     | reliable speed          |                             | TESTED                        | passing/failing combinations and    |
|     |                         |                             |                               | total bit period. Rated 4 ns        |
|     |                         |                             |                               | retained; discrete nominal results  |
|     |                         |                             |                               | do not prove maximum speed across   |
|     |                         |                             |                               | PVT/layout.                         |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 52  | Timing — Clock jitter / | Define and test realistic   | No qualifying result for      | Gaussian aperture/evaluate jitter   |
|     | skew / duty-cycle /     | clock-delivery budgets      | optimized profile 2; NOT      | 0/10/50/100 ps, two seeds, finite   |
|     | slew sweeps             |                             | TESTED                        | 50 ps edges and 100 fF loads.       |
|     |                         |                             |                               | Delivered jitter measured with <=5  |
|     |                         |                             |                               | ps residual. Routed clock delivery  |
|     |                         |                             |                               | and rare-event tails open.          |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 53  | Timing — Repeated       | Verify reset, recovery and  | No qualifying result for      | 1484 additional connected           |
|     | analog conversions on   | reacquisition;              | optimized profile 2; NOT      | characterization conversions; each  |
|     | the same slice /        | digital-controller stress   | TESTED                        | point retains one slice across      |
|     | sustained throughput    | tests alone are             |                               | samples. Complete cycle measured.   |
|     |                         | insufficient                |                               | Foundry/PVT long-run throughput     |
|     |                         |                             |                               | remains open.                       |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 54  | Startup — Minimum       | Existing cold start uses a  | No qualifying result for      | 2/5/20 ns warmup: 6/6 pass; two     |
|     | startup time / brownout | fixed 16 us warmup          | optimized profile 2; NOT      | power cycles: 3/3 conversions pass. |
|     | / power-cycle recovery  |                             | TESTED                        | Compact-model powered-off stress    |
|     |                         |                             |                               | OPEN. Startup rail energy/peaks are |
|     |                         |                             |                               | now measured separately from full   |
|     |                         |                             |                               | cycles.                             |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 55  | Process variation —     | Current campaign uses       | No qualifying result for      | +/-10% passive scale: 6/6 pass.     |
|     | Passive process corners | nominal passive process;    | optimized profile 2; NOT      | Eight uniform +/-1% R/2R draws:     |
|     | and mismatch            | MIM temperature effects     | TESTED                        | 16/16 pass. Foundry statistics      |
|     |                         | remain included             |                               | OPEN.                               |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 56  | Physical — Profile-2    | Older profile-1 layout      | No qualifying result for      | Integration: actual profile-2       |
|     | layout DRC / LVS /      | receipts do not qualify the | optimized profile 2; NOT      | layout and extraction required;     |
|     | extracted device and    | new geometry                | TESTED                        | native RC bridge implemented.       |
|     | wire parasitics         |                             |                               | Sampled-noise factory rejects kT/C  |
|     |                         |                             |                               | on external routes without an       |
|     |                         |                             |                               | explicit effective C.               |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 57  | Physical timing —       | Include actual routing,     | No qualifying result for      | Integration: actual clock           |
|     | Routed clock/data setup | clock tree, skew and loads  | optimized profile 2; NOT      | tree/routes and timing libraries    |
|     | and hold / analog       |                             | TESTED                        | required.                           |
|     | control delivery        |                             |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 58  | Physical reliability —  | Define budgets and qualify  | No qualifying result for      | R/C rail/coupling hooks present;    |
|     | IR drop /               | the physical implementation | optimized profile 2; NOT      | physical geometry, reliability      |
|     | electromigration /      |                             | TESTED                        | limits and checks required.         |
|     | device stress /         |                             |                               |                                     |
|     | crosstalk               |                             |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
| 59  | Replication —           | Qualify after single-slice  | No qualifying result for      | Integration after one-slice         |
|     | Interleaved-slice       | physical closure            | optimized profile 2; NOT      | closure; no replication             |
|     | mismatch / crosstalk /  |                             | TESTED                        | qualification claimed.              |
|     | aggregate throughput    |                             |                               |                                     |
+-----+-------------------------+-----------------------------+-------------------------------+-------------------------------------+
```

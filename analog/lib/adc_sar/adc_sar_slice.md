# Reusable electrical SAR ADC slice

`adc_sar_slice.h` implements a process-independent, single-ended SAR electrical
slice using Analog++ MOS, resistor and capacitor equations. It contains no PHY,
PGA, MDI termination, interleave scheduler or digital normalizer. An external
sequencer supplies complemented trial bits and reads the retained core-domain
comparison. Resolution is a template parameter (2–16 bits); qualification below
uses eight bits. A resolution change requires new accuracy and timing evidence.

The technology-specific implementation, actual GDS/DEF placement, extracted
Analog++ process model and measured physical checks are maintained separately
in [IHPSG13G2/pnr.md](IHPSG13G2/pnr.md). Generic-model passes below do not qualify
that physical implementation.

## Current generic-library status (2026-11-15)

The current generic source passed [all 44 registered library suites](../../../build/adc-sar-lib/campaign-k9lj8e_b/results.md)
in `build/adc-sar-lib/campaign-k9lj8e_b`: the complete electrical transition
search, noise calibration, characterization sweeps, full-cycle power and
all-node waveform checks are included. The campaign is source-hash locked and
has no omitted suites. Its generic model result does not establish a product
budget or physical process/layout qualification. Older campaign tables below
retain their own source and test dates.

This source-only commit excludes the shared Analog++ solver edits used by that
campaign. Its 44-suite result describes the recorded workspace snapshot and
cannot be reproduced from this commit alone.

```text
Item                         Current result                              Remaining requirement
---------------------------  ------------------------------------------  ---------------------------------------------
Generic regression           44/44 pass, current source                 Product limits and physical qualification
Model element identities     137 map; two named slices remain distinct   Preserve names in technology adapters
Generic resistor model       Ideal two-terminal equation                Bind technology parasitics in an adapter
Noise and yield              Synthetic sampled-noise fixture only        Supply calibrated data and product limits
Product power and linearity  Measured-evidence gate added                Define power, DNL/INL and spectral budgets
```

The generic resistor has only two conduction nodes. Substrate contacts and
their parasitics belong to a technology adapter; the generic ideal resistor
does not claim to reproduce them. Unspecified product limits remain open rather
than becoming passes because the test harness measured a number.

Process-specific candidate results and placement failures are recorded in the
[IHPSG13G2 qualification history](IHPSG13G2/qualification_history.md).

### Generic linearity, power and spectral acceptance

`qualification.py` now reads a passing source-locked library campaign and
applies product limits to **measured** evidence. Its command-line path verifies
that the campaign source hashes match the current tree before writing a result.
It requires complete ascending and descending electrical transitions, complete
code bins, refined full-cycle power, coherent frequency records, and passing
baseline/input-range hold
conversions. A failed or omitted required suite is rejected. DNL/INL use the
worst absolute endpoint of each search-bracket interval, including both
directions. Power is the maximum IO-plus-core rail energy divided by its full
sample period over every required characterization case; peak current is the
maximum individual rail peak across those cases. The dedicated power suite
provides a two-resolution BDF2 reference, while other stress-case extrema
have not all had individual timestep refinement. Input-driver and clock-driver
energy is outside the rail-power number. ENOB is at each stated stimulus amplitude.

The values below are extracted from the **current passing electrical campaign**
`build/adc-sar-lib/campaign-k9lj8e_b/checks.json`. The source-matched gate
result is `build/adc-sar-lib/campaign-k9lj8e_b/qualification.json`. Its product
budgets are unspecified, so the gate returns `INCOMPLETE`. `PASS` in
the evidence column refers to the measurement and numerical regression, not
product acceptance.

```text
Metric                     Required evidence / acceptance limit       Current measured value        Evidence   Product
-------------------------  -----------------------------------------  ----------------------------  ---------  -----------
Endpoint DNL, abs          93 complete bins; product max unset         0.203549 LSB worst bound      PASS       UNSPECIFIED
Endpoint INL, abs          Both directions; product max unset          0.141260 LSB worst bound      PASS       UNSPECIFIED
Full-cycle rail power      All required cycles; product max unset      12.727224 mW, 1102 cycles   PASS       UNSPECIFIED
Peak rail current          All required cycles; product max unset      136.613969 mA              PASS       UNSPECIFIED
SNDR                       Coherent spectra; product min unset         39.252861 dB min, 4 records  PASS       UNSPECIFIED
ENOB at stimulus           Coherent spectra; product min unset         6.228050 bits min            PASS       UNSPECIFIED
SFDR                       Coherent spectra; product min unset         49.113040 dB min             PASS       UNSPECIFIED
Absolute hold error        Baseline + input range; product max unset   5.123510 mV max, 38 runs     PASS       UNSPECIFIED
```

The maximum average rail power in this grid is at the required generic
fast/8 ns acquisition case; the largest instantaneous rail current is at the
required 50 ps RMS jitter case with seed 7. These are C++ model source-side
measurements. The former dedicated power-only figures (12.253069 mW and
20.332975 mA) omitted these other required cases and are not used for product
budget comparison.

`requirements` in `parameters.h` stores these eight budgets as template
parameters on `adc_sar_slice`. `qualification_limits.h` exports the bound
values as JSON; each default NaN becomes `null`. Use a product's actual limits
with the source-matched campaign:

```sh
python3 analog/lib/adc_sar/qualification.py \
  --campaign build/adc-sar-lib/campaign-k9lj8e_b/checks.json \
  --limits analog/lib/adc_sar/qualification_limits.example.json \
  --output build/adc-sar-lib/campaign-k9lj8e_b/qualification.json
```

Replace the `null` entries with approved budgets, or export a slice's bound
`requirements` using `write_qualification_limits<Slice>(stream)`. The gate
returns `PASS` only when every budget is specified and met, `FAIL` for any
breach, and `INCOMPLETE` while any budget remains unspecified. This is a
generic model acceptance check; physical process/layout qualification is
reported separately.

## Parameters and defaults

```cpp
using namespace analog::adc_sar;
constexpr auto extracted = [] {
    auto p = schematic_layout;
    p.input_r = 20;       // ohms, replace with an extraction result
    p.held_c = 10e-15;    // farads, WIRE capacitance only
    return p;
}();
using Slice = adc_sar_slice<8, ihp130_profile2, ihp130_passives,
                            nominal_corner, extracted>;
```

All numeric configuration groups are C++20 structural template arguments, not
hidden process constants. `Devices`, `Passives`, `Corner`, `Layout`, `Limits` and `Noise`
are independently replaceable. SI units apply; `Corner.temperature` uses Celsius and noise calibration uses kelvin.
`parameters.h` documents the individual fields. The transistor widths below are
**total widths**, not widths per finger. The main signal-path members and their
connections precede the latch, level interface and routing in both the member
list and `_assign()`.
The auxiliary `comparator_left_trim` parameter defaults to zero and adds a
placeable capacitance to the left regenerative node. The parameter regression
checks its exported capacitance difference.

```text
+-------------------------+----------------------------------------------+------------------------------------------------+
| Group                   | Default / meaning                            | How it reaches the model                       |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Main transmission gate  | N 15 µm / 6 fingers, P 30 µm / 4 fingers, L  | Generic beta 0.024/0.012 A/V²; geometry is     |
|                         | 0.45 µm                                      | exported explicitly                            |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Cancellation devices    | N 8 µm / 4 fingers, P 16.8 µm / 2 fingers    | Opposing dummy channels; intrinsic switching   |
|                         |                                              | charge requires compact models                 |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Complement driver       | N 40 µm / 4, P 80 µm / 8                     | Finite MOS driver and 20 fF MIM load           |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Hold / off coupling     | 1 pF / 1 fF                                  | Real charge storage; small coupling uses three |
|                         |                                              | MIM units in series                            |
+-------------------------+----------------------------------------------+------------------------------------------------+
| DAC                     | R/2R, R=1 kΩ, 3 fF per tap/driver            | Finite rail drivers, configurable per-leg R    |
|                         |                                              | and 2R mismatch                                |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Preamplifier            | W=10 µm / 2, L=0.45 µm; load 1 kΩ,           | Current steering, finite gain, loading and     |
|                         | degeneration 100 Ω, tail 300 Ω               | feedback charge                                |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Comparator              | Input/tail N50; feedback N20/P40; precharge  | Regenerative electrical latch with             |
|                         | P10, L=0.45 µm                               | internal/output charge                         |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Retention / interface   | Buffered NAND latch; HV input, 1.2 V core    | Comparator loading, state retention, clear and |
|                         | interface                                    | static interface current                       |
+-------------------------+----------------------------------------------+------------------------------------------------+
| MOS geometry            | W, L, fingers; optional source/drain area    | Explicit four-terminal process bindings;       |
|                         | and perimeter                                | partial extracted junction overrides are       |
|                         |                                              | rejected                                       |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Generic MOS sensitivity | Separate N/P beta scales, threshold shifts,  | Applied to the C++ MOS equations; **not**      |
|                         | mobility exponent and threshold TC           | named technology process corners               |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Temperature             | 27°C                                         | MIM TC and explicitly supplied MOS/resistor    |
|                         |                                              | sensitivity coefficients                       |
+-------------------------+----------------------------------------------+------------------------------------------------+
| MIM                     | 1.5 fF/µm² + 0.04 fF/µm perimeter; 0.055 Ω   | Geometry synthesized from target C; C and      |
|                         | access; TC1 3.6e−6/K, TC2 2e−9/K²            | access R enter native C++; process export      |
|                         |                                              | replaces both with one cap_cmim cell           |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Passive variation       | C/R scale, R TC, per-bit R/2R error,         | Changes the stamped electrical values; nominal |
|                         | termination error                            | R TC is zero because the old design uses ideal |
|                         |                                              | resistors                                      |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Hold leakage            | 0 S explicit shunt by default                | Optional finite conductance; default does not  |
|                         |                                              | invent a gigaohm stabilizing path              |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Routing                 | All additional wire R/C default to zero      | Input, held, DAC, control, bit, decision, rail |
|                         |                                              | and return paths have separate endpoints       |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Coupling                | Track/held, evaluate/held, DAC/held,         | Explicit two-terminal capacitors; aggressor is |
|                         | aggressor/held                               | externally driven                              |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Limits                  | Acquisition/hold 20 mV, code error 2; input  | Qualification limits, not idealized circuit    |
|                         | 0.85–2.05 V; clock 4 ns, track 8 ns          | behavior                                       |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Sampled noise (opt-in)  | Final Noise template argument;               | Ideal kT/C enabled in generated records; extra |
|                         | sample_noise() factory                       | PSD omitted by default. Circuit remains        |
|                         |                                              | deterministic until a bench applies the        |
|                         |                                              | record.                                        |
+-------------------------+----------------------------------------------+------------------------------------------------+
| Undefined budgets       | Absolute hold error, power, peak current,    | NaN means **no acceptance requirement          |
|                         | DNL/INL, SNDR/ENOB/SFDR, jitter, yield,      | specified**, never automatic PASS              |
|                         | input_noise_rms                              |                                                |
+-------------------------+----------------------------------------------+------------------------------------------------+
```

The supplied template defaults are one example profile, not process
qualification. Numeric fields and units are defined in `parameters.h`;
technology-specific provenance and model-card checks belong in the adapter
directory.

## Process and extracted-layout integration

`write_process_subcircuit()` in `process_export.h` exports an **isolated,
elaborated** slice. Every MOS has an exact element/body/geometry binding. Model
names are configurable. It exports fingers and complete extracted junction geometry without
inferring width from generic beta. Resistors and non-MIM explicit capacitors
remain circuit values. This export needs a deck with the selected MOS/passive
corners, temperature and model libraries. A concrete adapter must bind those
models and provide its own qualification report.

Supply the complete process model card/backend when replacing a process; do not
approximate its many intrinsic capacitance, body-effect, leakage, noise,
mismatch, self-heating, stress and RF parameters with one TT/SS/FF multiplier.
The generic channel law does not implement those compact-model effects.
Geometry affects the physical export; generic conduction uses explicit beta
and threshold. Change both deliberately when changing a generic device design.
Explicit circuit capacitors are additional to compact-model intrinsic charge;
wire extraction must exclude intrinsic pin/device capacitance to avoid counting
it twice.

For a particular floorplan, either use lumped `Layout` values or set
`Layout.external_routes=true` and provide `slice.annotation`. The callback gets
stable `name.driver` and `name.sink` electrical terminals for:

- `input`, `held`, `dac`, `track`, `evaluate`, `clear`, `decision`;
- `bit0` through `bitN-1`;
- `io`, `core`, `ground`.

The callback stamps its RC network once, during elaboration. Do not short the
endpoints before importing the network. The regression exercises the existing
Analog++ `spef_rc` parser on the input route and verifies that the driver/sink
remain distinct. External RC and lumped R/C cannot be enabled together. Each
unrouted net needs an explicit direct connection. A full extracted RC tree,
including coupling, can therefore enter the native C++ solver. Supply-source
impedance and decoupling outside the slice belong in its parent/bench.

The synthetic routed test below is a bridge regression. For physical qualification,
record the model-card/OSDI hashes, process/passive corners, temperature, geometry
and junction extraction, layout/SPEF hashes and units, pin map, intrinsic-versus-
wire capacitance ownership, source/load impedances, clock paths and stimulus
schedule. Preserve bodies/wells and supply-return paths. A technology adapter
must consume those same bindings; a separate chip hierarchy is not
automatically retargeted by this library.

## Running and interpreting tests

The directory is independently buildable, so library changes do not require
building the PHY or changing its existing receipts:

```sh
cmake -S analog/lib/adc_sar -B build/adc-sar-lib -DCMAKE_BUILD_TYPE=Release
cmake --build build/adc-sar-lib -j2
ctest --test-dir build/adc-sar-lib --output-on-failure -j2 \
  --output-junit results/ctest.xml
```

For a fresh campaign directory, complete CSV/JUnit report, spectral metrics,
all-node comparison, and source/binary/evidence hashes, run:

```sh
python3 analog/lib/adc_sar/tests/run.py --jobs 2
```

The runner writes `latest.json` in the selected build directory; the current
receipt uses `build/adc-sar-characterization/latest.json`. A campaign
fails if sources or any of its three native binaries change during execution. The library is also
registered in the normal `analog/CMakeLists.txt` build.

Tests use the native Analog++ solver. An optional Eigen sparse backend is used
when available; it is not a different circuit model. The legacy electrical testbench
commits finite-impedance control sources through clocked bench registers; the new
characterization fixture uses finite-slew PWL controls;
conversion decisions come from the actual core-interface voltage. It retains
capacitor and latch state between successive conversions. It is a library
sequencer, not an RTL-equivalence test of the PHY's 13-slot controller.

CSV files contain measured acquisition error, held peak-to-peak variation,
absolute held-target error, raw/ideal code and error count, eight decision
samples, decision stability/margin, and measured rail current/energy. Legacy current
and energy fields describe the **conversion observation window**. New
characterization power CSVs also record startup and complete sample cycles. Endpoint/supply limits and spectral/power
budgets must not be inferred from a passing unrelated check.

The following 22-suite table records the prior deterministic campaign.
The current 38-suite characterization campaign below reruns all suites, including
transitions and noise. The older selected noise receipt remains separately
labelled as historical evidence.

```text
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| Suite              | Requirement / check                     | Current result                                  | Status / time  |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| parameters         | Exact device inventories and export     | 8-bit: 59 MOS; 4-bit: 51 MOS; 5 MIM cells; all  | PASS 0.00 s    |
|                    | bindings; reject invalid configurations | guards pass                                     |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| acquisition        | At 25/50 ohm and 8 ns: error <20 mV;    | 24 drive/time points plus loaded case; nominal  | PASS 0.49 s    |
|                    | detect inadequate drive and added       | max 0.000000637 mV; negative controls detected  |                |
|                    | loading                                 |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| dac                | All 256 DAC codes monotonic; absolute   | 256/256 pass; max error 0.102531 LSB; max step  | PASS 3.60 s    |
|                    | error <1 LSB                            | DNL magnitude 0.198100 LSB                      |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| conversion         | Acquisition/hold <=20 mV; code error    | 5/5 pass; hold max 5.562568 mV; code error 0    | PASS 16.37 s   |
|                    | <=2; valid stable decisions             |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| routing            | Conversion contract with synthetic RC;  | 5/5 pass; hold max 5.588135 mV; code error 0;   | PASS 17.22 s   |
|                    | SPEF input endpoints remain distinct    | SPEF guard passes                               |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| comparator         | Correct resolution for abs(dV) >=1 mV;  | 8 nonzero points resolve correctly (40-100 ps); | PASS 0.58 s    |
|                    | exactly zero must stay unresolved       | zero unresolved                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| leakage            | 10 nS leakage: measured hold decay      | 1/10/100 us tested; max deviation 30.347200 uV  | PASS 0.35 s    |
|                    | within 50 uV of analytic RC             |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| timing             | 4/6 ns cases pass; 0.2 ns negative      | 1 ns: 0/2; 2 ns: 2/2; 4 ns: 2/2; 6 ns: 2/2; 0.2 | PASS 26.34 s   |
|                    | control must fail                       | ns: 0/1 (negative control)                      |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| voltage            | Conversion contract across independent  | 9 supply pairs: 18/18 pass; ripple: 2/2 pass;   | PASS 69.99 s   |
|                    | source voltages and specified ripple    | max code error 1                                |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| range              | 33 points over 0.85-2.05 V all meet the | 33/33 pass; 28 exact, 5 one-count errors        | PASS 105.26 s  |
|                    | conversion contract                     |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| convergence        | Equal codes; selected analog metrics    | Code difference 0; max metric delta 0.004784639 | PASS 11.24 s   |
|                    | differ by <1 mV                         | mV; all-node qualification OPEN                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| sensitivity        | Conversion contract under four generic  | 18/18 pass; hold max 5.980395 mV; code error 1  | PASS 60.86 s   |
|                    | MOS and two passive sensitivities       |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| dynamic            | 64 coherent sine conversions meet the   | 64/64 pass; max code error 1; SNDR 39.252861 dB | PASS 205.41 s  |
|                    | conversion contract                     | (no spectral gate)                              |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| settling           | 2 ns DAC settling passes; inadequate 1  | 1 ns: 3/4, error 5; 2 ns: 4/4, error 0; 3 ns:   | PASS 39.54 s   |
|                    | ns setting must be detected             | 4/4, error 0                                    |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| clock              | Conversion contract with 100            | 6/6 pass; max code error 0                      | PASS 22.87 s   |
|                    | fF/control, 50/500/1000 ohm, 50 ps      |                                                 |                |
|                    | skew, +/-25 ps perturbation             |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| startup            | Conversion contract after 2/5/20 ns     | Warmup: 6/6 pass; recovery: 3/3 pass; max code  | PASS 29.44 s   |
|                    | warmup and clear/power-cycle recovery   | error 0                                         |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| mismatch           | Both inputs pass for each of 8          | 8/8 draws, 16/16 conversions pass; 95% Wilson   | PASS 55.98 s   |
|                    | synthetic uniform +/-1% R/2R draws      | interval 0.675592-1; not calibrated yield       |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| metrics            | Recover known spectral amplitudes,      | 7 measurement-method tests pass; known          | PASS 0.05 s    |
|                    | transition errors and confidence        | transition gain/offset, DNL/INL and bracket     |                |
|                    | bounds; reject invalid data             | bounds recovered                                |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| adaptive_input     | Future PWL source must preserve RC      | 4 RC steps pass; max analytic error 0.162277 mV | PASS 0.09 s    |
|                    | state; error <0.5 mV; reject            |                                                 |                |
|                    | past/nonfinite edits                    |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| history            | Same interior voltage must return the   | 8/8 conversions pass; five visits to 0.8572 V   | PASS 25.73 s   |
|                    | same code after large input jumps       | all return code 66                              |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| transition_search  | Known transfer function bracketed both  | 3 algorithm tests pass                          | PASS 0.05 s    |
|                    | directions; detect missing codes and    |                                                 |                |
|                    | repeat failures                         |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
| transitions        | Complete electrical conversions;        | 94 boundaries each direction; 93 complete bins; | PASS 3614.91 s |
|                    | disjoint complete bins; repeated        | 1128 conversions; DNL/INL magnitudes MEASURED   |                |
|                    | centers; direction/timestep agreement   |                                                 |                |
+--------------------+-----------------------------------------+-------------------------------------------------+----------------+
```

`metrics.py` supplies coherent-record SNR/SNDR/ENOB/SFDR/THD, endpoint-fit
transition INL/DNL and Wilson confidence intervals. It rejects invalid records.
A sparse DC sweep cannot establish missing codes or DNL; use measured transition
boundaries. A deterministic corner sweep cannot establish statistical yield.

## Historical characterization and waveform qualification

[Full 38-suite results](../../../build/adc-sar-characterization/campaign-z9ucd6w6/results.md) and
[hashed sources, binaries and evidence](../../../build/adc-sar-characterization/campaign-z9ucd6w6/checks.json).
The separate [Analog++ transient receipt](../../../build/adc-sar-characterization/campaign-z9ucd6w6/analog_transient_regressions.json) records 10/10 passing solver/tool suites.

This campaign reruns every suite registered at its source date, including the exhaustive
transition scan and sampled-noise calibration. The earlier campaign tables below
are historical source snapshots. The current-source gate and remaining
qualification limits are summarized above; this section retains its original
campaign's detailed measurements.
All 97 legacy top-level CSV outputs are byte-identical to the prior deterministic
campaign; local edge refinement remains opt-in.

```sh
python3 analog/lib/adc_sar/tests/run.py \
  --build build/adc-sar-characterization --jobs 6
cmake --build build/analog-rc --target check-analog-transient -j 3
```

Each characterization point keeps one connected slice alive across its samples.
The nominal sample spacing is 50 ns (20 MS/s); the input center is 1.45 V for sine
records. Temperature/device sensitivities are explicitly generic, not process corners.
The grids below are finite characterization grids, not an exhaustive Cartesian
product of every input, timing, temperature, impedance and supply condition.

```text
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| Sweep / check            | Coverage and requirement                   | Archived result                            | Status       |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| frequency                | 0.3125/1.5625/4.6875/9.0625 MHz; 0.45 V    | 4 points; 256 conversions; 0 failed        | PASS / GRID  |
|                          | peak; 50 ohm; 64 samples per point         | conversions retained. Required points      |              |
|                          |                                            | pass.                                      |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| amplitude                | 0.05/0.25/0.55 V peak; 1.5625 MHz; 50 ohm; | 3 points; 192 conversions; 0 failed        | PASS / GRID  |
|                          | 64 samples per point                       | conversions retained. Required points      |              |
|                          |                                            | pass.                                      |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| impedance                | 25/50/500/5000 ohm; 1.5625 MHz; 0.45 V     | 4 points; 256 conversions; 52 failed       | PASS / GRID  |
|                          | peak; 64 samples per point                 | conversions retained. Required points      |              |
|                          |                                            | pass.                                      |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| kickback                 | 25/50/500/5000 ohm; DC inputs 0.9/1.45/2.0 | 4 points; 12 conversions; 2 failed         | MEASURED     |
|                          | V; opening edge through hold/recovery      | conversions retained. Required points      |              |
|                          |                                            | pass. Max upstream excursion 46.207389 mV. |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| psrr                     | IO/core ripple +/-25 mV at                 | 10 points; 320 conversions; 0 failed       | MEASURED     |
|                          | 0.625/3.125/8.125 MHz; IO +/-5/50 mV at    | conversions retained. Required points      |              |
|                          | 3.125 MHz; selected 5 ps replay            | pass.                                      |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| acquisition              | Track 0.25/0.5/1/2/4/8 ns; 50/500/5000 ohm | 30 points; 120 conversions; 39 failed      | PASS / GRID  |
|                          | nominal; 50 ohm generic slow/hot and       | conversions retained. Required points      |              |
|                          | fast/cold; alternating 0.9/2.0 V           | pass.                                      |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| evaluation               | DAC settle 0.5/1/2/3 ns x evaluate         | 20 points; 80 conversions; 56 failed       | PASS / GRID  |
|                          | 0.1/0.25/0.5/1/2 ns; four connected        | conversions retained. Required points      |              |
|                          | conversions per point                      | pass.                                      |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| jitter                   | Gaussian aperture/evaluate jitter          | 7 points; 224 conversions; 0 failed        | PASS / GRID  |
|                          | 0/10/50/100 ps RMS; seeds 7/19; 32         | conversions retained. Required points      |              |
|                          | conversions each; physical edge            | pass.                                      |              |
|                          | measurement                                |                                            |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| power                    | Complete startup + track/capture +         | 7 points; 24 conversions; 2 failed         | MEASURED     |
|                          | conversion/recovery; 2/4/6 ns bit periods; | conversions retained. Required points      |              |
|                          | 2/5/20 ns warmup; fine BDF2 replay         | pass.                                      |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| Characterization total   | 89 finite operating points; all required   | 1102/1102 required conversions pass;       | PASS / GRID  |
|                          | points must pass                           | 151/382 stress conversions fail and remain |              |
|                          |                                            | reported.                                  |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| All-node convergence     | 61 nodes; startup/acquisition/conversion;  | Both-grid peak 15.915566 mV; RMS 0.097986  | PASS         |
|                          | unshifted comparisons; numerical gates     | mV                                         |              |
|                          | below                                      |                                            |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
| Regression campaign      | All registered ADC suites, including       | 38/38 pass; no suites omitted. Analog++    | PASS         |
|                          | exhaustive DNL/INL and noise calibration   | transient regressions: 10/10 pass.         |              |
+--------------------------+--------------------------------------------+--------------------------------------------+--------------+
```

The new clock fixture uses continuous 50 ps PWL edges through 50 ohm drivers;
jitter tests also use 100 fF control loads. Aperture and evaluation perturbations
are separate seeded Gaussian sequences. Delivered half-level crossings are
measured at the physical pins. They are not rounded to the legacy bench's
100 ps command clock. SAR decisions still come from the electrical comparator
and retention/interface path; no ideal ADC supplies decisions.

At the measured physical capture aperture, the bench records both the ideal
upstream source and the delivered ADC-pin voltage. The external acquisition and
code-error checks use the upstream source, while retaining the original pin-based
checks. This exposes source-loading distortion even when the sampler closely
follows an already distorted input pin. Required operating points keep the
20 mV acquisition/hold and two-count error limits. Deliberately stressed points
remain visible; a passing characterization suite does not mean every grid point
passes, or that an unspecified product noise/spectral/power budget is satisfied.

### Frequency, amplitude and source impedance

```text
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| Sweep / point          | f MHz / peak V / R ohm   | External          | External code    | SNDR dB / ENOB at       | Failed /     |
|                        |                          | acquisition mV    | error (<=2)      | stimulus                | runs         |
|                        |                          | (<=20)            |                  |                         |              |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| frequency / bin_1      | 0.3125 / 0.45 / 50       | 0.080741          | 1                | 40.932 / 6.507          | 0/64         |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| frequency / bin_5      | 1.5625 / 0.45 / 50       | 0.403391          | 1                | 39.253 / 6.228          | 0/64         |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| frequency / bin_15     | 4.6875 / 0.45 / 50       | 1.209681          | 1                | 39.818 / 6.322          | 0/64         |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| frequency / bin_29     | 9.0625 / 0.45 / 50       | 2.338472          | 1                | 39.253 / 6.228          | 0/64         |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| amplitude /            | 1.5625 / 0.05 / 50       | 0.044878          | 1                | 20.255 / 3.072          | 0/64         |
| amplitude_50           |                          |                   |                  |                         |              |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| amplitude /            | 1.5625 / 0.25 / 50       | 0.224093          | 1                | 33.252 / 5.231          | 0/64         |
| amplitude_250          |                          |                   |                  |                         |              |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| amplitude /            | 1.5625 / 0.55 / 50       | 0.493074          | 1                | 40.667 / 6.463          | 0/64         |
| amplitude_550          |                          |                   |                  |                         |              |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| impedance / source_25  | 1.5625 / 0.45 / 25       | 0.296786          | 1                | 39.253 / 6.228          | 0/64         |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| impedance / source_50  | 1.5625 / 0.45 / 50       | 0.403391          | 1                | 39.253 / 6.228          | 0/64         |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| impedance / source_500 | 1.5625 / 0.45 / 500      | 2.405394          | 1                | 38.649 / 6.128          | 0/64         |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
| impedance /            | 1.5625 / 0.45 / 5000     | 68.424705         | 6                | 34.948 / 5.513          | 52/64        |
| source_5000            |                          |                   |                  |                         |              |
+------------------------+--------------------------+-------------------+------------------+-------------------------+--------------+
```

```text
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| Sweep / point          | Held gain / phase    | SNR / SNDR dB        | ENOB at stimulus     | SFDR / THD dB        | Source slew  |
|                        | deg                  |                      |                      |                      | V/us         |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| frequency / bin_1      | 1.000000 / -0.010253 | 41.258 / 40.932      | 6.507                | 50.615 / -52.340     | 0.883573     |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| frequency / bin_5      | 0.999999 / -0.051268 | 39.983 / 39.253      | 6.228                | 49.131 / -47.357     | 4.417865     |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| frequency / bin_15     | 0.999995 / -0.153798 | 40.322 / 39.818      | 6.322                | 49.113 / -49.419     | 13.253594    |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| frequency / bin_29     | 0.999982 / -0.297340 | 39.983 / 39.253      | 6.228                | 49.131 / -47.357     | 25.623615    |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| amplitude /            | 1.000000 / -0.051336 | 20.656 / 20.255      | 3.072                | 29.777 / -30.795     | 0.490874     |
| amplitude_50           |                      |                      |                      |                      |              |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| amplitude /            | 1.000000 / -0.051355 | 33.838 / 33.252      | 5.231                | 43.694 / -42.235     | 2.454369     |
| amplitude_250          |                      |                      |                      |                      |              |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| amplitude /            | 0.999999 / -0.051217 | 40.979 / 40.667      | 6.463                | 45.892 / -52.261     | 5.399612     |
| amplitude_550          |                      |                      |                      |                      |              |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| impedance / source_25  | 1.000000 / -0.037678 | 39.983 / 39.253      | 6.228                | 49.131 / -47.357     | 4.417865     |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| impedance / source_50  | 0.999999 / -0.051268 | 39.983 / 39.253      | 6.228                | 49.131 / -47.357     | 4.417865     |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| impedance / source_500 | 0.999985 / -0.306364 | 39.833 / 38.649      | 6.128                | 45.821 / -44.871     | 4.417865     |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
| impedance /            | 0.963014 / -8.421833 | 35.126 / 34.948      | 5.513                | 43.645 / -48.909     | 4.417865     |
| source_5000            |                      |                      |                      |                      |              |
+------------------------+----------------------+----------------------+----------------------+----------------------+--------------+
```

Spectral results use complete coherent 64-sample records. ENOB is reported at
the stated amplitude; it is not a full-scale ENOB claim. SNDR/ENOB/SFDR acceptance
budgets remain unspecified. These deterministic sweeps do not add intrinsic
compact-model noise or calibrated process mismatch.

### Upstream kickback and ripple response

The DC kickback runs measure input-pin excursion relative to the upstream
source and integrate signed returned current from the physical opening aperture
through hold/conversion/recovery. The generic model includes its explicit
coupling capacitors; intrinsic channel-charge injection still requires the
actual compact-model circuit. No product kickback budget is supplied.

```text
+--------------------+--------------------------------+----------------------------------+------------------------+
| Source ohm         | Peak input disturbance mV      | Max abs returned charge fC       | Failed / runs          |
+--------------------+--------------------------------+----------------------------------+------------------------+
| 25                 | 0.003007                       | 0.003593                         | 0/3                    |
+--------------------+--------------------------------+----------------------------------+------------------------+
| 50                 | 0.005997                       | 0.003590                         | 0/3                    |
+--------------------+--------------------------------+----------------------------------+------------------------+
| 500                | 0.057087                       | 0.003579                         | 0/3                    |
+--------------------+--------------------------------+----------------------------------+------------------------+
| 5000               | 46.207389                      | 0.160640                         | 2/3                    |
+--------------------+--------------------------------+----------------------------------+------------------------+
```

The ripple fits use actual aperture timestamps and delivered rail voltages.
Sampled-hold PSRR uses the continuous held-input error; code-equivalent PSRR
uses the ADC output expressed against the nominal 3.3 V reference. Because IO
also supplies this R/2R DAC reference, IO code modulation includes reference
gain modulation. Responses below the 1 uV held-error reporting floor or half an
LSB at the output are labelled unresolved rather than infinite rejection.
These reporting floors are not product specifications.

```text
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| Rail / point             | Ripple frequency    | Delivered peak mV    | Hold response uV / PSRR   | Code response mV / PSRR dB    |
|                          | MHz                 |                      | dB                        |                               |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| core_1                   | 0.6250              | 24.879749            | 0.000329 / unresolved     | 0.000000 / unresolved         |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| core_13                  | 8.1250              | 24.901769            | 0.000329 / unresolved     | 0.000000 / unresolved         |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| core_5                   | 3.1250              | 24.882896            | 0.000329 / unresolved     | 0.000000 / unresolved         |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| core_refined5ps          | 3.1250              | 24.882896            | 0.000326 / unresolved     | 0.000000 / unresolved         |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| io_1                     | 0.6250              | 24.844165            | 0.587955 / unresolved     | 13.045671 / 5.595             |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| io_13                    | 8.1250              | 24.877775            | 7.629417 / 70.266         | 11.690408 / 6.560             |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| io_5                     | 3.1250              | 24.848966            | 2.938152 / 78.545         | 12.921566 / 5.680             |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| io_amplitude_5           | 3.1250              | 4.969793             | 0.587884 / unresolved     | 0.000000 / unresolved         |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| io_amplitude_50          | 3.1250              | 49.697887            | 5.875052 / 78.547         | 22.946911 / 6.712             |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
| io_refined5ps            | 3.1250              | 24.814593            | 2.931313 / 78.553         | 12.921566 / 5.668             |
+--------------------------+---------------------+----------------------+---------------------------+-------------------------------+
```

At 3.125 MHz, the 10/5 ps hold-response checks pass for both rails: IO amplitude
difference 0.006839 uV and core
0.000003 uV. The numerical gate is the larger of 0.1 uV or
12% of response amplitude. Full process/layout PSRR and a product rejection
budget remain separate.

### Connected acquisition and evaluation limits

Each point includes four alternating 0.9/2.0 V conversions on the same slice.
A point passes only if all four conversions meet the external-source and
internal electrical contracts. "Shortest tested pass" is a discrete tested
boundary, not a proven continuous-time minimum or a process PVT limit.

```text
+-------------------------------+---------------------------------+--------------------------+----------------------------+
| Connected acquisition         | All-conversions passing track   | Failing track ns         | Shortest tested pass       |
| condition                     | ns                              |                          |                            |
+-------------------------------+---------------------------------+--------------------------+----------------------------+
| Nominal / 50 ohm              | 0.5, 1, 2, 4, 8                 | 0.25                     | 0.5 ns                     |
+-------------------------------+---------------------------------+--------------------------+----------------------------+
| Nominal / 500 ohm             | 4, 8                            | 0.25, 0.5, 1, 2          | 4 ns                       |
+-------------------------------+---------------------------------+--------------------------+----------------------------+
| Nominal / 5000 ohm            | none                            | 0.25, 0.5, 1, 2, 4, 8    | not established            |
+-------------------------------+---------------------------------+--------------------------+----------------------------+
| Generic slow / 125 C          | 0.5, 1, 2, 4, 8                 | 0.25                     | 0.5 ns                     |
+-------------------------------+---------------------------------+--------------------------+----------------------------+
| Generic fast / -40 C          | 0.5, 1, 2, 4, 8                 | 0.25                     | 0.5 ns                     |
+-------------------------------+---------------------------------+--------------------------+----------------------------+
```

```text
+------------------------+---------------------------------------------------+---------------------------------------------------+
| DAC settling ns        | Evaluation times passing all 4 conversions, ns    | Failing evaluation times, ns                      |
+------------------------+---------------------------------------------------+---------------------------------------------------+
| 0.5                    | none                                              | 0.1, 0.25, 0.5, 1, 2                              |
+------------------------+---------------------------------------------------+---------------------------------------------------+
| 1.0                    | none                                              | 0.1, 0.25, 0.5, 1, 2                              |
+------------------------+---------------------------------------------------+---------------------------------------------------+
| 2.0                    | 1, 2                                              | 0.1, 0.25, 0.5                                    |
+------------------------+---------------------------------------------------+---------------------------------------------------+
| 3.0                    | 1, 2                                              | 0.1, 0.25, 0.5                                    |
+------------------------+---------------------------------------------------+---------------------------------------------------+
```

The default 8 ns acquisition and 2 ns DAC settling + 2 ns evaluation schedule
remains the rated bench schedule. Shorter passing points document available
generic-model margin; they do not change the physical timing specification.

### Random jitter and complete-cycle rail power

The delivery residual removes the mean finite-driver delay, then compares each
physical edge against its requested random offset. The gate is <=5 ps; sample
RMS values naturally differ from the requested population RMS. These seeded
records are not a rare-event metastability/yield qualification. The fixed
regression envelope requires the 0/10/50 ps cases to pass; 100 ps is stress data.

```text
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
| Requested RMS ps /    | Delivered aperture   | Delivered evaluate   | Max timing delivery   | External code error  | Failed /     |
| seed                  | RMS ps               | RMS ps               | residual ps           | / invalid decisions  | runs         |
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
| 0 / 7                 | 0.000                | 0.000                | 0.000000              | 1 / 0                | 0/32         |
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
| 10 / 7                | 10.478               | 10.506               | 0.003321              | 1 / 0                | 0/32         |
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
| 10 / 19               | 9.116                | 9.642                | 0.003157              | 1 / 0                | 0/32         |
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
| 50 / 7                | 52.390               | 52.531               | 0.003234              | 1 / 0                | 0/32         |
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
| 50 / 19               | 45.582               | 48.209               | 0.003312              | 1 / 0                | 0/32         |
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
| 100 / 7               | 104.780              | 105.061              | 0.003235              | 1 / 0                | 0/32         |
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
| 100 / 19              | 91.165               | 96.419               | 0.003222              | 1 / 0                | 0/32         |
+-----------------------+----------------------+----------------------+-----------------------+----------------------+--------------+
```

Power accounting uses trapezoidal source-side integration on the actual
nonuniform solver grid. Startup is separate. Every complete cycle includes
tracking/capture, all decisions, and recovery up to the next acquisition origin.
The reported power is the IO/core rail contribution; external input and clock
stimulus-driver energy is outside these rail totals. Signed supply charge and
energy, individual phases and peaks remain in the per-point CSVs.

```text
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
| Point                   | Startup energy       | Full-cycle energy     | Average rail      | Full-cycle peak IO /  | Failed /     |
|                         | IO+core pJ           | IO+core pJ            | power mW          | core mA               | runs         |
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
| period_2000             | 135.282              | 223.965-421.927       | 6.587-12.410      | 19.041 / 0.249255     | 2/4          |
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
| period_4000             | 135.282              | 373.162-612.653       | 7.463-12.253      | 19.090 / 0.249294     | 0/4          |
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
| period_6000             | 135.282              | 480.548-803.260       | 7.281-12.171      | 19.090 / 0.249300     | 0/4          |
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
| refined_0.156250        | 161.336              | 442.394-443.133       | 8.848-8.863       | 20.333 / 0.254871     | 0/2          |
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
| refined_0.312500        | 161.337              | 442.395-443.134       | 8.848-8.863       | 20.324 / 0.254873     | 0/2          |
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
| warmup_2000             | 132.322              | 373.221-612.653       | 7.464-12.253      | 19.090 / 0.249294     | 0/4          |
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
| warmup_5000             | 132.864              | 373.162-612.653       | 7.463-12.253      | 19.090 / 0.249294     | 0/4          |
+-------------------------+----------------------+-----------------------+-------------------+-----------------------+--------------+
```

The two nominal 1.45 V power reference runs use BDF2 at 0.3125/0.15625 ps, with
local 1/0.5 fs refinement for 5 ps at startup and after actual driver changes.
Maximum full-cycle energy/average-current difference is
0.000156% (limit 1%); maximum peak-current difference is
0.042253% (limit 5%). Product power/current budgets and physical reliability
qualification remain unspecified.

### All-node waveform convergence

The old 10/5 ps backward-Euler comparison had a 2.634778 V instantaneous
difference despite matching final codes. Finer BDF2 runs exposed additional
unresolved fast responses in the DAC drivers, the initial charging of sampling
networks, and the response after TRACK changes. A generic 3 fF driver load with
low channel resistance can require femtosecond resolution immediately after a
switch. No device size, capacitance, code limit or voltage gate was changed to
hide these numerical errors.

Analog++ now offers opt-in `simulation::edge_step` and `edge_window`. It refines
startup and the interval after an actual `sim_driver` bit change, preserving
existing source breakpoints and carrying the window across `advance_to()` calls.
Idle clock edges do not trigger additional bursts. Both options default to zero;
this is explicit event refinement, not a general automatic error-control solver.
An independent RC regression checks waveform accuracy, initialization, retained
history, breakpoint preservation, negative controls and invalid settings.

The reference study uses BDF2 with regular steps 0.625/0.3125/0.15625 ps and local
steps 2/1/0.5 fs over 5 ps windows. Long warmup integrations are split with
`advance_to()` while preserving state. All 61 slice nodes are saved from startup
through acquisition and the final decision. Comparisons use the same physical
timestamps, including switching edges; only timestamp rounding within 1e-18 s
is snapped. No waveform is shifted to hide delay or cropped to omit a transient.

```text
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Numerical check                | Acceptance / scope                        | Measured result                           | Status   |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Nested-grid instantaneous      | <= 20 mV                                  | 12.640163 mV                              | PASS     |
| error                          |                                           |                                           |          |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Held-node instantaneous error  | <= 1 mV                                   | 0.255559 mV                               | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Maximum per-node RMS error     | <= 1 mV                                   | 0.078304 mV                               | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Threshold crossing time error  | <= 2 ps                                   | 0.078138 ps                               | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| IO/core energy difference      | <= 1 %                                    | 0.000385 %                                | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| IO/core charge difference      | <= 1 %                                    | 0.000232 %                                | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| IO/core peak current           | <= 5 %                                    | 0.000017 %                                | PASS     |
| difference                     |                                           |                                           |          |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Both-grid instantaneous error  | <=20 mV; all saved timestamps             | 15.915566 mV                              | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Both-grid RMS / held peak      | <=1 mV each                               | 0.097986 / 0.584434 mV                    | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Compared interval              | Startup, acquisition and all 8 decisions  | 0-61 ns; 61 nodes                         | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Waveform alignment             | No time shifting or deleted switching     | 212 matched crossings; no unmatched nodes | PASS     |
|                                | windows                                   |                                           |          |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Refinement trend               | Final peak error <=60% of preceding pair  | 50.326010 -> 12.640163 mV                 | PASS     |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
| Independent tool regression    | 10 fs RC; <200 uV analytic error;         | 70.3202 uV max; zero final continuation   | PASS     |
|                                | preserve PWL knots and continuation       | difference; idle clocks do not trigger    |          |
|                                |                                           | bursts                                    |          |
+--------------------------------+-------------------------------------------+-------------------------------------------+----------+
```

The registered test samples the fine solution at the coarser grid timestamps.
An additional [both-grid verification](../../../build/adc-sar-characterization/campaign-z9ucd6w6/waveforms/union_grid_check.json)
checks every saved timestamp in both grids; its [reproducible script](../../../build/adc-sar-characterization/campaign-z9ucd6w6/waveforms/verify_union.py) is included in the hashed evidence.
Ordinary linear interpolation across an ideal control jump would invent a ramp:
an intentionally naive reverse-grid diagnostic produces a spurious 1.650 V
error. The supplemental comparison derives capacitance-free sim_driver nodes
from the emitted equations, verifies identical jump times, and uses the left
limit before each jump and the recorded right value at the jump. Continuous
nodes retain ordinary interpolation. No node, time interval or switching
transient is excluded. The larger both-grid error is the headline result.


These gates qualify the tested nominal generic reference waveform. The legacy
10 ps mode remains useful for its tested code/settled-metric contracts; it is not
promoted to a converged fast-waveform mode. Foundry corner, intrinsic-charge/noise
and extracted-layout waveform qualification still require their own receipts.

## Prior deterministic campaign: 2026-11-13

[Full results](../../../build/adc-sar-lib/campaign-x6ynxwjj/results.md),
[hashed evidence](../../../build/adc-sar-lib/campaign-x6ynxwjj/checks.json).
These are **generic direct-drive library experiments**, not physical-process results.
Do not compare their acquisition numbers directly with the earlier connected
PGA/PAM-5 process-model campaign.

```text
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Test / measurement         | Requirement                        | Current value                                | Status     |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Regression suites          | All 22 registered suites pass      | 22/22 PASS                                   | PASS       |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Conversion population      | Apply the conversion contract;     | 207 ordinary conversions: 203 pass, 4        | PASS       |
|                            | retain expected negative controls  | expected stress failures; plus 1128          |            |
|                            |                                    | transition conversions                       |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Rated-period records       | Acquisition <=20 mV; hold <=20 mV; | 186/186 pass (ordinary period/settling       | PASS       |
|                            | code error <=2; valid decisions    | sweeps excluded; transition probes listed    |            |
|                            |                                    | separately)                                  |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Acquisition error          | <=20 mV after capture              | 0.099590 mV maximum                          | PASS       |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Hold variation             | <=20 mV peak-to-peak               | 5.980395 mV maximum                          | PASS       |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Absolute held-target error | No separate limit specified        | 5.502289 mV maximum                          | MEASURED   |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Code error                 | <=2 counts                         | 1 count maximum; 158 exact, 28 one-count     | PASS       |
|                            |                                    | results                                      |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Input-range sweep          | 0.85-2.05 V stimulus; conversion   | 33/33 pass; 28 exact, 5 one-count errors     | PASS       |
|                            | contract at every point            |                                              |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Comparator decisions       | 8 per conversion; core low <0.36 V | 1,488 decisions in ordinary rated records; 0 | PASS       |
|                            | or high >0.84 V; consistent        | invalid                                      |            |
|                            | comparator outputs                 |                                              |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Decision stability         | Same valid decision throughout     | All rated-record windows pass                | PASS       |
|                            | +/-25 ps of sampling               |                                              |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Decision settling margin   | No additional margin budget;       | 1285.000000 ps minimum                       | MEASURED   |
|                            | stability gate above applies       |                                              |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Rated bit schedule         | 4 ns/bit; 8 ns acquisition; 2 ns   | All rated records pass; 1 ns DAC settling    | PASS       |
|                            | DAC settling and 2 ns evaluation   | detects a 5-count error                      |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| I/O/core supply sweep      | Conversion contract at each source | 3.0/3.3/3.35 V x 1.08/1.2/1.35 V; 18/18      | PASS       |
|                            | setting                            | conversions pass                             |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| I/O ripple                 | Conversion contract under the      | +/-25 mV, 16 ns period; 2/2 pass; separate   | PASS       |
|                            | specified disturbance              | PSRR budget absent                           |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Average I/O current        | No current budget specified        | 2.037858-4.278528 mA                         | MEASURED   |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Peak I/O current           | No peak-current budget specified   | 63.504328 mA maximum                         | MEASURED   |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Supply energy              | No energy budget specified         | 212.086395-455.525655 pJ per observation     | MEASURED   |
|                            |                                    | window                                       |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| SNR                        | No spectral acceptance budget      | 39.982940 dB                                 | MEASURED   |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| SNDR                       | No spectral acceptance budget      | 39.252861 dB                                 | MEASURED   |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| ENOB at test amplitude     | No ENOB acceptance budget          | 6.228050 bits (not full scale)               | MEASURED   |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| SFDR                       | No spectral acceptance budget      | 49.131158 dB                                 | MEASURED   |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| THD                        | No THD acceptance budget           | -47.356934 dB                                | MEASURED   |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Timestep: final code       | Identical codes at 10 ps and 5 ps  | 128 / 128; difference 0 counts               | PASS       |
|                            | requested step                     |                                              |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Timestep: selected         | Acquisition, hold and              | Code delta 0; max analog delta 0.004784639   | PASS       |
| voltages                   | absolute-hold differences <1 mV    | mV                                           |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
| Timestep: all nodes        | Full-waveform convergence not      | 61 nodes; 2.634778 V largest pointwise       | OPEN       |
|                            | established; no per-node tolerance | difference                                   |            |
|                            | specified                          |                                              |            |
+----------------------------+------------------------------------+----------------------------------------------+------------+
```

The aggregate voltage, code, decision and power values above use the 186 ordinary
rated records, excluding period and DAC-settling sweeps. Transition-probe
measurements are listed separately below. Current/energy values
cover only the conversion observation window and remain timestep/model limited.
The spectral record has 64 points, five sine cycles, 50 ns sample spacing,
1.5625 MHz input, 1.45 V center and 0.45 V peak amplitude (rectangular DFT).
MEASURED means a value exists but no product acceptance limit was specified;
OPEN means that qualification is incomplete. All registered suites were rerun
for this campaign after correcting the library acquisition sequence.

The full table below retains open acceptance and integration work: DNL/INL
magnitude budgets and PVT/layout characterization, process-backed noise
calibration, and product spectral/kickback/PSRR/power limits. The current
characterization campaign above adds finite sweeps and complete-cycle power
measurements. The sampled-noise implementation and its validation are
reported below. A passing suite count does not close
those unimplemented or unspecified acceptance requirements.

### Single-slice ADC transition characterization

The test searches actual ADC transitions using complete eight-bit electrical
conversions. One live slice retains its capacitor and latch state for both
ascending and descending searches and all center repetitions. The search uses
adaptive bisection, so individual input probes need not form a monotonic ramp.
A separate one-slice run checks selected boundaries at half the solver timestep.
No parallel slices, interleaving or ideal-ADC decisions are used.

The new repeat test exposed an acquisition-sequence error: leaving the previous
SAR result on the DAC during tracking changed the preamplifier state. Its next
trial then coupled a history-dependent voltage into the held input. The library
sequencer now drives the first (midscale) trial during tracking, with EVALUATE
low. The capture command remains 8 ns after the per-sample stimulus epoch.
At 0.8572 V, the old sequence returned code 65 after a 2.05 V input despite the
search identifying code 66's interval. The corrected
sequence returns code 66 on all five history-regression visits. Integrators must
apply this sequence to their external controller; this is not a new PHY RTL or
floorplan qualification. No device size, hold capacitance or accuracy limit was
changed to obtain these results.

For transition T[k] from code k-1 to k, width[k] = T[k+1] - T[k]. The
endpoint-fitted LSB is (T[last] - T[first]) / (last - first). DNL is width / fitted
LSB - 1; INL is transition departure from that endpoint line / fitted LSB. The
report also retains nominal-LSB widths and transition errors, so removing
endpoint offset/gain cannot conceal them. Only complete bins within the specified
input range are included; this does not qualify all 256 possible code bins.
Voltage brackets propagate to conservative metric intervals in checks.json.
Those intervals cover search resolution, not noise, all solver error, or PVT.

```text
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Transition test /           | Requirement / scope                   | Current value                                 | Status     |
| measurement                 |                                       |                                               |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Electrical coverage         | One nominal slice; 0.85-2.05 V; eight | T[66] through T[159], both search directions; | PASS       |
|                             | actual comparator decisions per       | 1128 conversions total                        |            |
|                             | conversion                            |                                               |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Complete code bins          | Only bins bounded on both sides by    | Codes 66-158; 93 complete bins; clipped range | PASS       |
|                             | measured transitions                  | endpoints excluded                            |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Transition bracket          | Width <= nominal LSB/64               | Limit 0.201416 mV; observed max 0.201416 mV   | PASS       |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Missing codes / ordered     | Positive, disjoint complete-bin       | 0 missing complete codes in both directions   | PASS       |
| bins                        | bounds; centers produce expected code |                                               |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Repeated interval centers   | Two conversions per center and        | 372/372 expected codes                        | PASS       |
|                             | direction, retained circuit state     |                                               |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| History regression          | Interior code independent of prior    | Five visits to 0.8572 V all code 66 after     | PASS       |
|                             | input                                 | midscale DAC tracking fix                     |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Direction agreement         | Midpoint difference <= 2 search       | 0.151062 mV; limit 0.402832 mV                | PASS       |
|                             | tolerances                            |                                               |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Selected timestep check     | 10 ps -> 5 ps at 66, 96, 128, 159     | Max difference 0.170535 mV; limit 0.402832    | PASS       |
|                             |                                       | mV; 20 conversions                            |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Transition-probe voltage    | Acquisition/hold <=20 mV              | Acquisition max 0.000008 mV; hold max         | PASS       |
|                             |                                       | 5.514708 mV                                   |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Transition-probe accuracy   | Code error <=2; eight valid stable    | 758 exact, 370 one-count, 0 two-count; 9,024  | PASS       |
|                             | decisions                             | valid decisions; no invalid decisions         |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Transition-probe timing     | Valid decision throughout +/-25 ps    | Minimum measured settling margin 1420.000 ps  | PASS       |
|                             | sampling window; no extra margin      |                                               |            |
|                             | budget                                |                                               |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Ascending endpoint DNL      | Product magnitude limit unspecified   | Max abs 0.191415 LSB                          | MEASURED   |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Ascending endpoint INL      | Product magnitude limit unspecified   | Max abs 0.126837 LSB                          | MEASURED   |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Ascending nominal DNL       | Normalize widths by 3.3 V / 256       | Max abs 0.196045 LSB                          | MEASURED   |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Ascending nominal           | T[k]/nominal_LSB - k; retains gain    | Max abs 0.278442 LSB                          | MEASURED   |
| transition error            | and offset                            |                                               |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Descending endpoint DNL     | Product magnitude limit unspecified   | Max abs 0.182959 LSB                          | MEASURED   |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Descending endpoint INL     | Product magnitude limit unspecified   | Max abs 0.129013 LSB                          | MEASURED   |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Descending nominal DNL      | Normalize widths by 3.3 V / 256       | Max abs 0.187744 LSB                          | MEASURED   |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Descending nominal          | T[k]/nominal_LSB - k; retains gain    | Max abs 0.288208 LSB                          | MEASURED   |
| transition error            | and offset                            |                                               |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Fitted gain / offset        | Partial-range endpoint fit; not a     | Ascending: gain -0.572549%; extrapolated      | MEASURED   |
|                             | zero-input offset measurement         | intercept 8.460437 mV                         |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
| Search-bracket uncertainty  | Conservative interval propagation; no | Worst abs endpoint DNL <= 0.203549; INL <=    | MEASURED   |
|                             | random-noise confidence claim         | 0.141260 LSB within measured brackets         |            |
+-----------------------------+---------------------------------------+-----------------------------------------------+------------+
```

DNL/INL are **measured**, with product magnitude limits still unspecified.
PASS applies to electrical conversion validity, positive complete-bin widths,
repetition, and the stated numerical consistency checks. It does not imply a
linearity budget, transistor-model equivalence or extracted-layout signoff.

### Prior deterministic values by experiment

Every conversion is evaluated against acquisition <=20 mV, hold variation
<=20 mV, code error <=2 counts, and eight valid decisions stable across the
+/-25 ps sampling window. The failed 1 ns period, 0.2 ns period and 1 ns DAC
settling cases are intentionally inadequate timing experiments. Their detection
is part of the passing suite results; they are not rated-operation passes.
Values rounded to 0.000000 mV are below the displayed precision, not proof of
exactly zero analog error.

```text
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| Experiment                  | Runs | Failed | Max acquisition   | Max hold mV    | Max code error  | Result        |
|                             |      |        | mV (<=20)         | (<=20)         | counts (<=2)    |               |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| clock_load_1000             | 2    | 0      | 0.000644          | 5.205569       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| clock_load_50               | 2    | 0      | 0.000007          | 5.238854       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| clock_load_500              | 2    | 0      | 0.000274          | 5.220742       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| coarse                      | 1    | 0      | 0.000002          | 5.572330       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| conversion                  | 5    | 0      | 0.000008          | 5.562568       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| dac_settle_1000ps           | 4    | 1      | 0.000006          | 4.298697       | 5               | EXPECTED FAIL |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| dac_settle_2000ps           | 4    | 0      | 0.000006          | 4.239477       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| dac_settle_3000ps           | 4    | 0      | 0.000006          | 4.240691       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| dynamic                     | 64   | 0      | 0.099590          | 5.523517       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| history                     | 8    | 0      | 0.000007          | 5.572052       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| mismatch_1                  | 2    | 0      | 0.000007          | 5.241185       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| mismatch_2                  | 2    | 0      | 0.000007          | 5.221320       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| mismatch_3                  | 2    | 0      | 0.000007          | 5.245455       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| mismatch_4                  | 2    | 0      | 0.000007          | 5.224635       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| mismatch_5                  | 2    | 0      | 0.000007          | 5.244726       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| mismatch_6                  | 2    | 0      | 0.000007          | 5.237542       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| mismatch_7                  | 2    | 0      | 0.000007          | 5.255265       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| mismatch_8                  | 2    | 0      | 0.000007          | 5.225993       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| period_1                    | 2    | 2      | 0.000007          | 5.460209       | 4               | EXPECTED FAIL |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| period_2                    | 2    | 0      | 0.000007          | 5.571034       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| period_4                    | 2    | 0      | 0.000007          | 5.572330       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| period_6                    | 2    | 0      | 0.000007          | 5.572382       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| period_negative_control     | 1    | 1      | 0.000002          | 2.941996       | 77              | EXPECTED FAIL |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| power_cycle                 | 3    | 0      | 0.000007          | 5.225639       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| range                       | 33   | 0      | 0.000006          | 5.481726       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| refined                     | 1    | 0      | 0.000002          | 5.577115       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| ripple                      | 2    | 0      | 0.032021          | 5.124453       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| routed_synthetic            | 5    | 0      | 0.000008          | 5.588135       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| sensitivity_fast_cold       | 3    | 0      | 0.000007          | 5.763582       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| sensitivity_nfast_pslow     | 3    | 0      | 0.000007          | 5.765353       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| sensitivity_nslow_pfast     | 3    | 0      | 0.000008          | 5.297682       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| sensitivity_passive_high    | 3    | 0      | 0.000008          | 5.194333       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| sensitivity_passive_low     | 3    | 0      | 0.000006          | 5.980395       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| sensitivity_slow_hot        | 3    | 0      | 0.000008          | 5.268055       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| startup_20ns                | 2    | 0      | 0.000007          | 5.240621       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| startup_2ns                 | 2    | 0      | 0.000007          | 5.240621       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| startup_5ns                 | 2    | 0      | 0.000007          | 5.240621       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_0                    | 2    | 0      | 0.000005          | 4.302822       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_1                    | 2    | 0      | 0.000005          | 4.302822       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_2                    | 2    | 0      | 0.000005          | 4.302822       | 1               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_3                    | 2    | 0      | 0.000007          | 5.240621       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_4                    | 2    | 0      | 0.000007          | 5.240621       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_5                    | 2    | 0      | 0.000007          | 5.240621       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_6                    | 2    | 0      | 0.000007          | 5.388168       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_7                    | 2    | 0      | 0.000007          | 5.388168       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
| supply_8                    | 2    | 0      | 0.000007          | 5.388168       | 0               | PASS          |
+-----------------------------+------+--------+-------------------+----------------+-----------------+---------------+
```

## Sampled-noise model and calibration

This implementation is opt-in **sampled input-equivalent noise**. It provides a
kT/C reference, band-limited white/flicker records, a PSD coefficient fitter,
and a blind estimator that recovers RMS noise from actual electrical ADC codes.
It does not claim intrinsic compact-model transient-noise simulation or measured calibration.
The default electrical slice remains deterministic until the bench applies a
noise record to its analog stimulus during acquisition. No output code is
perturbed or replaced by an ideal quantizer.

`noise.h` generates seeded records before simulation. Sampling noise
is IID Gaussian with variance `ktc_variance_scale * k_B * T / C_eff`. Additional
one-sided PSD is `S(f) = white_psd_V2_Hz + flicker_V2 / f`, integrated only over
`[low_Hz, high_Hz]`. Its Gaussian Fourier coefficients have Hermitian symmetry;
the inverse transform retains the spectrum's time correlation. Random values
are never drawn inside Newton iterations or on solver retries. A record is
finite: do not wrap it or reuse its FFT period as a new independent record.
The actual sample cadence must match `period_s`.

The default `C_eff` is the temperature-scaled hold capacitor plus lumped held
capacitance. It is an **ideal settled sampling estimate**, not a calibrated sum
of bias-dependent MOS/Miller capacitances. With kT/C enabled, extracted routes
require an explicit `capacitance_F` override so extraction cannot silently disappear from the noise
calculation. Additional PSD requires a matching calibration temperature; its
bias, source/load, capture schedule and bandwidth must also match the application.
The factory does not extrapolate a measured PSD to another process or corner.
The reference PSD must already include the relevant analog transfer and aliasing
into sampled baseband; a raw transistor noise PSD is not an ADC input PSD.

The reference uses the exact [SI Boltzmann constant](https://physics.nist.gov/cgi-bin/cuu/Value?k).
The independent electrical regression applies a Johnson voltage source with
one-sided density `4*k_B*T*R` to an Analog++ RC circuit and checks its output
against `sqrt(k_B*T/C)`. Finite PWL bandwidth and record duration are included in
the stated regression tolerance. The [ADC input-noise measurement guidance from
Analog Devices](https://www.analog.com/en/resources/analog-dialogue/articles/adc-input-noise.html)
explains why a code-center histogram can hide sub-LSB noise; here the ADC's
transition probabilities are fitted instead.

```text
+------------------------------+------------------------------------------+--------------------------------------------------------+
| Parameter / input            | Default / rule                           | Meaning                                                |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| include_ktc                  | true for generated records               | Add ideal sampled kT/C only when the supplied          |
|                              |                                          | additional PSD excludes it.                            |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| capacitance_F                | 0: use scaled hold C + lumped held C     | Explicit override required for kT/C with               |
|                              |                                          | external_routes. Bias-dependent MOS C must be included |
|                              |                                          | in a calibrated effective C.                           |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| ktc_variance_scale           | 1                                        | Variance multiplier for an independently calibrated    |
|                              |                                          | sampling-noise correction.                             |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| white_psd_V2_Hz              | 0; omitted, not measured zero            | Additional one-sided input-referred white PSD.         |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| flicker_V2                   | 0; omitted, not measured zero            | K in the additional one-sided PSD K/f.                 |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| low_Hz / high_Hz             | 1 kHz / 8 MHz                            | Engineering analysis band, not calibrated device noise |
|                              |                                          | specifications. Nonzero PSD must fit the record        |
|                              |                                          | resolution and Nyquist limit.                          |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| reference_temperature_K      | 0 for absent extra PSD                   | Nonzero PSD requires a positive calibration            |
|                              |                                          | temperature matching the slice corner. No automatic    |
|                              |                                          | temperature extrapolation.                             |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| count / period_s / seed      | Explicit call arguments                  | Power-of-two finite record, 16 through 2^22 samples;   |
|                              |                                          | replay the same record for solver-step comparisons.    |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| Corner / Passives / Layout   | Existing slice template parameters       | kT/C temperature follows Corner; default C follows     |
|                              |                                          | hold, cap_scale, MIM TC and held_c.                    |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| PSD provenance               | Required source kind, reference and      | CSV SHA256, temperature, fit band and sampling-noise   |
|                              | condition                                | ownership are recorded in JSON.                        |
+------------------------------+------------------------------------------+--------------------------------------------------------+
| input_noise_rms limit        | NaN                                      | No product noise acceptance budget is supplied;        |
|                              |                                          | measured RMS is not a product PASS.                    |
+------------------------------+------------------------------------------+--------------------------------------------------------+
```

For ideal sampled-noise records:

```cpp
using namespace analog::adc_sar;
auto samples = adc_sar_slice<>::sample_noise(1024, 50e-9, 7);
// During acquisition, drive the real analog input with:
double noisy_input = quiet_input + samples.total.at(sample_index);
```

Calibrate additional noise from a CSV with columns
`frequency_Hz,input_psd_V2_Hz` (linear, one-sided PSD, not ASD or dB):

```sh
python3 analog/lib/adc_sar/noise_calibration.py \
  --psd my_slice_psd.csv --output calibrated_noise.json \
  --header calibrated_noise.h --source-kind compact-model \
  --reference 'Qualified noise deck and model hashes' \
  --condition 'TT; IO 3.3 V; core 1.2 V; input 1.65 V; declared acquisition/load' \
  --temperature-k 300.15 --sampling-noise excluded \
  --low-hz 1000 --high-hz 8000000
```

Use `--sampling-noise included` for a total PSD that already contains sampling
noise; the generated profile then disables the separate kT/C term. The fitter
uses nonnegative white and flicker coefficients, rejects insufficient/invalid
frequency coverage, and rejects relative RMS residual above 10% by default.
`--maximum-relative-rms-residual` sets that explicit fit-quality gate. JSON
retains the original file SHA256, source kind, reference, condition and fit band.
The optional header defines `calibrated_noise`, usable as the final slice
`Noise` template argument. A runtime profile can also be passed as the fourth
argument of `sample_noise()`.

The regression fixture deliberately identifies itself as `synthetic`: its
known PSD is `4e-16 + 4e-10/f V^2/Hz`. Its fitted coefficients are imported back
into the native C++ slice factory and their generated RMS is checked. This
validates the import/calibration path; the values are **not technology defaults**.
Device-card noise must be propagated through the biased circuit's noise
transfer function before it becomes an ADC input-referred PSD.

### Prior sampled-noise campaign results

[Selected-suite results](../../../build/adc-sar-noise/campaign-toqjnkrg/results.md),
[hashed evidence](../../../build/adc-sar-noise/campaign-toqjnkrg/checks.json),
[calibration profile](../../../build/adc-sar-noise/campaign-toqjnkrg/noise_adc/calibrated_profile.json).

This campaign runs 25 of the 26 registered suites: all 21 existing shorter
regressions and all four new noise suites (`noise_model`, `noise_rc`,
`noise_metrics`, `noise_adc`). The exhaustive deterministic transition scan is
explicitly omitted; its prior 22-suite receipt above remains separate. No old
transition result is promoted to a new-source pass. All 97 ordinary CSVs, including per-bit decisions, are byte-for-byte identical
to that prior campaign; `deterministic_csv_comparison.json` records the hashes.

```sh
python3 analog/lib/adc_sar/tests/run.py \
  --build build/adc-sar-noise --jobs 2 --exclude-transitions
# Omit --exclude-transitions to run all 26 registered suites.
```

An initial [320-observation diagnostic](../../../build/adc-sar-noise/campaign-ft1v78ce/results.md)
estimated 51.736151 uV RMS, with a 43.091277-62.665911 uV 95% interval.
The final measurement was expanded once to the fixed four seeds below; it
retains the initial samples and does not select favorable seeds or relax limits.
A profile interval is a finite-sample estimate, not guaranteed coverage of the
known reference in every run.

The electrical ADC noise measurement uses nominal 27 C, 3.3 V IO and 1.2 V
core supplies at T[128]. The nine generator T/C cases do not qualify electrical
ADC noise across PVT. Each timestep run keeps one live slice across its entire
campaign. It searches T[128] to at most sigma/32, checks stable noise-off
codes at +/-sigma/4, and interleaves five quiet input levels across four fixed seeds (7, 19, 31 and 43).
Each injected sample is generated by the C++ slice factory and reaches the
physical sampling/holding/conversion path. The estimator receives only quiet
input voltage and observed code, not the injected noise values or expected RMS.
It fits both transition center and Gaussian RMS by binomial maximum likelihood.
Its 95% profile interval assumes IID Gaussian sampling noise; correlated
white/flicker data require a correlation-aware uncertainty analysis instead.
The 5 ps replay uses the same seed-7 samples and refits the center, so this is
a noise-width convergence check, not an absolute-offset or full-waveform claim.

```text
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Test / measurement          | Requirement / data source                  | Current result                            | Status     |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Selected regression         | 21 existing suites + 4 new noise suites;   | 25/25 selected suites PASS; 26 registered | PASS       |
| campaign                    | exhaustive transition suite explicitly     |                                           |            |
|                             | omitted                                    |                                           |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Ideal sampling reference    | 1 pF, 27 C, ktc_variance_scale=1           | 64.374047 uV RMS                          | REFERENCE  |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Generator T/C calibration   | 9 cases: -40/27/125 C x 0.5/1/2 pF; 131072 | Nominal 64.579138 uV; max RMS error       | PASS       |
|                             | samples each; RMS error <1.5%              | 0.318592%                                 |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Generator mean /            | Mean and lag-1 correlation each <1.5% of   | Nominal mean -0.171328 uV; lag-1          | PASS       |
| independence                | normalized scale                           | correlation 0.000193                      |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Independent electrical RC   | Johnson source through 1 kohm RC; T/C      | Nominal 62.401805 uV vs 64.374047; max    | PASS       |
|                             | cases; RMS error <12%                      | error 3.063722%                           |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Electrical RC timestep      | Same forcing waveform; solver grid/4 ->    | 0.108791% change                          | PASS       |
|                             | grid/8; RMS change <0.5%                   |                                           |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Band-limited white          | Synthetic calibrated PSD, 1 kHz-8 MHz;     | Expected 56.565007 uV; measured 56.632523 | PASS       |
|                             | 131072 samples; RMS error <12%             | uV; error 0.119361%                       |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Band-limited flicker        | Synthetic calibrated PSD, 1 kHz-8 MHz;     | Expected 59.957308 uV; measured 58.826164 | PASS       |
|                             | 131072 samples; RMS error <12%             | uV; error -1.886582%                      |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Combined model record       | Synthetic calibrated PSD, 1 kHz-8 MHz;     | Expected 104.587268 uV; measured          | PASS       |
|                             | 131072 samples; RMS error <12%             | 104.233904 uV; error -0.337866%           |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Spectral time correlation   | Independent S(f) cosine quadrature, lags   | Max normalized covariance error           | PASS       |
|                             | 1/10/100; covariance error <8% of total    | 1.112020%; flicker lag-1 correlation      |            |
|                             | variance                                   | 0.857364                                  |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| PSD coefficient calibration | 33 independent analytic PSD points;        | White 4e-16 V^2/Hz; K 4e-10 V^2; relative | PASS       |
|                             | recover white and K/f; reject poor fits    | RMS residual 1.77e-16                     |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Calibration import to C++   | CSV -> JSON/header -> native slice noise   | Imported profile RMS 104.233904 uV vs     | PASS       |
|                             | factory; synthetic data explicitly         | 104.587268 uV                             |            |
|                             | labelled                                   |                                           |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Measurement-method guards   | Known PSD/held-out frequencies; known      | 6 Python method tests pass                | PASS       |
|                             | probit width; provenance and double-count  |                                           |            |
|                             | guards; reject unresolved histograms       |                                           |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Noise-off ADC controls      | Codes stable at +/-sigma/4 around a        | 32 controls pass; all return the expected | PASS       |
|                             | freshly bracketed transition               | code                                      |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC threshold resolution    | Fresh T[128] bracket at each timestep;     | 10 ps: 1.220703 uV; 5 ps: 1.220703 uV;    | PASS       |
|                             | width <=sigma/32                           | limit 2.011689 uV                         |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Blind ADC noise recovery    | Four seeds, 5 input levels x32 each; 640   | 57.324896 uV RMS; error -10.950300%;      | PASS       |
|                             | noisy conversions; RMS recovery error      | input noise samples are hidden from the   |            |
|                             | <=25%                                      | estimator                                 |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC seed 7                  | 160 noisy conversions; individual fit and  | 52.917130 uV RMS; interval                | MEASURED   |
|                             | 95% profile interval                       | 41.184744-69.322096 uV                    |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC seed 19                 | 160 noisy conversions; individual fit and  | 50.464847 uV RMS; interval                | MEASURED   |
|                             | 95% profile interval                       | 38.829502-66.698200 uV                    |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC seed 31                 | 160 noisy conversions; individual fit and  | 66.276357 uV RMS; interval                | MEASURED   |
|                             | 95% profile interval                       | 52.324465-86.437305 uV                    |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC seed 43                 | 160 noisy conversions; individual fit and  | 56.061882 uV RMS; interval                | MEASURED   |
|                             | 95% profile interval                       | 43.601886-73.549858 uV                    |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC fitted RMS uncertainty  | 95% profile-likelihood interval; IID       | 50.585491-65.305253 uV RMS                | MEASURED   |
|                             | Gaussian sampling-noise assumption         |                                           |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC timestep replay         | Same seed-7 record at 10 ps and 5 ps;      | 5 ps: 52.917130 uV; paired width change   | PASS       |
|                             | refit center; width change <=15%           | 0.000000%; 160 noisy conversions          |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC electrical validity     | All calibration probes: acquisition/hold   | 864 conversions; max acquisition 0.000002 | PASS       |
|                             | <=20 mV; code error <=2; eight valid       | mV, hold 5.514797 mV; max code error 1;   |            |
|                             | stable decisions                           | zero invalid decisions                    |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| ADC decision timing         | Stable +/-25 ps window; no additional      | 6,912 valid decisions; minimum settling   | PASS       |
|                             | settling-margin budget                     | margin 1370.000 ps                        |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Technology noise input      | Bias-dependent device noise, sampled       | No calibrated full-slice PSD is supplied; | OPEN       |
|                             | transfer and aliasing, effective C,        | synthetic white/flicker coefficients      |            |
|                             | actual PSD and correlation data            | validate only the library interface       |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
| Product noise limit         | A specified input_noise_rms requirement    | Unspecified; recovery gates above         | OPEN       |
|                             |                                            | validate the model/tool, not a product    |            |
|                             |                                            | noise budget                              |            |
+-----------------------------+--------------------------------------------+-------------------------------------------+------------+
```

RMS recovery, moment/correlation checks and numerical tolerances above are
regression criteria. They do not establish a product noise limit, process noise
accuracy, statistical yield, or stochastic comparator metastability tails.
The 8-draw deterministic resistor-mismatch experiment remains a separate test.

The historical PHY requirements matrix is archived with the process-specific
[qualification history](IHPSG13G2/qualification_history.md).

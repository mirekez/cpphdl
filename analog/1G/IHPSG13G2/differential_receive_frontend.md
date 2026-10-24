# Differential receiver with physical reference and cold storage

`differential_receive_preamplifier : IC` preserves both polarities of one
resistor-loaded MOS differential amplifier. Two finite source followers drive
the positive and negative strings of `differential_receive_gain`. Its `VREF`
port accepts an independently declared reference IC; its output impedance,
charge and shared supply affect the receiver equations.

`differential_receive_frontend<Count> : IC` adds a bank of real SAR slices,
including transmission-gate samplers, resistor DACs, comparators, decision
latches and low-voltage output interfaces. `differential_mdi_receiver<Count>`
also adds the passive MDI input network. Ports are members; supply ports come
from `IC`. Electrical declarations add connected devices rather than executing
an ordered numerical assignment.

```cpp
// Inside a parent IC, declare actual children and connect their ports.
adc_range_reference reference;
differential_mdi_receiver<13> receiver;

void _assign() override {
    reference.Vdd = Vdd;
    reference.Vss = Vss;
    reference.elaborate();

    receiver.P = P;
    receiver.N = N;
    receiver.VREF = reference.OUT;
    receiver.Vdd = Vdd;
    receiver.Vss = Vss;
    receiver.Vlogic = Vlogic;
    receiver.EVALUATE = EVALUATE;
    receiver.CLEAR = CLEAR;
    for (unsigned bit = 0; bit < 5; ++bit)
        receiver.SELECT_N[bit] = SELECT_N[bit];
    for (unsigned slice = 0; slice < 13; ++slice) {
        receiver.TRACK[slice] = TRACK[slice];
        CORE_DECISION[slice] = receiver.CORE_DECISION[slice];
        for (unsigned bit = 0; bit < 8; ++bit)
            receiver.BIT_N[slice][bit] = BIT_N[slice][bit];
    }
    receiver.elaborate();
}
```

The parent supplies the signal ports shown above. Gain controls are active
low: 30/29/27/23/15 select positive differential gains 0.8/1.25/2/3.5/8.
`TRACK` is active high; `BIT_N` is an active-low DAC trial. `EVALUATE` clocks
comparators, `CLEAR` clears decision latches, and `CORE_DECISION` is the
physical 1.2 V decision output. A digital controller needs real voltage-domain
interfaces and a clock circuit to drive those electrical ports.

The cold slice sets all explicit initial capacitor voltages to zero. It has
1.5 pF hold storage, independent 20 µm/ng2 NMOS and 42 µm/ng5 PMOS
charge-cancellation dummies, no artificial sampler leakage return, and
charge-defined latch nodes. Main NMOS/PMOS widths are 37.5 µm/ng5 and
75 µm/ng10; all four sampler devices use 0.45 µm length. Its ADC comparator
initialization follows the selected preamplifier supply initialization.
The precharged characterization class remains available separately.

`ADCReceiveBankController<13> : Module` composes the minimum safe SAR bank with
sample calibration and an aligned slice tag. It launches at 250 MHz, tracks
for two clocks (8 ns), separates sampler opening from reference switching,
and returns raw/normalized data at offsets 13/21 clocks. Its sixteen-position
simulation interface pads positions 13..15 with constants; those positions
contain no converters. The independent digital test deliberately toggles their
unused comparison bits and checks that they cannot affect data.

The closed electrical fixture starts both rails and every explicit capacitor
voltage at zero. Tracking switches charge the storage during the rail ramp;
the controller opens them before conversion. It ramps the rails to
3.3/1.2 V, waits 16 µs, and exercises one slice, four slices or all thirteen
for two bursts. Physical MOS circuits feed the controller's comparator inputs.
Native runs use the analog/C++RTL engine; Verilog-A and IHP PSP runs use the
same controller through an event-checked XSPICE bridge. Every flow independently
checks the input network, sampler acquisition, hold, conversion, decision
stability, cadence, normalization and supply/headroom. C++RTL and Verilog-A
must additionally agree within one ADC count. PSP uses a different device law
and must pass the same absolute purpose checks.

The testbench uses finite line-source impedances, finite 50-ohm control
sources and explicit comparator clock/clear sources. Those are simulation
interfaces, not a physical transmitter, clock generator, AGC loop or reset/pad
power-sequencing implementation. The 4 ns changing PAM stimulus is an electrical
stress case; it does not establish cable compliance, echo cancellation or BER.

Headroom checks apply at captured inputs and throughout each conversion's
held interval. The PGA must stay between its achieved supply rails at every
solver state. Between-aperture PGA excursions outside the ADC conversion
window are recorded separately. Acquisition and hold errors remain limited
to 20 mV; code error remains limited to two counts. Negative tests reject
captured/held headroom violations and errors at the final decision.

Before the preamplifier change below, the thirteen-slice gain-4 native and
Verilog-A runs pass. Gain-4 PSP also passes the original-edge characterization;
gain-8 PSP fails acquisition, hold and code accuracy. Gain-8 native acquisition fails at 25.352 mV. The apparently passing
Verilog-A case used a 100 ps host control edge while the exported experiment
declared 2 fs. Independent copies with 2 fs edges fail acquisition at
23.256 mV, and timestep refinement to 2 ps changes that to 23.149 mV.
The maintained bridge now validates and uses the declared edge; earlier
passing waveforms do not qualify the revised stimulus. These diagnostics
leave the original inputs and failures intact. Thus
the thirteen-slice analog bank is not accepted as the default. See
[qualification status](qualification-status.md) for measurements and scope.
Generated compact and default Verilog-A files are under `generated/`; an
exported model is not evidence that its physical circuit has passed.

Build `analog_1g_ihp_differential_receive_conversion` and run
`analog.1g.ihp_differential_conversion_*` CTests. Full-bank tests retain complete
waveforms, actual controller/bridge events and source/tool/model hashes; they
can take hours. `tests/cold_sampler_sizing.py` is a separate retained-failure
diagnostic. A promising physical dimension must be declared in C++, exported
and qualified again before replacing the candidate.

## Reducing tracking lag and conversion kickback

The current candidate keeps 1.5 pF hold storage and the 20 µm PMOS dummy.
Its main sampling NMOS/PMOS are 32.5/65 µm, with five/seven fingers. Lower
channel resistance reduces the lag while the sampler tracks a moving input.

The conversion preamplifier uses two 25 µm NMOS inputs, each with five fingers.
Their load resistors are 1000 Ω, source degeneration is 100 Ω, and the shared
tail return is 300 Ω. Halving channel strength and doubling resistance
approximately preserves DC node voltages and gain while reducing current
and intrinsic gate charge. It slows settling, which must still pass the actual
SAR decision schedule. Explicit capacitors are unchanged.

The reason for this change is measured coupling. In the failed full-bank PSP
case, held voltage shifted persistently with each slice's own DAC trial even
after the common supply recovered. A slice with an unchanged trial barely
moved under the same supply impulse. The preamplifier connects both inputs
through its source network and gate capacitances; its DAC input can therefore
influence the supposedly held input. This is part of the connected circuit.

A separate one-slice process diagnostic of the combined candidate passes:
16.976 mV acquisition error, 10.531 mV hold variation and one-count maximum
code error. Reducing storage alone to 1 pF fails hold at 22.580 mV. The
earlier combined candidate was declared in C++ and bound to the same physical
widths/finger counts. Its maintained one-slice native, Verilog-A and PSP flows passed, and
generic codes agree exactly. Complete thirteen-slice Verilog-A tests pass
both gain settings. The restarted complete native tests also pass, with
all 26 codes matching Verilog-A at each gain. The complete gain-4 PSP run
passes at 12.805 mV acquisition, 11.835 mV hold variation and two-count
maximum code error. Gain 8 fails at 26.329 mV acquisition and three-count
code error; its 17.431 mV hold variation passes. The unchanged bounds are
20 mV and two counts. Thus the one-slice success does not qualify the
complete bank. The failed waveform is preserved for coupled-load diagnosis.

The experiment explicitly declares a 2 fs control edge independently of its
2 ps native step and 20 ps maximum SPICE step. This is a near-ideal stimulus;
it does not qualify a physical voltage translator's loaded edge. The live
bridge reads this value from `conditions.json` instead of supplying its own
100 ps default. Numerical refinement can now keep the stimulus unchanged.

## One-slice phase coverage limitation

The passing one-slice test captures only the first phase of the five-symbol
sequence. Its rising input has −21.263 mV tracking error just before opening;
the opening recovery partly cancels that error, giving −16.976 mV acquisition
error at +1 ns. It does not cover falling/high-target phases.

The failed thirteen-slice trace repeats its worst error every five launches.
For launch 1, target is 1.733402 V and the input is falling at approximately
−0.102 V/ns. Tracking error is +12.964 mV just before opening; acquisition
error becomes +26.329 mV at +1 ns. The captured slice's DAC remains zero until
+4 ns, so that acquisition failure precedes its first DAC transition. The
opening transient initially drives HELD downward, then recovers upward as the
complement/dummy network settles. Later DAC transitions add further held error.
These observations separate phase coverage from physical slice identity; they
do not yet select a corrected device size.

`build/1G/IHPSG13G2/tests/results/full_gain8_diagnostic/phase_decomposition.json`
records targets, signed tracking/opening/acquisition errors, first-DAC changes,
code errors, and source waveform hashes for every captured launch. Independent
retained-deck phase rotations in `out/receiver-phase-diagnostic/` keep the same
five-symbol levels, 100 ps line edges, 16 µs warmup, 2 fs control edges, devices,
and 20 mV/two-count limits. They characterize phase-dependent errors without
replacing the failed full-bank evidence.

The isolated baseline phase rotations reproduce the failure pattern:

| Phase | Signed acquisition error | Hold variation | Code error |
| --- | ---: | ---: | ---: |
| 0, rising | −16.976 mV | 10.531 mV | 1 |
| 1, falling/high | +25.858 mV | 16.780 mV | 3 |
| 2, falling | +21.604 mV | 10.252 mV | 2 |
| 3, rising/low | −16.207 mV | 11.481 mV | 1 |
| 4, falling | +21.095 mV | 9.597 mV | 2 |

Increasing only the PMOS dummy from 20 to 25 µm changes phases 0/1 to
−20.668/+20.115 mV: both fail acquisition. With that dummy, increasing main
NMOS/PMOS widths from 32.5/65 to 37.5/75 µm changes those errors to
−19.881/+21.299 mV. The stronger main switches reduce tracking lag but add
charge that worsens the falling phase. These are rejected diagnostics, not
selected receiver changes. The original devices and failed full-bank proof
remain intact. `out/receiver-phase-diagnostic/phase_decomposition.json` records
these cases, including explicit main/dummy sizes and the complement/DAC probes.

Further phase-0/1 diagnostics retain the original main switches. With the
25 µm dummy, reducing the PGA output return from 500 to 400 ohms gives
−21.055/+20.086 mV acquisition error and adds 0.720 mA (2.376 mW) of settled
supply current. Reducing hold storage to 1.4 pF instead gives
−19.650/+20.263 mV, with 17.713 mV falling-phase hold variation. Neither change
is selected. The interrupted 1.35 pF cases have no qualification verdict.

Doubling only the original complement inverter's PMOS/NMOS widths from
20/10 to 40/20 µm, while retaining the original 20 µm dummy and 1.5 pF hold
storage, gives −16.697/+25.496 mV and one/three-count errors. On phase 1 the
complement reaches 50% at 59.9 ps and 90% at 103.8 ps after the command, yet
opening recovery remains +10.032 mV. Faster complement drive alone therefore
does not correct acquisition. `out/receiver-phase-diagnostic/clock_path_report.json`
records achieved clock crossings and signed errors with waveform hashes.
A separate physical translator diagnostic retains the nominal capture target;
its added delay must satisfy the aperture requirement rather than silently
moving the target time. No production clock path has been changed.

The retained baseline `cascoded_level_shifter`/`clock_level_shifter` process
splice, loaded by the original sampler and supplied through a physical
25 pF replica bias, fails phase 1 at −72.791 mV acquisition and five-count
code error; hold variation is 17.265 mV. TRACK and its complement cross
50% at 665.5 and 331.4 ps after the command. The matching output inverter
topology therefore does not guarantee matched propagation through the
cross-coupled translator. All low-voltage inverter and pull-down Vgs/Vgd/Vds
pairs remain below the separate DAC unit's 1.65 V exposure bound (maximum
1.433 V). This is a rejected retained-deck diagnostic, not a declared or
qualified receiver clock circuit. The original nominal aperture and limits
remain unchanged.

Doubling that translator's latch and output-buffer widths, matching the
separate core-DAC candidate's sizing, reduces 50% phase skew from 334.1 to
235.0 ps but moves TRACK to 684.2 ps. Phase-1 acquisition worsens to
−76.362 mV, with 17.246 mV hold variation and five-count error. Full LV
terminal exposure remains within 1.65 V at 1.433 V. This variant is also
rejected for the unchanged nominal aperture. A physical sampling-clock design
must explicitly establish command launch, achieved gate arrival and aperture
variation before a translator-backed sampler can replace the baseline.
The diagnostic translator costs twelve MOS per slice instead of the existing
two-MOS complement inverter, plus 1.134 pF explicit storage per slice and
25 pF shared replica storage; these are circuit inventories, not placed areas.

Independent dummy sensitivities provide a smaller charge-cancellation option.
At fixed PMOS dummy 25 µm/ng5, increasing the NMOS dummy from 12.5 to
17.5 µm/ng2 changes phase-0/1 acquisition from −20.668/+20.115 to
−9.357/+29.020 mV. The measured N sensitivities are +2.262/+1.781 mV/µm;
the earlier P20→25 step gives −0.738/−1.149 mV/µm. Thus N injection is more
useful at the lower voltage while P correction is stronger at the higher
voltage. These are finite differences of realized geometries, including the
P dummy's legal finger change, rather than a guarantee of linear scaling.
`out/receiver-phase-diagnostic/dummy_sensitivity.json` records the measurements,
waveform hashes, conditioning and predictions.

A derived N20 µm/ng2, P40 µm/ng5 pair retains the original main switches,
local complement, 1.5 pF storage and nominal aperture. It passes phases 0/1
at −14.769/+16.168 mV, with 10.040/16.050 mV hold variation and zero/two-count
errors. This two-phase result does not qualify the circuit: phase 2 falls
through nearly the same voltage as rising phase 0, and must independently
satisfy the tracking/acquisition bounds. All-five phase coverage and complete
thirteen-slice qualification remain required before adoption.

The completed N20/P40 five-phase diagnostic is **failed**, despite four
passing phases. Signed acquisition is −14.769, +16.168, +21.677, −10.706 and
+16.909 mV; all hold and code checks pass. Phase 2 retains +18.545 mV of
pre-opening tracking lag and +3.026 mV opening recovery. This supports testing
stronger main switches with independently balanced dummies while preserving
the 1.5 pF hold capacitor. `out/receiver-phase-diagnostic/candidate_summary.json`
records complete phase coverage and the aggregate failed verdict.


## Maintained balanced-sampler candidate

The next candidate uses main N37.5/ng5 and P75/ng10 with dummy N20/ng2 and
P42/ng5, retaining the original local complement, 1.5 pF storage and 500-ohm
PGA return. All five independent retained-deck PSP phases pass:

| Phase | Signed acquisition error | Hold variation | Code error |
| --- | ---: | ---: | ---: |
| 0 | −15.381 mV | 9.988 mV | 1 |
| 1 | +15.212 mV | 15.964 mV | 2 |
| 2 | +17.479 mV | 9.701 mV | 2 |
| 3 | −12.418 mV | 11.137 mV | 1 |
| 4 | +14.375 mV | 9.065 mV | 1 |

The maintained C++ class exposes independent dummy strengths and declares
this geometry. The strict process mapper fixes its widths and finger counts;
fixture metadata declares both dummy parameters. The binding guard passes
real exports at 1/4/13 slices and 23 negative controls, including old individual
main/dummy strengths and an incorrect dummy gate connection.
`out/balanced-sampler-export-audit/checks.json` verifies that all 313 ordered
resistor/MOS/capacitor instance lines in the fresh official one-slice export
match the passing phase-0 diagnostic byte for byte. The old source snapshot
is `out/balanced-sampler-before-source-update/`; the exact change inventory is
`out/balanced-sampler-export-audit/source_changes.json`.

Fresh full-bank qualification passes under
`build/1G/IHPSG13G2/tests/results/balanced_sampler_13_gain{4,8}/`, using all
three flows and 26 launches per case. Acquisition and hold limits remain
20 mV, and absolute conversion error remains at most two counts.

| Gain | Flow | Maximum acquisition error | Maximum hold variation | Code error |
| --- | --- | ---: | ---: | ---: |
| 4 | Native | 1.732 mV | 3.025 mV | 0 |
| 4 | Verilog-A | 1.522 mV | 3.039 mV | 0 |
| 4 | PSP TT/27 C | 8.700 mV | 13.562 mV | 1 |
| 8 | Native | 16.988 mV | 6.064 mV | 2 |
| 8 | Verilog-A | 14.860 mV | 6.064 mV | 2 |
| 8 | PSP TT/27 C | 17.775 mV | 19.477 mV | 2 |

Both generic comparisons pass the unchanged one-count tolerance. Gain 4 has
26 identical codes; gain 8 has 20 identical codes and six one-count differences.
`out/balanced-sampler-full-qualification/independent_receipt_audit.json`
independently rehashes every source, artifact and waveform, recomputes all six
absolute contracts, and verifies all 26 samples and slice identities per case.
`status.json` in that directory records the six passing jobs and both passing
comparisons. Interrupted initial generic attempts retain their own receipts
and explicit resource-stagger notes, without a circuit verdict.

Gain 8's worst acquisition is launch 12/slice 12. Its worst hold is launch
16/slice 3, leaving only 0.523 mV hold margin. These are nominal-passive,
TT-process unit results with the declared 2 fs host controls. They do not
qualify process corners or a physical sampling-clock implementation; no
storage reduction or default-bank-size change follows from this result.
The next physical clock experiment must preserve the declared aperture and
include actual gate loading, overlapping acquisitions and startup/disable
behavior described in `receive_timing_budget.md`.

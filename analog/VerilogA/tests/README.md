# Verilog-A export and independent simulation tests

These tests belong to the analog CTest suite. They compile native C++ ICs, export
their actual connected relations to Verilog-A, compile that output with OpenVAF,
and simulate the compiled model in ngspice. No hand-written substitute transistor
netlist is used as the comparison reference.

The always-available `analog.veriloga.exporter` checks IC declaration lifecycle,
aliased bidirectional ports, context ownership, nested device/state retention,
interface validation and explicit rejection of simulation-only/digital behavior.
`analog.veriloga.cli` compiles a configured class through the root standalone
entry point, including paths with spaces and a compiler-checked non-IC rejection.
The normal package build additionally tests `cpphdl --verilog-a` as
`analog.veriloga.root_cli`.

With Python 3, `analog.veriloga.generated` regenerates the checked-in basic and
example catalog in a temporary directory and requires byte-identical models and
bench inputs. It covers all 21 basic model variants, five custom example IC classes,
17 complete example scenarios and the mixed receiver's explicitly bounded analog
partition. New example folders require an explicit export or documented limit.
With OpenVAF enabled, `analog.veriloga.generated_compile` additionally compiles all
44 generated Verilog-A files. The exporter unit test also checks that behavioral
models remain marked, full mixed exports fail, and a partition exposes every
omitted source terminal and its digital driver control.

With external tools enabled, the following comparisons run:

| Case | Physical experiment | Native coarse / fine step | Comparison |
|---|---|---|---|
| `divider` | 1.2 V source, 1 kOhm / 2 kOhm loaded divider | 100 / 50 ns | All sampled voltages, 2 uV limit |
| `rc` | 1 kOhm / 1 nF charge toward 1 V | 100 / 50 ns | Whole trajectory plus independent exponential, 11 mV fine native error limit |
| `rc_initial` | The same RC discharging from explicit 1 V initial charge | 100 / 50 ns | Trajectory, initial state and analytic exponential |
| `floating_charge` | 0.7 V initial capacitor difference across two resistive paths | 100 / 50 ns | Both terminal trajectories, 2 mV limit |
| `mos_regions` | NMOS/PMOS, forward/reverse terminal orientation, cutoff/saturation/triode: twelve channels | 100 / 50 ns | Twelve loaded voltages, 2 uV limit |
| `inverter` | Two MOS channels, finite load/storage, input step | 1 / 0.5 ns | Full continuous output, 20 mV limit |
| `controlled_resistor` | Fast inverter approximation, nonzero reference and external load below the local supply return | 1 / 0.5 ns | Both conductance polarities and reference-relative output, 10 mV limit |
| `driver` | Bench bit step driving a loaded capacitor; supply then changes | 0.5 / 0.25 ns | Finite drive, rail selection and charge continuity, 10 mV limit |
| `opamp` | Four-MOS amplifier corrected by a loaded feedback divider | 100 / 50 ns | OUT, feedback, tail and mirror trajectories, 40 mV limit |
| `ldo`, `ldo_dropout` | Original LDO device topology with external load and output charge; line/load disturbances during startup | 2 / 1 ns | OUT, CONTROL and feedback through 80 us, 20 mV limit |
| `pll` | XOR phase detector, passive filter and three-stage MOS VCO | 1 / 0.5 us | Late frequency, unwrapped phase and mean control against ngspice |
| `agc` | MOS attenuator, amplifier, detector and error integrator; 50/150/50 mV carrier stimulus | 5 / 2.5 us | Three positive-peak, control and detector windows against ngspice |

Each fine observable must meet its absolute error limit and approach the reference
under refinement: `fine_error <= 0.85 * coarse_error + numerical_floor`. The floor
is the larger of 2e-6 in the observable's units and 2% of its limit. Frequency/phase floors use
the corresponding units. This permits essentially identical DC results while
rejecting unresolved dynamic errors. RC reference traces must also agree with the
analytic solution within 0.1 mV.

PLL tolerances are 12 Hz, 0.01 cycles of mean phase and 10 mV mean control. Each
trajectory must independently show at least 40 late reference/output edges,
matching cycle counts, frequency within 1% and unwrapped phase excursion below
0.05 cycles. AGC limits are 5 mV positive peak, 60 mV mean control and 5 mV mean
detector voltage in each 15 ms window starting at 60, 140 and 220 ms. Neither a
missing oscillator nor an empty window can pass.

The LDO reference exposed numerical damping hidden by its long example's settled
voltage checks. Coarse microsecond steps are inadequate for precise startup ringing.
The comparison therefore uses 2/1 ns steps, line change at 20 us and switched load
at 40 us. The 80 us interval resolves startup and disturbances while retaining the
native solver's 100000-step limit. The ordinary example still tests its original
millisecond-scale settled line/load/dropout behavior. No device law was changed
to obtain agreement.

`analog.veriloga.detect_resistor_fault` changes only the generated RC resistor from
1 kOhm to 2 kOhm. OpenVAF compilation and ngspice execution must succeed; comparison
must return a quantitative `failed` result. An error/crash/missing output is rejected
as an invalid fault-detection outcome.

`RunComparison.cmake` removes stale case outputs, retains every stage's log and
checks every process exit. `compare.py` rejects truncated/nonfinite/nonmonotonic
waveforms and observation mismatches, normalizes SPICE to the native CSV names,
and writes native-time differences plus a quantitative JSON report. It checks
pointwise continuous trajectories for nonoscillatory circuits; loop metrics avoid
confusing numerical phase shift with a different circuit law. See
[artifact descriptions](../README.md#independent-simulation-and-common-outputs).

Configure `CPPHDL_ANALOG_VERILOGA_TESTS=ON` in CI so missing tools cannot silently
remove the independent comparison coverage. `AUTO` reports absence; exporter/CLI
checks remain available. The core analog tests require neither Python nor a circuit
simulator. See [toolchain setup](../toolchain.md) for reproducible local installation.

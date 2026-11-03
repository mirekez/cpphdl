# IHP SG13G2 native-first solver tests

This directory owns process-specific simulator coverage. Circuit acceptance
remains in `1G/IHPSG13G2/tests/`; full-chip verification remains in
`pdk/1G_repeater_IHPSG13G2/tests/`.

The native adapter now loads the actual installed IHP PSP OSDI model into the
C++RTL process. C++RTL owns network assembly, Newton iteration, BDF integration
and rollback. It never invokes ngspice. ngspice independently runs the original
PDK wrapper after the native circuit checks and refinement checks have passed.

The short process suite has 16 cases:

| Workload | Cases | Checks |
| --- | ---: | --- |
| LV/HV NMOS/PMOS DC loads | 12 | Five gate biases at TT/27 C, SS/125 C and FF/-40 C; inversion, cutoff, load KCL, current delivery |
| Intrinsic terminal-charge excursion | 4 | LV/HV NMOS/PMOS at TT; a complete rise/fall, recovery, native timestep refinement and all-sample comparison |

Separate ABI tests verify current/charge derivatives with respect to physical
potentials using finite differences, trial rollback, invalid parameter rejection
and a missing model. PSP's auxiliary small-signal noise-current coordinates are
not finite-differenced; these are constrained to zero in this deterministic
suite. Noise analysis itself is not implemented or qualified.

The transient window is 2 ns. Native steps are 1 ps and 0.5 ps, refined by 60
within 1 ps of source corners and by 8 within 8 ps, to resolve charge relaxation. Each trace
is limited to 6,000 points, each subprocess to 20 seconds, and the suite to 180
seconds. ngspice uses Gear integration with adaptive steps capped at 0.5 ps and
`trtol=.1`; its independently chosen time grid is retained. Comparison checks
all samples from both grids using linear interpolation, without extrapolation.
DC tolerance is 0.2 microvolt / 0.2 nanoampere; transient and refinement tolerance
is 30 microvolts. No failed run is accepted by relaxing its tolerance.

From `analog/`, with the PDK and ngspice already installed:

```sh
# Fetch only the pinned ABI header; downloads stay outside source control.
python3 tests/IHPSG13G2/prepare_sdk.py
cmake -S . -B build/behavior -DCMAKE_BUILD_TYPE=Release \
  -DCPPHDL_BUILD_ANALOG_TESTS=ON -DCPPHDL_ANALOG_OSDI=ON \
  -DCPPHDL_ANALOG_VERILOGA_TESTS=OFF
cmake --build build/behavior --target analog_ihp_process_bench -j2
cmake --build build/behavior --target analog_ihp_inverter_bench -j1
ctest --test-dir build/behavior -R '^analog\.ihp\.' --output-on-failure
```

`CPPHDL_ANALOG_IHP_PDK` selects the installed PDK directory.
`CPPHDL_ANALOG_REFERENCE_NGSPICE` selects the reference executable.
The PDK can be prepared by `pdk/.prepare_IHPSG13G2.sh`; this suite does not
silently download the PDK during testing. `cpphdl::osdi` is an optional CMake
target; ordinary analog/digital builds do not require the OSDI SDK.
The options also work through the parent cpphdl CMake entry point.

The latest report is `build/behavior/tests/IHPSG13G2/reference/checks.json`,
with a readable `timing.txt` beside it. Each invocation also has a unique
subdirectory containing input hashes, parameter bundles, decks, logs and
waveforms. Reports retain failure status and are not committed. Timings separate
native/refined/reference subprocess wall and CPU time; the native receipt also
records time inside its executable. Numerical thread pools are limited to one
thread for both subprocesses. Process wall time includes launch, loading,
solving, output and waiting, not just numerical integration. Refinement runs are
additional validation cost. These short checks do not demonstrate a 100x speedup.
Native PDK-card preprocessing is timed separately as `parameter_preparation_wall_s`
(once per shared corner/polarity bundle); ngspice parses the original PDK wrapper
inside its process. These process timings are not pure solver-kernel timings.

The separate `analog.ihp.inverter_native` and `analog.ihp.inverter_reference`
tests exercise the actual two-transistor `sg13g2_inv_1` schematic. The native
IC adds an output capacitor through inheritance; the transistor equations
remain coupled through the shared output. A checked CDL frontend supplies
the NMOS/PMOS geometry, and the reference keeps the original CDL topology
with only the instance-prefix change required for ngspice's PDK wrappers.
Unknown devices, connections and geometry options fail closed.

Eight cases cover a 21-point DC transfer and one rise/fall at each of:
TT/27 C/1.2 V with 5 fF and 55 fF, SS/125 C/1.08 V with 55 fF, and
FF/-40 C/1.32 V with 55 fF. Each engine independently checks inversion,
restored output levels, supplied stimulus, voltage envelope, complete
observation window and exactly two transitions. Native refinement runs before
ngspice. DC tolerance is 0.2 microvolt; all-sample transient and refinement
tolerance remains 30 microvolts.

These waveforms last 3 ns, with 0.25 ps coarse and 0.125 ps refined native
steps and additional refinement around PWL corners. The longer hold and
settling intervals are necessary for the slow-corner loaded cell; a 2 ns
window did not reach the specified 1% rail-restoration criterion. The 5 fF
case needs finer steps than the single-transistor load tests. Traces are
bounded to 32,000 points and 8 MiB; the same 20-second subprocess and
180-second suite limits apply. Fault tests in `test_inverter.py` reject
changed topology, stuck outputs, extra transitions, incomplete traces,
comparison errors, and running the reference after native failure.

Reports and timings are in
`build/behavior/tests/IHPSG13G2/inverter_reference/` (or `inverter_native/`).
The November 3 run passed all eight cases in 18.71 seconds including coarse
refinement, preprocessing and checks. Maximum DC difference was 35.30 nV;
maximum transient difference was 5.84 microvolts. The four refined native
transients took 7.65 seconds of subprocess wall time versus 3.55 seconds for
ngspice; native DC took 0.039 seconds versus 0.480 seconds. These timings
were measured while receiver routing was active and do not establish an
isolated throughput benchmark. This test qualifies the schematic inverter,
not the extracted clock gate, its 55 fF delivery contract or the whole ASIC.

`workloads.py` still inventories eleven larger 1G workload families. These
primitive-level process tests and coupled inverter establish prerequisites
for three families
(DC, charge and process/temperature), not equivalence of all original circuits.
`--require-complete` therefore still fails. Startup/bias networks, PAM-5 banks,
sampler/SAR feedback, clock feedback, storage, extracted hierarchy and pad
interfaces still need their native circuit implementations and comparisons.
Do not replace long qualification with this smoke profile or claim ASIC closure.

The adapter follows the [OSDI 0.3 interface specification](https://openvaf.github.io/osdi/osdi_v0p3.pdf).
Its SDK is fetched from a pinned OpenVAF revision with a SHA-256 check. Only
ABI 0.3 is accepted. Models requesting `$bound_step` are explicitly rejected
until integrated step-bound support is implemented. AC/noise, RF/NQS,
Monte Carlo and the complete MOSVAR wrapper are outside this tested scope.

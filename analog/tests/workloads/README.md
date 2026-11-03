# Short native-first simulation workloads

The C++RTL class backend produces the primary result. Optional ngspice runs
provide independent reference solutions; their output is never an input to the
native simulation. Both outputs must pass the same independent circuit checks
before they are compared. Each native run is also repeated at half its timestep.

`bench.cpp` implements IC classes with electrical ports and inherited device
behavior. `run.py` runs fourteen short cases across six workload families:

| Family | Short experiment | Independent check |
| --- | --- | --- |
| Linear DC | Five input voltages | Resistor divider ratio |
| Nonlinear DC | Five input voltages | Cubic current and KCL |
| Linear charge | 2 us decay | Exponential response |
| Nonlinear charge | 2 us current ramp and charging | Integrated current equals cubic stored charge plus resistor loss |
| Coupled RC | Two nodes and inter-node capacitance | Independent common/differential decay modes |
| Source breakpoints | Rise, hold and fall in 2 us | Analytic loaded-RC convolution |

The DC cases use an actual algebraic operating-point solve. Transient runs
produce about 2,001 coarse and 4,001 fine native points. The reference retains
every adaptive output point, capped at 10,000. The smoke suite limits each
process to 20 seconds, 512 MiB address space and 16 MiB output files, and the
entire runner to 180 seconds. A timeout is a failure. It never counts a partial
trace or silently increases the timestep to fit a budget.

Comparisons use both complete observation grids with linear interpolation
inside their overlap; no waveform values are discarded. The only excluded
endpoint is native t=0 when ngspice UIC starts just after zero. Both traces must
cover the full 2 us interval with at most 1.01 ns gaps. The absolute waveform
tolerance is 30 uV. Each independent circuit equation has its own 2e-5 bound
(volts for voltage equations; normalized units for the charge/KCL invariants).
These tolerances describe these fixtures, not arbitrary circuits.

The charge-form reference uses ngspice's documented `Q=` behavioral capacitor:
[ngspice manual, section 3.3.9](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).
Its current-driven test begins from a zero-charge DC state. It does not assume
that `.ic` on a behavioral capacitor's terminal initializes the hidden charge
state identically to the native backend.

```sh
cmake -S . -B build/behavior -DCPPHDL_BUILD_ANALOG_TESTS=ON -DBUILD_TESTING=ON
cmake --build build/behavior --target analog_workload_smoke -j 1
ctest --test-dir build/behavior -R '^analog.workloads\.' --output-on-failure
```

`native_smoke` does not require ngspice. `reference_smoke` is registered when
`CPPHDL_ANALOG_REFERENCE_NGSPICE` is available; it runs and checks native results
first, then runs the reference. No other ngspice suites are enabled by this
option. Results go under the build directory with unique run folders and a
receipt of timings, input/trace hashes, errors, unknowns and native linear-solve
counts. Compile time is excluded from the recorded simulation timings.

The checker regression rejects wrong voltages, missing endpoints, coarse
grids, nonfinite samples, duplicate timestamps and reference-only spikes.

Inventory existing registrations without executing their simulations:

```sh
python3 tests/workloads/inventory.py --build build --build build/behavior \
  --output build/ngspice-workloads.tsv
```

The tool counts distinct names across the selected configured builds, excludes
native-only flows, comparison-only flows, emit-only work and the ADC header/ABI
test, and retains CMake-wrapped ngspice tests. Its workload tags help discovery;
they are not model-equivalence or runtime evidence. Conditional registrations
absent from every supplied build are not counted.

This initial suite is not all-workload coverage. Inductive storage, MOS
switching, oscillator acquisition, digital feedback, ADC conversion, PAM-5,
channel noise and process models still need short class-backend representatives.
Existing legacy tests exercise many of those circuits, but the class backend
cannot claim their evidence. The [IHP catalog](../IHPSG13G2/README.md) explicitly
lists process-specific gaps. Full startup, lock/drift and ASIC qualification
remain separate tests with their original requirements.

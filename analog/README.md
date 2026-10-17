# C++HDL analog — intention-first electronic schemas

This directory is the analog part of the C++HDL package. Include
`<cpphdl_analog.h>` and use namespace `cpphdl::analog` (an alias of `::analog`).
Link the CMake target `cpphdl::analog`.

Checked-in Verilog-A models are in [basic/generated](basic/generated/README.md)
and [examples/generated](examples/generated/README.md), with regeneration commands,
complete simulation benches and explicit export limits for digital behavior.

Describe an IC as a C++ class, line by line. Add a device to deliver a function,
then another to correct or constrain it. Connect their purposes through wires;
the framework solves their combined physical behavior.

Simulation-only primitives use `sim_`: for example, `sim_voltage_source` supplies
an ideal stimulus, `sim_sense` interprets voltage thresholds, and `sim_store` updates
ideal bit state. These names expose simulation behavior without implying a physical
circuit implementation. Transistor and passive-device laws keep their device names.
See the [primitive naming contract](doc/specification.md#simulation-only-primitives).

```cpp
class Inverter : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;

    void _assign() override {
        auto pmos_vdd = pmos::from(Vdd); // Source-bound alias, using the inherited supply port.
        Y = pmos_vdd(A); // Enable the output when A is low.
        Vss = nmos(A, Y); // Correct/disable it by pulling the same Y low when A is high.
    }
};
```

`IC` supplies public `Vdd` and `Vss` electrical ports; every instance inherits its
own pair. Declare additional ports as members with `_PORT(...)`,
following C++HDL's style. Define `_assign()` for the line-by-line purposes; IC
classes have no user constructors or constructor wiring. Connect and elaborate:

```cpp
Inverter gate;
gate.A = A;
gate.Vdd = Vdd;
gate.Vss = Vss;
gate.elaborate(); // Calls _assign() once, through the common IC interface.
```

The base supplies simulation membership, identity, optional naming, and declaration
lifecycle checks. Supply ports bind explicitly; inheritance supplies no voltage or
global connection. Unused rails add no equations. All component models still enter
the same coupled solve.

`=` is a **purpose declaration that also handles assignment/connection** according
to the participating C++ types. These two declarations coexist and influence the
same Y. Its voltage emerges from both transistor currents, its load, charge and
supply network. Later declarations do not erase earlier physical devices.

An active variable can add behavior directly through its type:

```cpp
capacitance Y{10_pF, Vss}; // Connected node with charge storage, initially 0 V.
Y = pmos_vdd(A);
Vss = nmos(A, Y);

cmos<> gate; // A reusable IC with A, Y, Vdd and Vss member ports.
pmos pull_up{A, Vdd}; // A named variable of a device type.
```

The primary requirements are [purpose and mutual influence, #1](doc/requirements.md)
and [line-by-line C++ device declarations, #2](doc/requirements.md).
Read the [language guide](doc/language.md), [specification](doc/specification.md), [architecture](doc/architecture.md),
and [design decisions](doc/design-decisions.md). The local C++HDL
[Buffer](../examples/basic/Buffer.cpp), [Fifo](../examples/basic/Fifo.cpp),
and [Fifo2clk](../examples/cdc/Fifo2clk.cpp) examples informed typed class
composition and simultaneous next-state/commit semantics. The domains share only
the typed port selector. Analog `IC` has its own lifecycle and instance naming;
explicit bridge adapters coordinate native digital registers with physical time.
Read the [integration design](doc/cpphdl-integration.md).

## Build and run

A C++20 compiler and CMake 3.20+ are sufficient for the native library and examples.
C++HDL runtime headers are taken from the parent package. LLVM, Clang Tooling and
Verilator are not required for this standalone analog build. CTest runs the
regressions and validates the example outcomes.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
./build/analog_two_transistor_expression examples/two_transistor_expression/results
./build/tests/analog_semantics
./build/analog_mixed_signal examples/mixed_signal/results
ctest --test-dir build --output-on-failure -L analog
```

Each example also compiles directly, e.g.:

```bash
g++ -std=c++20 -O2 -Iinclude -I. -I../include examples/cmos_inverter/main.cpp -o /tmp/inverter
/tmp/inverter /tmp/inverter-results
```

`trace.csv` records SI voltages/times and exact bit values; `equations.txt` shows
elaborated models, terminal identities and source locations; `report.json` reports
sampled obligations, assumptions and model limitations. These files are generated
locally and are not tracked in Git. CTest writes its example artifacts under
`build/tests/example-results/`; direct runs use the output directory passed above.
Exit codes: 0 sampled pass, 1 failed/unverified/out of contract, 2 diagnosed model error.
The mutual influence example intentionally returns 1; the diagnostics example checks expected diagnostics and returns 0.

`cmake --build build --target check-analog` builds and runs the complete CTest suite.
From the parent package, the same
target is available in its build tree when `CPPHDL_BUILD_ANALOG=ON` (the default).
See [tests/README.md](tests/README.md) for both build modes and coverage.

Reusable circuit definitions live in [basic/](basic/README.md), including the
[LDO](basic/ldo.hpp), [PLL](basic/pll.hpp), [AGC](basic/agc.hpp),
[op-amp](basic/opamp.hpp), [regulator](basic/regulator.hpp),
[channel/level inverters](basic/inverter.hpp), divider,
RC filter, differential pair and current mirror. Include `<basic/basic.hpp>`
or an individual schema header.

## Commented C++ examples

| Folder | What to read | Expected behavior |
|---|---|---|
| [RC](examples/rc_filter/README.md) | `capacitance` output plus a resistor purpose | 0.992396 V at 5 us |
| [inverter](examples/cmos_inverter/README.md) | The requested two-purpose C++ class | High then low |
| [digital](examples/digital_pipeline/README.md) | Nested classes with `SUM = sim_add(...)`, `Q = sim_store(...)` | First edge Q=4, R=0 |
| [feedback](examples/opamp_feedback/README.md) | Transistor amplifier corrected by a loaded divider | About 0.994192 V |
| [regulation](examples/regulator/README.md) | Regulator IC defined from PMOS, error amplifier and feedback | Tracks a 1.2 V reference with finite-gain error |
| [LDO](examples/ldo/README.md) | PMOS pass device, feedback divider and error amplifier | Line/load recovery and headroom loss |
| [PLL](examples/pll/README.md) | MOS XOR, passive filter and current-starved ring VCO | 12 kHz frequency and phase lock under output loading |
| [AGC](examples/agc/README.md) | MOS attenuation, peak detector and capacitive error feedback | Peak recovery after threefold input changes; weak-input saturation |
| [diagnostics](examples/diagnostics/README.md) | Invalid models and uncertain interpretation | Five expected checks |
| [device types](examples/transistor_pair/README.md) | `pmos` and `nmos` as C++ variables | Same inverter trajectory |
| [model types](examples/class_variants/README.md) | `cmos<fast_model>` / `cmos<channel_model>` | Same purpose, different dynamics |
| [interaction](examples/mutual_influence/README.md) | External load defeats an IC and droops its neighbor's rail | Deliberate requirement failure |
| [Mixed native C++HDL](examples/mixed_signal/README.md) | RC input, native register pipeline, loaded analog output and return sensing | Preserved latency and capacitor continuity |
| [active variables](examples/two_transistor_expression/README.md) | `electrical Y` versus `capacitance Y` | Equivalent when storage is matched |

## Implemented scope

The header-only C++ library provides the IC base, typed member ports/wires, native device classes, model
policies, unit-checked physical parameters, nonlinear terminal relations, capacitors,
basic transistor circuit schemas, bit operations/registers and explicit domain bridges.
One coupled engine assembles modified nodal equations, solves them by damped Newton,
and integrates continuous state with backward Euler while coordinating digital events.

The semantic test executable checks 41 independent component, physical and event expectations,
with 12 additional native C++HDL interoperability checks. Coverage includes
repeated purposes retaining all devices, active-variable equivalence,
loading and shared supply interaction, finite-gain feedback, step refinement,
simultaneous register commits, unknowns, diagnostics and exact 64-bit values.

Accuracy depends on the selected models and integration step. Detailed types need
validation before claiming greater precision. Adaptive/sparse DAE integration,
root-localized thresholds, physical noise/power/damage, foundry devices, PLL/memory
libraries, formal refinement and mixed analog/digital RTL synthesis remain future work.
Native mixed-signal co-simulation is implemented; native outputs exposed to the
analog engine must be registered-state outputs. See the integration contract.
Simulated physical time does not imply hard real-time wall-clock execution.

## Code formatting

Follow [.clang-format](.clang-format): four-space indentation, spaces around
assignment operators, and a space after commas. Format changed C++ files with
`clang-format -i path/to/file.cpp` and keep documentation examples consistent.

## Verilog-A export and independent simulation

The [Verilog-A backend](VerilogA/README.md) elaborates native `IC` classes into
continuous branch contributions. Use the root `cpphdl --verilog-a` command or the
standalone `cpphdl-veriloga` driver. [C++ export examples](VerilogA/examples/) and
[ngspice/OpenVAF comparisons](VerilogA/tests/README.md) cover shared topology,
initial charge, MOS polarity/regions and LDO/PLL/AGC feedback. Tests compare native
step refinement with independently compiled/simulated Verilog-A and normalize both
to CSV. External simulator tests are optional by default and can be required in CI.

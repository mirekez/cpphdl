# Analog language

Analog describes connected electronic components using C++20 classes, member
ports and purpose declarations. The surface is ordinary C++; the framework
supplies device equations and evolves the connected network during simulation.

Include `<analog.hpp>` for the core, `<basic/basic.hpp>` for reusable schemas,
or `<cpphdl_analog.h>` for the native C++HDL simulation bridge and schemas.
The namespace is `analog`, also available as `cpphdl::analog`. Link the CMake
target `cpphdl::analog`. The [specification](specification.md) defines the detailed
semantics; [requirements](requirements.md) states the design requirements.

## Components and purpose declarations

An IC is a class derived from `IC`. Declare `_PORT(...)` members and put device
purposes in `_assign()`. The base supplies separate `Vdd` and `Vss` ports for
every instance. Bind ports by name, then call `elaborate()` once before running.

```cpp
#include <analog.hpp>

using namespace analog;
using namespace analog::literals;

class Inverter : public IC {
public:
    _PORT(electrical) A;
    _PORT(electrical) Y;

    void _assign() override {
        auto pmos_vdd = pmos::from(Vdd);
        Y = pmos_vdd(A); // Enable the output from Vdd when A is low.
        Vss = nmos(A, Y); // Pull the same output toward Vss when A is high.
    }
};

int main() {
    simulation sim;
    electrical A, Vdd, Vss;
    sim.reference(Vss);
    Vdd = sim_voltage_source(Vss, 1.2_V);
    A = sim_voltage_source(Vss, 0_V, 1_us, 1.2_V);

    Inverter gate;
    gate.A = A;
    gate.Vdd = Vdd;
    gate.Vss = Vss;
    gate.elaborate();
    gate.Y = capacitor(Vss, 10_pF); // External storage participates in the same network.

    sim.observe("Y", gate.Y, Vss);
    sim.assume_voltage("supply range", Vdd, Vss, 1.1_V, 1.3_V);
    sim.require_final_voltage("output low", gate.Y, Vss, 0_V, .05_V);
    sim.run(2_us, 5_ns);
    return sim.status() == "sampled_pass" ? 0 : 1;
}
```

`=` is a purpose declaration that also handles assignment/connection according
to the participating types. The two transistor purposes remain present together.
Connected loads, stored charge and supply impedances can change the achieved
output. The solver combines their terminal relations across all class boundaries.

`_assign()` creates the model graph during elaboration. The registered equations
continue to act throughout simulated time. Source order does not isolate one
device's behavior from the next device or from external components.

## Wires, active variables and hierarchy

`electrical` holds a shared node identity. An active type such as
`capacitance Y{10_pF, Vss}` adds charge storage to that node. Ports pass directly
to devices; they do not need a getter call.

Parent ICs contain child IC members, bind their ports and call each child's
`elaborate()`. Parents connect child supply ports explicitly. C++ classes and
templates select implementations: for example, `cmos<channel_model>` declares
MOS devices, while `cmos<fast_model>` uses a controlled-conductance approximation.
A model's accuracy must be established for the intended operating conditions.
See [class variants](../examples/class_variants/README.md).

The [basic schemas](../basic/README.md) define inverters, amplifiers and a
regulator through visible device connections. Their names do not select hidden
circuit topologies in the solver.

## Checks and observations

These are implemented C++ calls on a `simulation` object:

| Call | Meaning |
|---|---|
| `sim.reference(Vss)` | Choose the electrical zero reference |
| `sim.observe("Y", Y, Vss)` | Record a voltage difference |
| `sim.observe("Q", Q)` | Record a managed bit signal |
| `sim.assume_voltage(name, A, Vss, low, high)` | Check a sampled experiment envelope |
| `sim.require_final_voltage(name, Y, Vss, low, high)` | Check the final achieved voltage |
| `sim.require_final(name, Q, expected)` | Check a final managed bit value |
| `sim.require_trace(name, predicate)` | Check measured behavior over the completed trajectory |
| `sim.run(stop, step)` | Integrate the declared network and evaluate its checks |
| `sim.write(directory)` | Write generated equations, traces and a report |

Declare observations and checks before `run()`. Requirements evaluate the chosen
circuit and trajectory; they do not choose an implementation or force a node to
meet its goal. Reports distinguish sampled pass, failure, missing evidence and
violated assumptions. A passing sampled check does not prove all operating cases.

`require_trace` takes a testbench callback receiving `const std::vector<sample>&`.
Return `true` for a sampled pass, `false` for failure, or `std::nullopt` for missing
evidence. Sample values follow observation order. The callback runs after integration;
it adds no physical law or feedback. A violated global assumption suppresses it and
marks its requirement out of contract. See the commented
[LDO](../examples/ldo/main.cpp), [PLL](../examples/pll/main.cpp) and
[AGC](../examples/agc/main.cpp) for settling, edge/phase and peak measurements.

## Simulation-only behavior and digital boundaries

The `sim_` prefix marks ideal behavior or host simulation adapters. For example,
`sim_voltage_source` supplies an ideal stimulus, `sim_store` implements ideal bit
storage, and `sim_sense` interprets voltage thresholds. `sim_sense` contains no
physical sensing circuit. `sim_driver` supplies a behavioral finite-resistance
output path. The [primitive naming contract](specification.md#simulation-only-primitives)
lists these APIs.

Native C++HDL `Module`, `logic` and `reg` classes participate through explicit
`sim_to_cpphdl`, `sim_clock_cpphdl` and `sim_registered_output` adapters. All active
next states are computed before any state commits. The
[mixed-signal example](../examples/mixed_signal/README.md) demonstrates the
implemented bridge and its physical loading and charge-continuity checks.

## Execution and validation

Elaboration collects device laws and connections. The engine assembles shared
node-current equations, solves the nonlinear network and integrates capacitor
state with backward Euler by default or explicit [BDF2](integration.md).
Both require time-step refinement; neither provides local-error adaptivity.
Threshold views and digital events are
coordinated with that solve. Physical equations are generated from the declared
devices; there is no automatic topology synthesis from a function name.

From the package root:

```bash
cmake -S analog -B analog/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build analog/build -j 4
ctest --test-dir analog/build --output-on-failure -L analog
```

The [CTest suite](../tests/README.md) checks physical behavior, IC hierarchy,
simulation boundaries and native digital interoperability. The
[architecture](architecture.md) and [specification](specification.md) document
model and numerical limitations.

## Verilog-A output

`cpphdl --verilog-a source.hpp --top MyIC --ports Vdd,Vss,IN,OUT --output MyIC.va`
compiles and elaborates the native class before emitting its physical relations.
The [backend guide](../VerilogA/README.md) defines its supported subset, C++ API,
initial-condition contract and independent OpenVAF/ngspice comparisons. This path
adds no alternate electronic description syntax.

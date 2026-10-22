# Analog requirements

## 1. Purpose first; connected components influence one another

During component development, describe its purpose/intention briefly. The framework
provides its underlying formulas. Connected components influence and change one
another's behavior as simulated physical time evolves. A load, supply impedance,
or feedback connection can correct an IC's behavior, prevent its purpose from
being achieved, or drive it outside its operating envelope.

The integration/simulation engine combines all connected terminal relations,
conservation equations, continuous state, and discrete events. An intention never
forces the numerical result to satisfy itself. Functional failure and physical
damage are different; damage requires an explicit thermal/failure model.

Acceptance: [shared supply and switched loading](../examples/mutual_influence/README.md),
[feedback](../examples/opamp_feedback/README.md), and
[native mixed-domain composition](../examples/mixed_signal/README.md).

## 2. Declare ICs line by line in C++, adding devices and connecting them with wires

**The language is native C++.** Describe each IC as a C++ class derived from `IC`.
Inherit the public `Vdd` and `Vss` electrical ports from `IC`; declare additional
ports as `_PORT(...)` members, following C++HDL. Pass ports directly to
devices and digital purposes, without getter calls. Supply ports are per instance and
connect explicitly, including between parents and children. Inheritance adds no
voltage source or reference. Unused ports require no equations; physically used
floating rails remain errors. Put its purposes in
`_assign()` and connect member ports externally; do not define IC constructors. Add one device
to deliver a function; add another to correct, constrain, disable, or complement
that function. Declare wires and use them to connect the devices. Each line states
a purpose; together the devices determine actual behavior.

```cpp
electrical Y; // Declare the common output wire.
auto pmos_vdd = pmos::from(Vdd); // Alias for a PMOS supplied from Vdd.
Y = pmos_vdd(A); // First device enables the output when A is low.
Vss = nmos(A, Y); // Second disables it by pulling Y low when A is high.
```

`=` is a **purpose declaration that also handles assignment/connection**, with
semantics supplied by the participating C++ types. Both electrical declarations
remain present; the second does not erase the first. Their shared wire makes
their physical effects interact. `IC::elaborate()` calls `_assign()` once, before integration;
their physical laws remain active throughout simulation.

Types can express more than plain connectivity. An active variable such as
`capacitance Y{10_pF, Vss};` adds charge storage to its connected node. Device/IC
types such as `pmos`, `nmos`, and `cmos<>` are usable as C++ variables. Aliases,
classes, templates, and model policies support reuse and faster versus more detailed
versions. There is no separate text grammar or `&` composition operator.

Define functional blocks such as an inverter, op-amp or regulator as ordinary IC classes composed of
devices and feedback connections. Their names must not select a special solver
law. Put reusable basic schemas in [basic/](../basic/README.md). Define amplifiers
from visible device connections, including bias, compensation and load paths.
Any approximate model must expose its mathematical boundary and limitations.
See the [level inverter](../basic/level_inverter.hpp), [op-amp](../basic/opamp.hpp) and
[regulator](../basic/regulator.hpp). Fast approximations must also expose their
component connections; reusable elemental model laws belong in the framework.

Acceptance: [exact two-purpose form and active variables](../examples/two_transistor_expression/README.md),
[named device variables](../examples/transistor_pair/README.md), and
[model types](../examples/class_variants/README.md).

## 3. Purpose declarations, mathematical models, and constraints stay distinct

A device class declares its purpose and terminal contract. Its selected model
supplies equations. Separate assumptions and requirements describe the operating
envelope and acceptance bounds. A target is not evidence that the result reached
it. Reports must distinguish sampled pass, sampled fail, unknown/unverified, and
out of contract. Unsupported observations must not acquire invented precision.

## 4. Types and terminal identities determine meaning

`electrical` represents a shared bidirectional connection. Copy construction
references the same wire; an electrical purpose/connection preserves the target
wire identity and joins terminal relations. Digital `bits<W>` represents a
single-writer value; a purpose computes or stores its assigned value. Physical
quantities have distinct C++ types, so capacitance cannot be supplied as resistance.
Supply rails are real network nodes, and voltage always has a reference.

## 5. Class hierarchy must retain connected physical interaction

`IC` provides shared simulation membership, identity, optional naming, and a
guarded declaration lifecycle. Native IC objects are noncopyable and nonmovable;
wire handles remain copyable. An IC can contain devices and other IC instances. Class boundaries are never
simulation isolation boundaries. All connected terminal equations are assembled
together. Model policies such as `cmos<fast_model>` and `cmos<channel_model>` expose
the same port shape and purpose, but replacement accuracy requires evidence over
specified observations and an operating envelope. More detail does not prove
greater accuracy. Native C++ inheritance does not prove behavioral substitutability.

## 6. Continuous state and digital state require coordinated execution

The prototype integrates explicit capacitor charge, solves algebraic
feedback, and coordinates threshold views and digital edge events. Registers
compute next state from one old-state snapshot and commit simultaneously, following
the useful separation in C++HDL work/strobe examples. Known stimulus discontinuities
preserve continuous state. Unknown bits cannot silently select a physical rail.

## 7. Evidence must declare its scope and model limitations

A successful execution checks one sampled trajectory of selected models. It is
not a proof across every load, temperature, process corner or time. A numerical
failure is not proof of physical impossibility. Model provenance and source
locations accompany generated equations. Power, noise, jitter, metastability,
and damage need dedicated validated models and analyses.

## 8. Share C++HDL definitions and preserve mixed-domain state semantics

The framework belongs in the C++HDL package under `analog/`. Keep the intersection
minimal: `IC` and digital `Module` are independent bases, sharing only the typed
`_PORT` declaration selector. `IC` must not inherit the digital
module lifecycle. Analog ports retain shared-node identities; native digital ports
retain lazy value bindings. Select by port type, without a runtime mode flag or
implicit owner detection. Keep physical types distinct from digital values and
preserve the existing native `logic<W>`/`reg<T>` definitions. The analog core must
build without the digital port implementation or native cache clock.

Native modules use explicit known-value and clock boundaries. Compute all next
states before committing any state on an edge. Re-solve physical equations after
commit while preserving capacitor charge. Unknown analog interpretations must
not become invented two-state native inputs or clocks. Retain native module
ownership for the full run. Demonstrate this in [mixed_signal/main.cpp](../examples/mixed_signal/main.cpp)
and run regressions from [tests/](../tests/README.md). Native co-simulation does not
imply mixed-signal SystemVerilog synthesis.

## 9. Examples and explanations must match the executable language

Every current example uses commented C++ source (`main.cpp`), a folder explanation,
build/run instructions, equations, trace and report where applicable. Include
positive behavior and deliberate failures. Regression checks use independent
physical expectations and event semantics. Generate equations, traces and reports
locally; they are not required source files.

## 10. Simulation-only primitives must be explicit

Prefix primitives that supply ideal behavior, external stimuli, direct bit
evaluation or host co-simulation with `sim_`. Examples and generated equations
must use those names too, without unprefixed compatibility aliases. Device-law
primitives such as `pmos`, `nmos`, `resistor` and `capacitor` retain their names;
their model approximations remain documented separately. A `sim_sense` purpose
interprets a voltage and provides no physical receiver circuit. Compound hardware
functions still require explicit classes in `basic/`.

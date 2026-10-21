# Design decisions

The [language](language.md) uses native C++ classes derived from `IC`, C++HDL-style
member ports, `_assign()` for line-by-line purposes, and types that can add active
behavior. Purpose declarations build a circuit; constraints check its achieved
behavior; the integration engine computes its physical consequences.

## Purpose declarations are the surface

```cpp
electrical Y;
Y = pmos_vdd(A); // Enable output from the high rail.
Vss = nmos(A, Y); // Correct it by pulling low under the complementary condition.
```

The two purposes work together. Electrical `=` retains both device relations
through their shared wire. Digital `=` binds computation or storage. This is a
purpose declaration with assignment/connection semantics supplied by the
participating C++ types.

A model returning a terminal is the implementation mechanism; the public meaning
is the device's purpose on the named target. The signature must say which terminal
is targeted. PMOS enables its returned output from a supplied source. NMOS connects
its drain toward the targeted low rail. The mathematical model, not C++ source
ordering, determines currents and voltages after elaboration.

## A purpose does not guarantee success

Enabling Y toward Vdd can fail to deliver a high output under excessive load.
The NMOS and external circuits remain able to influence Y. An op-amp purpose can
be corrected by feedback yet retain finite-gain error. A regulator reference enters
an amplifier through an ordinary port and does not become an ideal source on its output. These are
conditional design purposes with measurable requirements, not ideal-value forcing.

Physical damage is not inferred from an endpoint failing. It needs additional
thermal, limit and failure-state models. MOS threshold is a device parameter;
logic conversion has a separately declared voltage interpretation.

## Native C++ types provide reuse and active behavior

`electrical` provides connection identity. `capacitance` adds charge storage.
`pmos` and `nmos` are device types, while `cmos<Model>` is an ordinary reusable IC
type whose body is explicit. A source-bound alias is a C++ callable. Repeated IC
declaration creates member ports; `_assign()` elaboration instantiates its device
purposes. ICs have no user constructors. Wire copies reference existing connectivity.
The base supplies public per-instance `Vdd` and `Vss` ports. Derived classes use
them directly and bind child supplies explicitly. Rails remain normal shared
network nodes; inheritance provides no power source. Unused rails introduce no
equations, allowing pure digital and passive classes to use the same base.
Model policies select fidelity with the same class usage pattern.

This follows the useful approach in C++HDL examples: classes, members, templates,
typed member ports and bindings, `_assign()`, and separate state advancement/commit. The simulation
engine must additionally solve coupled electrical equations. The runtime shares
only typed port declaration plumbing with C++HDL. Analog `IC` and digital `Module`
have independent lifecycles; the explicit adapter executes
native clocked modules inside the physical-time engine. Mixed-signal synthesis
is a separate capability.

## Do not infer a circuit from a technology name

The C++ `cmos<>` class contains a model policy whose channel implementation has
the two transistor purpose declarations. Its name is a library choice for this IC; CMOS technology
also supports many other circuit classes. They must have their own class bodies.

The fast realization is also a schema: `level_inverter` in
[basic/level_inverter.hpp](../basic/level_inverter.hpp) connects two complementary
`sim_controlled_resistor` paths. This simulation-only law describes one bidirectional
conductance controlled by a voltage difference. It does not contain rail selection
or inverter topology. The schema supplies those connections. This keeps the
approximation explicit while retaining load and supply coupling.

## More detail requires validation

The [basic schema library](../basic/README.md) defines the op-amp from an NMOS
input pair, a PMOS current mirror, explicit resistors, and a capacitor. The regulator
then composes that amplifier with a pass PMOS. Neither function has a backend
primitive. Supply current, finite drive, startup and load interaction follow from
those device connections. The reference in the experiment remains an explicit
ideal stimulus; it can later be replaced with a reference-generating schema.

The differential pair uses a resistor tail, so its bias varies with input common
mode. The mirror's weak return resistor establishes an off-state gate voltage.
Its current is retained in KCL, not inserted as a hidden numerical conductance.
The output shunt sets finite gain in a square-law model that omits channel-length
modulation. This is an explicit educational circuit, not a validated precision
amplifier or foundry topology. Its transistor parameters, headroom and capacitance
need validation before asserting an accuracy advantage over a simpler model.

A fast finite-drive model retains physical loading. A two-channel model captures
additional terminal dependence, but omits intrinsic charge, leakage and body effects.
Neither deserves an accuracy claim without a reference, envelope and error metric.
Class/template compatibility establishes shape, not quantitative substitutability.

## Numerical and event semantics must remain explicit

Electrical class boundaries cannot isolate nodes numerically. KCL and state laws
compose across all connected devices, so feedback and shared supplies are solved
together. Fixed-step backward Euler is a transparent minimal prototype, with
numerical damping and sampled threshold crossings. It is not a validated general
adaptive DAE backend. Numerical convergence is not a guarantee of trajectory accuracy.

Digital next values use one old-state snapshot and commit simultaneously. Unknown
values propagate; they cannot silently select a physical drive rail. Modulo-width
addition is an explicit library contract, including exact unsigned 64-bit behavior.
Sampled assumptions/requirements retain a clear evidence scope; a passing example
is not a universal proof of the designer's purpose.

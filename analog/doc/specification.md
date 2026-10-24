# C++HDL Analog C++ 0.3 — minimal specification

The surface language is C++20. There is no custom parser, separate grammar, or
model-composition operator. Include `<cpphdl_analog.h>` for the shared runtime,
bridge and schemas, or `<analog.hpp>` for the analog core and
`<basic/basic.hpp>` for reusable schemas. Namespace `cpphdl::analog` aliases
`::analog`; physical literals remain available through its `literals` namespace. This specification defines the library's
purpose declarations, typed variables, elaboration and execution semantics.

## 1. Class and purpose model

An IC is a C++ class derived from `IC`, with `_PORT(...)` members and an overridden
`_assign()` containing its device purposes. It has no user-defined constructor.
Default construction creates member handles; named member bindings establish the
interface before one-time elaboration.
A purpose can deliver a function, correct it, store a result or constrain a shared
connection. Each device supplies terminal laws and state through its model class.
The minimal inverter is:

```cpp
electrical Y;
auto pmos_vdd = pmos::from(Vdd);
Y = pmos_vdd(A);
Vss = nmos(A, Y);
```

The first device's purpose enables Y from Vdd when A is low. The second disables
that output by conducting Y toward Vss when A is high. Both use Y throughout
simulation. The meanings are conditional on the actual terminal voltages; they
are not ideal digital switches or unconditional voltage assignments.

`=` is a purpose declaration that also handles assignment/connection. Its semantic
effect is type-directed. For electrical purposes, it retains the LHS wire identity
and adds the selected device terminal relation to that wire. For digital purposes,
it binds the computation or storage that will assign the signal's value. Native
C++ calls and overload resolution perform elaboration; physical solving happens
later. Ordinary C++ numeric `=` retains ordinary C++ semantics outside these types.

The chosen C++ spelling does not prove the requested purpose succeeds. A load can
prevent Y reaching a valid high level. Separate constraints record acceptance
requirements. The inverter class contains its two device lines explicitly;
its name does not create hidden topology.

### Common IC base and member ports

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

Inverter gate;
gate.A = A;
gate.Vdd = Vdd;
gate.Vss = Vss;
gate.name("gate"); // Optional label; identity is unique within its simulation.
gate.elaborate();
```

`IC` declares public `_PORT(electrical) Vdd;` and `_PORT(electrical) Vss;`.
Derived classes inherit these ports and must not redeclare/shadow them. Each
instance has a distinct pair; there are no global or automatically connected rails.
A parent binds child rails explicitly, for example `child.Vdd = Vdd;` and
`child.Vss = Vss;`. Additional supply domains use additional named ports.

`IC` is abstract, noncopyable and nonmovable, with a virtual destructor and
`virtual void _assign() = 0`. `owner()`, `id()` and `name()` supply common identity
and context. Its protected default construction registers a descriptor in the
current simulation. No C++ object pointers are retained in that descriptor.
A component must be elaborated in its owning simulation before a run.

`elaborate()` is the lifecycle entry point: it dispatches `_assign()`, records
success, restores the enclosing component context, and makes repeat requests
idempotent. Recursive declaration or a previously failed declaration is rejected.
A simulation with an undeclared/failed IC cannot run. `_assign()` is the override
hook; call `elaborate()` externally and for child ICs so the lifecycle guard applies.
Do not invoke the hook directly to bypass that guard.

`_PORT(T)` uses the shared `cpphdl::port<T>` selector. Electrical/active ports
and analog-engine `bits<W>` select `analog::port<T>`. Native digital values
such as `cpphdl::logic<W>` retain `cpphdl::function_ref<T>`. An electrical port has the same shared
wire semantics as its underlying type. Pass ports directly, for example
`sim_controlled_resistor(Vdd, A, Vss, R, low, high)`. Primitive electrical parameters
bind to the underlying wire by ordinary C++ inheritance. This does not sample a
voltage or advance time. Active electrical ports retain their registered device
models. Digital templates likewise accept bit ports and deduce their widths.
Electrical member bindings join nodes, while bit member bindings declare value-copy
relations with the existing one-writer checks. Nested port connections preserve
old-state/simultaneous-commit semantics.

`_PORT(capacitance) Y{10_pF, Vss};` declares an active member port using the
inherited Vss. Base members initialize before derived members, so the reference
already exists when the active port registers its storage model. Member initializers configure
active types without IC constructor arguments. Primitive/model classes still use
ordinary C++ object initialization internally.

`IC` groups multiple typed ports rather than representing a single node. Its
inherited supply ports are ordinary wires; the base creates neither a voltage
source nor a ground reference. Unused rails may remain unbound in logical or
passive models. A rail used by a physical model, observation, or voltage constraint
must participate in the physical solve; a floating rail remains an error. Reading
an unused, unsolved rail after a run raises `E_UNVERIFIED`.
Its instance boundary never isolates physical equations. Device descriptors
created inside `_assign()` carry that IC's identity, and reports list registered
components. Intrinsic models from active-variable initializers and external loads
can exist outside that hook; they remain in the same global equation system.

The selector lives in the package's `cpphdl_port_traits.h`; both include orders
use the same macro. `IC` and native `Module` are independent bases, each owning
its instance label `__inst_name` and lifecycle.
`IC` does not inherit digital scheduling or port evaluation. Native digital ports retain
their lazy callable bindings while analog ports retain shared-node identities;
selection depends on the port type, not an inferred enclosing class or runtime flag.
Legacy `CPPHDL_STATIC` is rejected for analog schemas, which need per-instance ports.

## 2. Wires, terminal contracts, and identity

### Simulation-only primitives

The `sim_` prefix identifies ideal behavioral primitives and host integration.
It does not claim that other device models are exact or that analog RTL synthesis
is implemented. Transistor and passive-device laws retain their device names.
Compound physical functions belong in explicit `basic/` schemas. There are no
unprefixed aliases for the following simulation-only APIs:

| Primitive | Simulation behavior supplied |
|---|---|
| `sim_voltage_source` | Ideal constant or stepped voltage stimulus with unrestricted drive |
| `sim_buffered_track` | Ideal unloaded input buffer and finite clock-controlled output drive; storage must be explicit |
| `sim_pwl_voltage_source` | Prescribed continuous piecewise-linear bench voltage waveform |
| `sim_controlled_resistor` | Prescribed conductance transfer with ideal, unloaded control terminals |
| `sim_driver` | Bit-controlled resistance to actual rails, without an output-stage circuit |
| `sim_add`, `sim_invert` | Direct computation of managed bit values, without gate topology |
| `sim_store` | Ideal edge-triggered bit storage, without register circuitry |
| `sim_sense` | Threshold interpretation of solved voltage, without input loading or a sensing circuit |
| `sim_cpphdl_value`, `sim_to_cpphdl` | Read and bind managed signals to host-executed C++HDL values |
| `sim_clock_cpphdl`, `sim_registered_output` | Schedule native module callbacks and read committed state |

Operations on an explicit `simulation` object, such as `sim.clock()` and
`sim.observe()`, already declare their simulator scope. They keep their member
names. Wire types, units, device parameters and reusable IC class names are unchanged.

`electrical Y;` creates a node handle in the currently active `simulation`.
Construct the simulation before constructing circuit variables. All terminals in
a connection must belong to that simulation. `sim.reference(Vss)` identifies the
reference node with zero, fixing the voltage gauge. Rails remain ordinary wires.
No supply voltage is invented by a name such as Vdd.

A device class constructs its model and exposes a terminal handle for the purpose
on the LHS. The core contracts are:

| C++ expression | Purpose target on LHS | Other terminals / effect |
|---|---|---|
| `pmos(A, Vdd)` or `pmos_vdd(A)` | Drain/output | Gate A, source Vdd; enable from high rail at low A |
| `nmos(A, Y)` | Source/low-rail connection | Gate A, drain Y; pull Y toward the target at high A |
| `resistor(IN, R)` | Other end of the resistor | One end IN; conduct according to voltage difference |
| `sim_controlled_resistor(IN, CONTROL, REF, R, low, high, response)` | Other path terminal | Bidirectional conductance controlled by CONTROL relative to REF |
| `inductor(REF, L, initial_current)` | Other branch terminal | Store branch current; V = L dI/dt |
| `transconductor(REF, PLUS, MINUS, gm)` | Output terminal | I(output -> REF) = -gm V(PLUS, MINUS) |
| `diode(CATHODE, Ron, drop, Rleak)` | Anode | Piecewise-linear forward path and explicit reverse leakage |
| `sim_buffered_track(IN, CLK, REF, R, low, high, response)` | Buffered output | I(output -> REF) = G(CLK) V(output, IN); zero input current |
| `capacitor(Vss, C, initial)` | Charge-storage node | Other end Vss; retain differential voltage state |
| `sim_voltage_source(Vss, V)` | Source positive terminal | Negative/reference terminal Vss |
| `sim_driver(bit, Vdd, Vss, R)` | Driven output | Bit selects finite resistance to an actual rail |

The PMOS/NMOS call orientations intentionally follow the requested enable/disable
purposes. They are declared API contracts, not inferred from a technology label.
Underlying channel laws still depend on all terminal voltages and support reversed
channel current. The PMOS alias is an ordinary C++ callable with its source bound:
`auto pmos_vdd = pmos::from(Vdd, model);`.

Electrical copy construction references the same node: `electrical alias = Y;`.
Electrical purpose/assignment `alias = other_wire;` joins node identities rather
than rebinding and abandoning the existing network. There is no electrical
single-writer rule: many device purposes can influence the same node.

Device objects such as `pmos pull_up{A, Vdd};` represent one physical model and expose
its purpose terminal via conversion to an electrical handle. They are noncopyable;
wire handles remain copyable. Destroying a construction handle does not remove a
model: the simulation owns the elaborated descriptors until it is destroyed.
Adding a new purpose after integration starts is rejected as `E_ELABORATION`.

## 3. Active variables and reusable IC types

```cpp
capacitance Y{10_pF, Vss, 0_V};
Y = pmos_vdd(A);
Vss = nmos(A, Y);
```

`capacitance` is an active variable type: it contains a node with a capacitor model
to the supplied reference. The value argument has type `capacitance_value`.
Its initial voltage is explicit or defaults to 0 V. It remains usable anywhere an
electrical connection is expected. These exact same two transistor purposes now
interact with charge storage. An `electrical Y` plus `Y = capacitor(Vss, 10_pF)`
has equivalent equations at matched initialization.

An IC is also a C++ type. `cmos<> gate;` instantiates the library's
explicit two-device inverter class. Bind `gate.A`, `gate.Vdd` and `gate.Vss`,
then call `gate.elaborate()`; `gate.Y` is its output wire. Here `cmos` is a
specific library class name, not an implicit assertion that CMOS technology means
inversion. Its body and model policies are visible in the header.

`cmos<fast_model>` elaborates a `level_inverter` child with two explicit
controlled-resistor paths to the actual rails;
`cmos<channel_model>` uses the two square-law device purposes. Both remain electrical
and load-dependent. Their shape is compatible in C++; behavior/accuracy equivalence
requires observations, an envelope and error bounds. C++ inheritance or templates
alone provide no physical equivalence proof. Classes can contain other class
instances, as the nested digital pipeline demonstrates. There is no numerical
isolation at class boundaries.

Reusable schemas live in [basic/](../basic/README.md). The library includes
`cmos<Model>`, `level_inverter`, `rc_filter`, `resistor_divider`, `nmos_differential_pair`,
`pmos_current_mirror`, `opamp`, `non_inverting_amplifier`, and `regulator`.
Include an individual `<basic/name.hpp>` or the `<basic/basic.hpp>` umbrella.
All schemas use inherited rails, member ports, and `_assign()`.

`opamp` contains a two-NMOS input pair and a two-PMOS current mirror. The input
pair shares a source node returned through a tail resistor. Its PLUS branch
controls the mirror; the mirror supplies the MINUS/output branch. Increasing PLUS
relative to MINUS increases output charging current. The output also connects to
an explicit compensation capacitor and a shunt resistor. The mirror has an
explicit weak bias-return resistor to define its gate voltage when the input
branch is off. These elements all participate in the physical solve.

Gain and bandwidth are consequences of transistor parameters, bias, capacitance,
and loading. There is no op-amp gain parameter, artificial rail clamp, hidden state,
or amplifier-specific backend law. This is a single-stage educational OTA used
as an op-amp, with restricted common-mode range, voltage swing, and available
current. It is not a general precision or rail-to-rail amplifier. Explicit resistor
and capacitor values, initialization, and topology are documented in basic/.

`non_inverting_amplifier` connects an op-amp and a resistor divider. A 1:1 divider
intends a closed-loop gain of two, while its loading and finite transistor drive
cause error. `regulator` connects a PMOS pass device and an `opamp` child. It binds
`error.PLUS = OUT;`, `error.MINUS = REF;`, and `error.OUT = CONTROL;`, sharing its
rails with the child. Excess output raises the pass gate and reduces current.
REF is an electrical input, supplied by an explicit reference stimulus in the
example. Output storage and load are external. Both amplifier and pass-device
supply currents load the actual shared supply wire.

Neither class name selects a special equation. The regulator expands recursively
into five MOS devices, resistors, and a capacitor; the global engine knows only
their terminal relations. There is no numerical boundary between those classes.

## 4. Dimensions and defaults

Voltage, duration, resistance, capacitance value, and channel strength are distinct
C++ types with explicit SI representations. Constructors do not implicitly convert
between dimensions. Examples use `_V`, `_mV`, `_us`, `_ns`, `_ms`, `_Ohm`, `_kOhm`,
`_pF`, `_nF`, `_uF` and `_mA_per_V2`. Supplying `1_nF` as a resistance is a C++
compile error. R, C, beta and threshold are positive. Schema configuration uses
the same dimensioned types as elementary device models.

`mos_model` has shared educational defaults beta=1 mA/V² and threshold magnitude
0.35 V. Override with `mos_model{2_mA_per_V2, .4_V}` or a model policy. Defaults
are versioned framework model parameters, not hidden constants per IC. The selected
parameters and model source locations are written to the equation artifact.

## 5. Consequence laws and automatic coupled lowering

After port binding and `_assign()` elaboration, node aliases are canonicalized.
Nodes used by physical models, analog sensing, observations, or voltage constraints
enter the equation system. Every participating nonreference node gets one voltage
unknown and KCL equation. Completely unused wires, including inherited rails, have
no solved voltage and introduce no equation. Ideal sources and capacitors add branch
current unknowns. Capacitor initial conditions supply continuous state. Positive current from
a to b contributes +I at a and -I at b.

```text
R: I(a->b) = (Va-Vb)/R
C: I(a->b) = C*d(Va-Vb)/dt; initial Va-Vb is supplied
source: Va-Vb = prescribed waveform; branch current is solved
NMOS: ov=Vg-Vs-Vt; ds=Vd-Vs
      I=0 if ov<=0
      I=beta*(ov*ds-ds²/2) if 0<=ds<ov
      I=beta*ov²/2 otherwise
      swap drain/source and negate current if Vd<Vs
PMOS: apply the NMOS law to negated voltages, then negate current
```

MOS gate current is zero; intrinsic charge, body effects, leakage and foundry
corners are omitted. External capacitance supplies the example's charge storage.
Channel and rail-driver models stamp their terminal currents on both connected
rails. The amplifier schemas inherit these laws: their bias and load currents
are present in the shared supply network. Intrinsic transistor charge, body
junctions, channel-length modulation, thermal effects, and failure models remain
omitted, so topology alone does not establish physical accuracy.

The general controlled-resistor law uses
`f = clip((Vcontrol-Vreference-low)/(high-low), 0, 1)` and `I(a->b)=G*(Va-Vb)`.
A rising control response has `G=f/R`; a falling response has `G=(1-f)/R`.
R is positive and finite, and the finite control interval satisfies low < high.
Its control/reference terminals draw no current; the two path terminals retain
bidirectional electrical influence. There is no implicit source or ground.

The [level inverter](../basic/level_inverter.hpp) binds one falling path between
Vdd and Y and one rising path between Y and Vss. Both use A relative to Vss as the
control. The solver sees two controlled resistors, with ordinary shared-node KCL.
The resulting approximation preserves finite drive, supply loading and arbitrary
external coupling. It adds no intrinsic storage or special inverter equation.
Its interpolated conductances are an explicit approximation, not a measured
transistor transfer characteristic.

## 6. Digital purposes and hybrid composition

`bits<W>` is an unsigned value signal, 1<=W<=64. One purpose or external stimulus
assigns it. Examples: `SUM = sim_add(A, B);`, `Q = sim_store(D, CLK);`, `DATA = sim_sense(A, Vss, low, high);`, and `Y = sim_driver(Q, Vdd, Vss, R);`. Digital wire copy construction aliases
a signal; copy assignment declares a value-copy purpose. Addition wraps modulo
2^W, including exact unsigned 64-bit wrapping. Wider arithmetic is deferred.

`sim_store` initializes to 0 unless a third initial-value argument is supplied. It
captures on definite 0→1 clock transitions. Every active register computes from
one settled old-state snapshot; all updates commit simultaneously. A captured
unknown data value remains unknown. Ambiguous 0→unknown or unknown→1 clock changes
make capture unknown rather than inventing retention. Initial clock levels cause
no synthetic edge.

`sim_sense` gives 0 for V<=low, 1 for V>=high and unknown in between. Unknown propagates
through bit computations, is JSON null/empty CSV, and cannot select a physical
driver rail. A threshold view does not model metastability. Pure combinational
cycles are rejected; storage explicitly breaks them.

Native C++HDL registers can also participate. `sim_to_cpphdl(signal)` binds a known
analog-engine signal to a native input. `sim_clock_cpphdl(shared_module, CLK)` calls
its `_assign()`, retains ownership, resets it once at run initialization, and
advances `_work(false)` on definite rising clock edges. `sim_registered_output<W>`
exposes pure registered-state outputs as analog-engine signals. Unknown inputs or
clocks raise `E_UNKNOWN_CPPHDL`; widths above 64 are rejected at compilation.
On an edge, all native work and all internal store sampling precede every commit.
After commits, algebraic variables settle while capacitor voltages stay continuous.
Native output ports must expose stored state, not arbitrary combinational input
paths. The [integration contract](cpphdl-integration.md) defines lifecycle, cache
epochs, ownership and supported interfaces.

## 7. Simulation ordering and evidence

`sim.run(stop, step)` checks IC lifecycle states, freezes topology, and checks read bit signals/writers/dependencies,
seeds known sources and charge, and solves the initial algebraic system at fixed
continuous state. A run can be executed only once for an elaborated simulation.
Known source steps and clock events supplement the regular integration grid.

For each next timestamp, integrate to the left side with the selected method
(backward Euler by default, or explicit [BDF2](integration.md)), apply
right-sided source changes holding capacitor charge, settle physical/bit
views, commit all active register next states together, and settle generated event
deltas. Record the right-sided sample. Exact event timestamps take priority over
nearly equal floating-point grid points. Unknown analog crossings are sampled;
root localization is not implemented.

The dense solver uses finite-difference Jacobians, scaled partial pivoting and
damped Newton. KCL/current rows scale by 1mA and voltage/state rows by 1V;
normalized residual tolerance is 1e-8. Before backtracking, the trial voltage
change is bounded by one quarter of the current voltage scale (at least 1 V).
This limits enormous updates from off channels with weak physical returns;
it adds no conductance or state constraint. A rank check is performed even at zero
residual. Limits: 60 Newton iterations, 24 line-search/boundary/event iterations,
100000 regular steps and 100000 half-period events per clock. A singular or
nonconvergent solve is not proof of physical infeasibility.

`assume_voltage` checks a sampled global experiment envelope. Final voltage/bit
requirements check actual final values. Voltage comparisons allow 1e-8 V numerical
tolerance, assumptions 1e-9 V; bit comparisons are exact. A violated global
assumption marks quantitative requirements out of contract. Per-class conditional
assume/guarantee inference is a future extension of this native prototype.

`require_trace(name, predicate)` registers a completed-trajectory bench requirement.
The callback receives `const std::vector<sample>&` in observation order and returns
`std::optional<bool>`: true passes, false fails, and no value is unverified. It is
evaluated after integration and ordinary requirements, only when global assumptions
hold. Its status participates in the same aggregate result and report obligations.
Declare it before `run()`; an empty predicate is E_VALUE and a late declaration is
E_ELABORATION. It adds no network equations. Exceptions from a predicate propagate;
they are not converted into successful checks.

`sampled_pass`, `failed`, `unverified`, and `out_of_contract` are separate report
statuses. A run without explicit requirements establishes no quantitative purpose
guarantee. Fixed-step numerical residual convergence is not an accuracy bound on
a trajectory or a continuous-time proof. Left-side extrema can be missed.

## 8. Diagnostics and deferred guarantees

Runtime codes include E_CONTEXT, E_ELABORATION, E_TYPE, E_WIDTH, E_DRIVER, E_UNKNOWN_CPPHDL,
E_UNDERCONSTRAINED, E_DIGITAL_CYCLE, E_VALUE, E_INFEASIBLE, E_SINGULAR,
E_NONCONVERGENCE, E_UNKNOWN_PHYSICAL, E_HYBRID_CYCLE, E_EVENT_CYCLE,
E_OUT_OF_CONTRACT, E_UNVERIFIED and E_IO. Dimension/signature errors are C++
compile errors. E_INFEASIBLE currently detects an empty numeric interval, not a
general physical synthesis contradiction. Failed construction aborts elaboration;
it is not a transactional graph edit to resume from.

Noise, jitter, physical timing, setup/hold, area, power, thermal damage, PLL lock
and all-load stability require additional models and analyses. The implementation
provides simulation and sampled checks, not automatic technology synthesis or a
C++HDL/SystemVerilog export backend.

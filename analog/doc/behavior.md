# Class-controlled simulation

`include/behavior.hpp` is an opt-in, working prototype of the class-based
backend. `include/behavior_devices.hpp` supplies resistor, capacitor and
simulation voltage-source classes. It uses the existing `IC`, `_PORT(electrical)`
and electrical connection identities. The existing solver remains the default.

Each device owns its physical behavior. Its virtual `evaluate()` contributes
currents **F**, stored charges **Q**, and their analytic derivatives to the
connected network. The shared solver satisfies

```
F(x, t) + dQ(x, t)/dt = 0
```

at every accepted time. A connected load changes the equations and therefore
the solution of every affected device. An object cannot independently advance
its own terminal voltage and still guarantee simultaneous electrical coupling.
Its implementation can specialize evaluation, internal state and breakpoints;
the coordinator owns the shared accepted time.

## Classes and inheritance

| Base class | Responsibility | Implemented subclasses |
| --- | --- | --- |
| `device : IC` | Bind unknowns; evaluate local equations; report breakpoints | `two_terminal`, typed resistor/capacitor/source |
| `stateful_device : device` | Begin trial, commit and restore internal state | Exercised by the transaction regression |
| `linear_solver` | Solve the assembled linear system; detect rank loss | `dense_solver`, `backend_solver` adapter for existing sparse backends |
| `nonlinear_solver` | Find a consistent coupled solution | `damped_newton` |
| `integration` | Form derivative weights from accepted history | `backward_euler`, derived `bdf2` |
| `step_control` | Propose intervals, retry failures and receive acceptance | `fixed_step` with optional failure bisection |
| `transient_solver` | Coordinate the full run | `coupled_solver` |

All bases have virtual destructors (devices inherit `IC`'s destructor).
Specialized methods use `override`; overloads can be added with ordinary C++
`using Base::method` when an overload set must remain visible. Physical IC ports
remain class members, without constructor port arguments.

`traits<Derived> : traits<Base>` inherits model metadata. A model can use
`typed_device<Derived, Base>` to expose its traits through virtual dispatch.
For example:

```cpp
class nonlinear_storage;

template <> struct traits<nonlinear_storage> : traits<capacitor> {
    static constexpr std::string_view name = "nonlinear storage";
};

class nonlinear_storage : public typed_device<nonlinear_storage, capacitor> {
public:
    double alpha = 1;

    void evaluate(const evaluation& state, equations& out) override {
        const double v = state.value(p_) - state.value(n_);
        const double c = capacitance.value;
        out.branch(p_, n_, 0, c * (v + alpha * v * v * v),
                   0, c * (1 + 3 * alpha * v * v));
    }
};
```

This example belongs in namespace `analog::behavior`. It inherits ports, binding,
the capacitance parameter and charge capability. It overrides the physical law.
The solver integrates **Q(V)**; multiplying a voltage-dependent capacitance by
voltage would generally give a different physical model.

Solver strategies are replaceable through base-class pointers. A specialized
solver may inherit `coupled_solver`, replace its `linear`, `nonlinear` or
`integrator` member, and override `run()` if the coordination itself changes.
The linear result is independently checked against the assembled equations.

## Declaring and running a circuit

```cpp
analog::simulation context; // Existing IC and wire declaration context.
analog::behavior::resistor load;
analog::behavior::sim_pwl_voltage_source stimulus;

context.reference(load.N);
stimulus.N = load.N;
load.P = stimulus.P;
stimulus.points = {{0, 0}, {1e-9, 1}};
load.elaborate();
stimulus.elaborate();

analog::behavior::network network(context);
analog::behavior::coupled_solver solver;
analog::behavior::fixed_step steps(1e-11);
const auto trace = solver.run(network, 2e-9, steps);
```

The backend discovers live behavior objects in the declaration context,
including members of nested ICs. The devices and context must outlive the
network. All ICs must be elaborated before running. Changes to bound connections
or device membership are rejected. Legacy enum-based devices and digital
operations are explicitly rejected by this prototype; they are not silently
omitted. Existing `simulation::run()`, observations and requirements are not yet
forwarded to this opt-in backend. Read its returned trajectory using
`network.index(wire)`; `network.index(ground_wire)` returns the ground sentinel.

The initial solve is DC unless the caller supplies a complete vector of initial
unknowns. Supplying that vector starts from the specified state; it is not yet
an implementation of ngspice's complete `.ic`, `.nodeset` and UIC semantics.

## State and failures

`begin_step()` starts a transaction. `evaluate()` may run repeatedly during
Newton iteration and line search. `accept()` commits once after the coupled
solution succeeds. `reject()` restores state on a failed attempt. Stateful
devices inherit `stateful_device`, whose three lifecycle operations are pure
virtual. Evaluation must be repeatable for a trial solution and must not change
the last accepted history. Acceptance and rollback must not throw.

Charges, integration history and time advance only on acceptance. BDF2 restarts
at source breakpoints and uses accepted interval lengths after retries.
The current step controller bisects convergence failures; it does **not** yet
implement a local truncation error controller. Singular equations and invalid
models are errors rather than reasons to silently change the circuit.

## Current ngspice replacement status

The active reference is the complete extracted **transmitter** transient, not a
complete 1G PHY system. Auditing its selected model libraries identifies PSP
and MOSVAR compact models, parameter expressions, conditional subcircuits,
temperature, extracted RC, behavioral sources, UIC and ngspice's convergence
and transient error control. The audit includes declarations in conditional
branches and unused subcircuits, so its counts are lexical declarations rather
than elaborated instance counts.

The new class backend has **not** simulated that deck. The remaining work is:

1. A SPICE frontend preserving library selection, parameters, conditionals,
   model geometry, hierarchy and observation expressions.
2. An OSDI device subclass evaluating the **same compiled PSP/MOSVAR models**,
   with model parameters, temperature, internal unknowns, node collapse,
   terminal charges, derivatives and transactional model state. OSDI is a
   simulator-independent model interface; it does not provide a full solver.
   See the [OSDI specification](https://openvaf.semimod.de/osdi/osdi_v0p3.pdf).
3. Equivalent initialization, limiting, convergence and adaptive integration,
   plus the legacy analog and digital event adapters.
4. Independent electrical checks on both complete outputs, then all-sample
   comparison and measured runtime. A transistor-model approximation, cached
   reference output or ngspice wrapper does not establish native equivalence.

Bitwise output identity and 100× faster execution remain unachieved targets.
Even the same differential equations do not imply bitwise identity when the
integration grid or floating-point operation order changes. Numerical-tolerance
comparisons must be reported separately from strict identity.

## Tests and reference workflow

The [short workload suite](../tests/workloads/README.md) adds bounded,
native-first DC and transient checks against independent ngspice solutions.
The [IHP workload catalog](../tests/IHPSG13G2/README.md) tracks missing process
coverage. These short fixtures do not establish full PSP-deck replacement.

Configure with `CPPHDL_BUILD_ANALOG_TESTS=ON`, build `analog_behavior`, then run:

```sh
ctest --test-dir build/behavior -R 'analog.behavior|analog.1g.native_reference.contract' --output-on-failure
```

The C++ regression checks analytic loaded RC response, inherited nonlinear
charge, solver dispatch, breakpoint landing, rejected-step rollback, singular
networks, stale topology and rejection of an inaccurate custom linear solver.
The Python regression tests the reference comparison gate with deliberately
incorrect, truncated and nonfinite waveforms. Its synthetic runtimes test the
gate logic and are **not** performance measurements.

Audit a completed or still-running deck without opening its output stream:

```sh
python3 1G/tests/native_reference.py --output build/reference-audit.json audit \
    --deck /path/to/physical/bench.cir \
    --osdi /path/to/psp103.osdi --osdi /path/to/mosvar.osdi
```

The audit hashes the deck and recursively selected library dependencies. Supply
every model binary used by the run with `--osdi`. It records options, temperature
and observations, and explicitly reports that the full native deck is unsupported.

The strict waveform comparator checks every column and every retained row,
including repeated timestamps. It requires identical sample times, rejects
truncation and nonfinite values, and never interpolates or subsamples. A future
native runner must emit requested observations on the reference observation grid
without consuming the reference **values** as stimuli.

`native_reference.py system` consumes two completed runner receipts. Each needs
`engine` (`ngspice` or `cpphdl`), `status` (`complete`), `input_sha256` (all circuit
and model file paths and hashes), `columns` (starting with `time_s`), `stop_s`,
`numerical_contract`, `host`, `trace`, `trace_sha256`, `checks` (the same named
independent electrical checks, all true), and `timing` with `scope` equal to
`simulation_and_output`, `threads` equal to 1, and measured positive `wall_s`.
Paths in receipts must be absolute. Receipts are trusted runner records; this
comparator does not execute or attest the simulator executable.

The system gate requires matching inputs/settings/host/checks, full strict
output identity, and measured speedup of at least 100. Enable its CTest entry
with both `CPPHDL_1G_NGSPICE_RECEIPT` and `CPPHDL_1G_NATIVE_RECEIPT`. Without
completed runs the system gate is **not run**; passing the unit regressions
does not qualify the 1G replacement. The active ngspice trace must finish before
it can become a completed reference receipt.

The optional [IHP native process adapter](../basic/IHPSG13G2/README.md) now
loads PSP OSDI equations directly into this backend. Its [bounded process tests](../tests/IHPSG13G2/README.md)
check LV/HV N/P devices, corners, temperature, intrinsic charge and rollback.
These passing device-level comparisons are prerequisites; they do not satisfy
the full-system identity/performance gate above.

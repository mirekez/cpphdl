# Shared C++HDL and analog runtime

The package owns the analog framework under `analog/`. The original workspace
path may be a convenience symlink; build scripts and shared include discovery use
the physical package location. Source files, examples, basic schemas and tests
belong to this package. Build products and generated results stay outside Git;
builds use `analog/build/` or the root package's `build/analog/` directory.

## Which definitions are shared

| Header | Use |
|---|---|
| [cpphdl_port_traits.h](../../include/cpphdl_port_traits.h) | One `_PORT(T)` macro and typed port selection for both domains |
| [cpphdl_module.h](../../include/cpphdl_module.h) | Native digital `Module` lifecycle; used by the bridge, not the analog core |
| [cpphdl.h](../../include/cpphdl.h) | Native digital umbrella: value types, registers, lazy ports and binding macros |
| [cpphdl_logic.h](../../include/cpphdl_logic.h) | Native two-state `logic<W>` values used at the boundary |
| [cpphdl_reg.h](../../include/cpphdl_reg.h) | Native old-value/next-value registers and explicit strobe |
| [cpphdl_port.h](../../include/cpphdl_port.h) | Existing lazy `function_ref<T>` value ports, including their cache behavior |
| [cpphdl_analog.h](../../include/cpphdl_analog.h) | Public umbrella for digital runtime, analog core, native bridge and basic schemas |
| [cpphdl.hpp](../include/cpphdl.hpp) | Explicit native input, output and clock adapters |

The analog core includes only the lightweight shared port-trait header.
The bridge includes `cpphdl.h`; it reuses the existing native types rather than
copying `logic` or register implementations. CMake exports `cpphdl::runtime` and
`cpphdl::analog`; linking the latter supplies all three include roots and C++20.
The digital-only runtime remains usable in C++17.

`cpphdl::analog` aliases the `::analog` namespace, exposing its class
names and physical literals. Native `cpphdl::logic<W>` is a value; analog-domain
`bits<W>` is a managed signal identity that can also represent unknown. These are
different abstractions, so neither is silently substituted for the other.

## Shared syntax without losing port meaning

`_PORT(T)` expands through `cpphdl::port<T>`. Ordinary native digital types select
exactly the existing `cpphdl::function_ref<T>`. Analog connection types advertise
a connection tag and a port-rebinding alias; electrical types, active electrical
subclasses and analog-domain `bits<W>` select `analog::port<T>` instead.
The electrical marker is inherited by types such as `capacitance`, so active
member initialization and purpose declarations retain their intrinsic models.

Both header include orders are compiled in the regression suite. Native digital
ports still bind lazy callables and are read with their existing `port()` syntax.
Analog ports pass directly to primitives, for example `pmos(A, Vdd)`. Connecting
electrical ports joins node identities; connecting managed bit ports declares a
single-writer copy purpose. A lazy digital value binding cannot replace a physical
node, because external components must influence all terminal relations at once.

`IC` and native `Module` are independent bases, each owning its instance label
`__inst_name` and lifecycle. `IC` remains abstract and noncopyable, and its `name()`
setter supplies that label.
Analog `_assign()` creates the model graph once through `elaborate()`; native
`_assign()` binds digital ports. Analog components do not inherit `_strobe()`,
`_work_neg()` or native interface-wiring helpers. Legacy
`CPPHDL_STATIC` is rejected for analog components because their rails and devices
must belong to individual simulation instances.

### Minimal intersection and the proposed lazy marker

The shared core contract is typed port declaration plumbing.
Lazy callable evaluation belongs to native `function_ref<T>` ports; analog ports
hold network identities and register relations for the coupled solver. The bridge
coordinates the two execution models explicitly. Neither domain's base class
imposes its execution model on the other. Instance naming uses the same spelling
in both domains without introducing a shared base or changing native inheritance.

A module's inheritance can be inspected with `std::is_base_of_v<cpphdl::Module, T>`
when a template receives its type. The bridge already uses this check; it needs no
stored marker. It cannot make `_PORT(T)` discover its enclosing class: that macro
receives only `T`, and a nonstatic data member's type cannot use `decltype(this)`
to supply the missing owner. An inherited member alias is possible, but templates
with dependent bases then need an explicit alias import; unrelated interface
classes and existing standalone port declarations also need a policy. Literal
lexical nesting does not inherit a surrounding class's policy either.

For this prototype, selection therefore stays type-directed and independent of
the owner's base. No runtime flag is stored. Electrical ports retain node identity
even inside a native module, and a mixed wrapper uses explicit native modules and
adapters for lazy digital evaluation. An automatic owner-only policy would require
an additional declaration convention rather than an invisible behavior change.

## Native clocked bridge

The native module must derive from `cpphdl::Module`, provide `_work(bool reset)`
and `_strobe()`, and expose outputs that read committed register state only.
A module may contain its own native child hierarchy. Bind it as follows:

```cpp
module->D = sim_to_cpphdl(DATA);
sim_clock_cpphdl(module, CLK);
Q = sim_registered_output<1>(module, &NativeModule::Q);
Y = sim_driver(Q, Vdd, Vss, 100_Ohm);
```

`module` is a `std::shared_ptr<NativeModule>`. Bridge callbacks retain ownership
throughout simulation, including when the construction handle leaves scope.
`sim_to_cpphdl` captures the managed input handle by value and reads it at native work
time. It produces the native `logic<W>` value with the same width. The bridge
currently supports widths 1 through 64; it rejects a wider native output at compile
time and incompatible destination widths with `E_TYPE`.

`sim_clock_cpphdl` calls native `_assign()` once on registration. It registers the
module with the clock's owning simulation, checks context and duplicate registration,
and retains work/strobe callbacks. At run initialization all native `_work(true)`
calls execute before any native `_strobe()`. Reset code must establish state without
reading unknown input ports. Initial clock levels create no synthetic rising edge.
There is no separately scheduled asynchronous reset port in this first adapter.

At each simulation timestamp:

1. Integrate analog charge to the event's left side and apply right-side stimuli.
2. Settle analog equations, threshold interpretations and bit computations.
3. Detect clock edges and collect managed-store input values from the old state.
4. Run `_work(false)` for every active native module, with that same old state.
5. Commit all managed stores and native `_strobe()` calls.
6. Refresh native output values and settle the physical network while holding
   capacitor charge continuous. Repeat for generated clock-event deltas.
7. Record the right-sided sample and evaluate the declared experiment requirements.

Different native modules may use different clock signals. All modules active on
the same timestamp share the work-before-commit barrier, regardless of registration
order. The analog scheduler preserves its existing sampled threshold and event
limits. Native code is not called at every Newton iteration to update register state.

The bridge checks that registered outputs belong to a clocked module and that their
lazy ports are bound. `sim_registered_output` is a state-source contract: it is not an
adapter for arbitrary combinational functions of live inputs. Such an adapter needs
explicit dependency and cycle semantics. Native modules with named/multiple clock
methods need a dedicated lifecycle adapter; the current helper handles the ordinary
`_work(bool)`/`_strobe()` pair. Mixed analog/digital RTL synthesis is not implemented.

## Unknown values and native caches

Unknown analog interpretations stay unknown in managed signals. Reading one as a
native two-state input raises `E_UNKNOWN_CPPHDL`; an ambiguous native clock also
raises that diagnostic. Unknown never silently becomes zero. The circuit can supply
an explicit hysteresis, synchronization or validity design before crossing this
boundary; the bridge itself does not invent one.

As in existing C++HDL native programs, each bridge executable defines
`long _system_clock = 0;` once. The bridge advances this counter to invalidate lazy
port caches before work and registered-output reads. It is a cache epoch, not
physical time or a count of hardware clock edges. Models should base their behavior
on ports and register state; physical time belongs to the analog scheduler. The
adapter currently runs on one host thread.

## Reviewable example and regressions

[mixed_signal/main.cpp](../examples/mixed_signal/main.cpp) combines a loaded RC input, threshold sensing,
a native two-stage C++HDL pipeline, a finite output driver, output capacitance,
a load resistor and return sensing. Its assertions check native latency, capacitor
continuity and the loaded final voltage. The [example explanation](../examples/mixed_signal/README.md)
describes timing and the generated artifacts.

[tests/README.md](../tests/README.md) documents the 40 analog semantic checks,
12 native interoperability checks, digital C++17/include-order compatibility,
compile-rejection cases and reproducible experiment runner. The root C++HDL CTest
suite registers these tests with label `analog`; no analog test depends on LLVM
or a Verilog converter when built directly from `analog/`.

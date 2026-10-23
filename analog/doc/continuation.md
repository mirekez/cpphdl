# Continuing a transient without restarting the circuit

`run(stop, step)` initializes the circuit and native controllers once.
`advance_to(stop, step)` continues to a later **absolute** time. It preserves
capacitor/inductor state, hysteretic interpretations, stored bits, native
registers and previous clock levels. The graph stays frozen. A continuation
does not elaborate devices again or replay reset.

```cpp
sim.run(duration(50e-6), duration(1e-9));
check_and_save(sim.trace);
sim.discard_trace();

sim.advance_to(duration(100e-6), duration(1e-9));
check_and_save(sim.trace);
sim.discard_trace();
```

The example retains at most one window of observations. Sources and clock
phases still use their original absolute time coordinates. `elapsed()` returns
the last accepted time independently of the retained trace. Use a new
simulation object for a fresh run.

Each window keeps the 100,000 regular-step limit and allows at most 200,000
distinct scheduled events, including source and clock edges. Subdivision of
rejected Newton steps remains bounded. Changing the requested step or stopping
off its grid can add integration points; check time-step convergence when
comparing such trajectories. Aligned windows with the same step use the same
absolute grid and do not duplicate a boundary observation or clock edge.

Numerical assumptions retain their accumulated truth value and final
requirements retain their latest observation, using constant storage per
requirement. An earlier assumption violation remains visible after trace
disposal and later recovery of the voltage. Final-only requirements are
evaluated at each completed window; an early sampled failure does not prevent
continuation. A trace predicate needs its full trajectory, so `discard_trace()`
rejects that combination with `E_TRACE_HISTORY`.

Invalid timing arguments are rejected before advancing a healthy session. An
exception during integration marks its result unverified and prevents further
continuation: a native callback might already have changed state, so a failed
session cannot safely be resumed as if nothing happened.

The new `analog.continuation` contract compares segmented and uninterrupted
RC/native/store trajectories, including a clock and data edge on a boundary.
It also checks four milliseconds of a 125 MHz native counter in twenty bounded
windows, retained assumption violations, trace-history guards and failed
session handling. These checks and the seven accompanying solver, interop,
hysteresis and requirement regressions pass. Run them with:

```sh
cmake --build analog/build --target check-analog-transient
```

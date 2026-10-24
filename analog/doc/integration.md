# Transient integration

The coupled solver uses backward Euler by default. Select second-order
backward differentiation explicitly before starting a transient:

```cpp
analog::simulation simulation;
simulation.method = analog::integration_method::bdf2;
```

Both methods use the same physical graph, Kirchhoff equations, nonlinear
device laws, Newton acceptance checks and event processing. This option
changes the derivative approximation for capacitors and inductors. It does
not select a different transistor model or replace a component's equations
with its intended output.

For a current step `h`, preceding accepted step `k`, and `r = h / k`, BDF2 uses

```
a = (1 + 2 r) / (1 + r)
b = r² / (1 + r)
dx/dt ≈ [a (x[n] - x[n-1]) - b (x[n-1] - x[n-2])] / h
```

The capacitor state is its terminal voltage difference; the inductor state
is its branch current. Residuals and analytic Jacobians use the same leading
coefficient. Only accepted steps update the history. A failed Newton attempt
can subdivide the step without replacing accepted history.

The first step uses backward Euler. The solver also restarts with backward
Euler after a source step, PWL knot, scheduled digital event, register commit,
or sensed digital change that can switch a physical driver. A step larger
than twice its predecessor uses backward Euler. `advance_to()` preserves
accepted history, including event identity when a segment ends at an event.
Initial-charge constraints and instantaneous charge continuity still use the
existing held-state equations.

The requested step bounds the regular time grid. Known events add points,
and optional `minimum_step` permits Newton-failure subdivision. **Neither
method currently estimates local truncation error.** A converged Newton
solve establishes equation consistency for the chosen discretization; it
does not establish waveform accuracy. Refine the time step and compare
independent physical simulations before accepting a timing or voltage limit.
Analog threshold detection remains limited by the sampled event grid.

`tests/integration_bdf2.cpp` checks analytic RC, RL and LC responses, source
steps, triangular PWL excitation, continuation across event boundaries,
rejected-step recovery, sensed driver switching, nonlinear MOS discharge and
coupled capacitors. It compares finite-difference and analytic Jacobians,
checks absolute errors, and requires the expected improvement under step
refinement. Run it through CTest as `analog.integration_bdf2`; existing solver
regressions continue to exercise the default backward-Euler behavior.

# Optional sparse equation backend

The integration engine can pass its Jacobian coefficients to a different linear
solver without changing device equations, physical connections, Newton
acceptance, charge continuity or time integration. The default backend remains
the existing dense Gaussian elimination and has no additional dependency.

```cpp
#include <sparse.hpp>  // Requires Eigen 3 headers.

analog::simulation sim;
sim.analytic_jacobian = true;
analog::use_sparse_solver(sim);
// Declare and connect ICs, then run the ordinary simulation.
```

`sparse.hpp` is separate from `analog.hpp`: digital code and ordinary analog
users do not include Eigen. It configures `simulation::linear_backend` with
Eigen's sparse QR solver and column ordering. The engine stamps coefficients
directly into sparse entries when this backend is selected, so it does not
first allocate a dense matrix. Finite-difference Jacobians remain available as
an independent derivative path.

Repeated contributions to the same coefficient are summed before scaling.
Row normalization followed by twelve row/column equilibration rounds handles
the differing units of voltage and charge/current unknowns. Solutions are
converted back to the original variables; up to three correction solves refine
the result. The acceptance check still uses the original row-normalized
equations and its existing residual tolerance.
Zero rows and numerical rank deficiency fail even when the nonlinear residual
is already zero. The backend checks the computed linear residual and rejects
nonfinite or incomplete solutions. Symbolic analysis is reused only while the
complete sparsity pattern remains identical. It never solves connected ICs
independently or discards their shared supply/loading equations.

`sparse_strategy::newton_directions` optionally uses sparse LU for intermediate
Newton directions and falls back to QR if that solve fails its accuracy check.
Before the integration engine accepts any state, including a zero-residual
state, QR still verifies the Jacobian's numerical rank. The engine and backend
share the convergence threshold. The default strategy uses QR for every solve.
Both strategies reuse a numeric factorization only when its complete matrix
pattern and coefficients match exactly; failed factorizations invalidate it.

`sparse_strategy::certified_lu` also permits LU at an accepted Newton state
when an independent sufficient rank bound succeeds. Otherwise it uses QR.
[`sparse_rank.hpp`](../include/sparse_rank.hpp) constructs absolute triangular
recurrences bounding the one- and infinity-norms of the inverse factors. It
also measures the permuted factorization residual and includes conservative
floating-point margins. The singular-value perturbation bound then gives

```text
sigma_min(A) >= 1 / sqrt(inverse_one_bound * inverse_infinity_bound)
                - factorization_error_bound
```

The sufficient test requires this lower bound to exceed twice the unchanged
1e-12 QR pivot threshold. An inconclusive or overflowing bound does not imply
singularity: it requests QR. The backend still solves every requested direction
and checks/refines it against the original row-normalized equations. This
changes no Newton acceptance rule, physical equation or integration step.

An independent dense-SVD regression checks the bound on nonsymmetric,
permuted and badly scaled matrices, along with tiny pivots, stale factors and
overflow. The captured 1,357-unknown ADC matrix needs about 2.3 ms for LU plus
the bound, compared with 214 ms for QR on this workstation; the difficult
349-unknown fixture correctly requests QR. These are individual matrix timings.
All nine one/four/sixteen-slice 250 MS/s ADC flow checks and the three focused
solver tests pass with the production backend in 829 s. The native sixteen-slice
prototype and Verilog-A return exactly matching codes; the superseded long QR
run was intentionally stopped and is not counted as passing evidence.

An inaccurate finite direction reports `E_LINEAR_ACCURACY`. With a declared
`minimum_step`, the engine rejects that attempted interval, restores its prior
state and retries a half interval. It preserves the residual bound and fails
when subdivision is exhausted. Initial, structural-rank and malformed-backend
failures are not retried. The captured ADC system and independent RC fault
tests cover this distinction.

Tests compare dense and sparse trajectories, use independently known solutions
over twelve decades of independent row and column scaling, reject singular zero-residual systems, and
check coupled RC arrays through 128 branches against their closed-form pole.
Refinement reduces continuous-time error. MOS arrays check the independent DC
operating point under finite common supply impedance and cross-node charge.
The existing capacitor-mesh declaration-order, charge/impulse and both-Jacobian
contracts also run through the sparse backend. The two focused sparse CTests
pass in 54.22 s after equilibration/refinement. Before this sparse-only change,
all 332 selected analog CTests passed after the common stamping refactor.
This is selected-circuit evidence, not proof for arbitrary models.

```bash
cmake --build analog/build --target analog_sparse_solver analog_capacitor_loops_sparse
ctest --test-dir analog/build -R 'analog\.(sparse_solver|capacitor_loops)' --output-on-failure
```

CMake registers these optional regressions when Eigen 3 is available. Backward
Euler, fixed regular steps and optional Newton-failure subdivision remain the
integration method. Sparse factorization does not supply truncation-error
adaptivity or establish long-term clock/ADC accuracy. See
[Eigen's sparse QR contract](https://libeigen.gitlab.io/eigen/docs-nightly/classEigen_1_1SparseQR.html)
for its rank threshold and the requirement to check solution accuracy.

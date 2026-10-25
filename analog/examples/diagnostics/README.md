# C++ type checks and useful runtime diagnostics

This program verifies five expected outcomes and returns zero if all match:

| Case | Expected result | Meaning |
|---|---|---|
| Floating resistor network | E_SINGULAR | No reference path fixes the common voltage |
| Two bit stimuli on one signal | E_DRIVER | A bit value needs a single assigning purpose |
| Negative resistance | E_VALUE | The selected model's parameter is invalid |
| Unknown threshold driving a rail | E_UNKNOWN_PHYSICAL | Unknown cannot silently become a physical decision |
| 1 V supply under a 1.1–1.3 V assumption | out_of_contract | Numerical comparison does not repair a violated assumption |

The floating network is diagnosed even when a zero-valued guess gives zero
residual: Jacobian rank still matters. Numerical/model failures are different
from proving a design's physical purpose impossible.

Dimension errors are ordinary C++ compile errors. For example,
`Y = resistor(A, 1_nF);` cannot compile because the argument is capacitance rather
than resistance. Compile-time type checks in `tests/semantics.cpp` verify this
without introducing deliberately broken example targets.

Inspect `diagnostics.json`, `report.json`,
and the `out-of-contract trace`. The latter
also has its own equations and report. Failed elaborations have no valid trajectory.

## Build and run

From the `analog/` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON
cmake --build build --target analog_diagnostics -j 4
./build/analog_diagnostics examples/diagnostics/results
```

[main.cpp](main.cpp) contains the full commented C++ class/experiment source.

`ctest --test-dir build --output-on-failure -L analog` runs the CTest regressions.

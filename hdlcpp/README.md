# SystemVerilog Frontend

For a two-state integration with externally undriven bidirectional pads,
`HDLCPP_OUTPUT_ONLY_INOUTS=module.pad,module2.pad` explicitly lowers those ports
as outputs, retaining internal drive and readback. Undriven bits become zero.
This is an integration assumption, not general resolved-net simulation; do not
use it for ports driven from both sides. Apply the same setting to metadata and
conversion passes. The OpenC910 fixture uses it for its unconnected GPIO pads.

Ordinary conversion preserves `casez`/`casex` literal masks and first-match
priority, including wide and concatenated labels. `casez` does not treat `X` as
a wildcard. A bounded coverage proof makes the final branch unconditional only
when earlier labels plus that branch cover every two-state selector value.
`wildcard_cases.sh` checks 65,536 inputs against a C++ oracle and optionally
Verilator, native graph and optimize-combs.

Assignments in procedural `for` initializers and increments count as writes to
module-scoped loop counters. They must not become constant configuration in
`_assign()`: the final counter value remains observable after the loop. Locals
declared in the loop header stay local. `legacy_operators.sh` checks the counter
and indexed register writes against ordinary C++, Verilator and native graph.

Slang is pinned in `slang-revision.txt`. Do not build against a moving branch.
The setup script fetches that exact commit, checks it out detached, and refuses
to overwrite local changes. CMake rejects a different revision.

From the repository root, with CMake 3.28 or newer and a C++23 compiler:

```sh
bash hdlcpp/setup_slang.sh
cmake -S hdlcpp -B build/hdlcpp -DCMAKE_BUILD_TYPE=Release
cmake --build build/hdlcpp --target hdlcpp -j2
```

Alternatively, enable `CPPHDL_BUILD_HDLCPP` in the root CMake build to include
the frontend and its regressions in the main CTest suite.

The native-graph frontend supports conditional early returns in value functions,
including nested calls, `if`/`case` branches and statically bounded `for` loops.
A return captures the result on that control path; subsequent local computation
cannot replace it. Paths that reach the end use the function's implicit return
variable, which must be defined. Static-storage, output/ref argument and
observable-side-effect restrictions still apply. `hdlcpp_native_returns` checks
these cases against an independent C++ reference without Verilator.

# SystemVerilog Frontend

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

# Reusable RTL Examples

Each library example is a header containing the RTL model and its complete
testbench. A normal `#include` exposes only the model. Defining `MAINFILE`
also enables that header's `main_from_header(int, char**)` test entry point;
`SYNTHESIS` still excludes host test code. Dependencies temporarily suppress
`MAINFILE`, so including Fifo does not also enable Memory's tests.

The build supplies the ordinary C++ `main()` separately. For example:

```sh
c++ -std=c++23 -DMAINFILE -Iinclude -include lib/basic/Buffer.h \
    tests/ExampleMain.cpp -o build/buffer-test
build/buffer-test --noveril
```

The shared adapter simply returns `main_from_header(argc, argv)`. Test arguments,
exit codes, native checks and Verilator checks stay in the library header.
Do not compile headers as precompiled-header targets.

The external Makefile supports both flows:

```sh
make -C examples EXAMPLE=math/Maximum.h native rtl
make -C lib EXAMPLE=basic/Buffer.h native rtl
make -C lib EXAMPLE=math/Fp32Bits.h native rtl
```

Override `CXX`, `CPPHDL`, `VERILATOR`, or `PYTHON` to select your toolchain.
`CXXFLAGS`, `LDFLAGS`, and `LDLIBS` are passed to the native compiler.

CMake retains the existing target names. Basic storage, memory and shared math
headers now live in [lib/](../lib/README.md), including `basic_Buffer` and
`math_FpAdd`. CMake finds headers with a
`main_from_header` entry point and builds them through the same adapter, with
native and Verilator tests in CTest.

`CPPHDL_BUILD_EXAMPLES_TESTS=ON` (the default) enables these example builds.
`CPPHDL_BUILD_LIB_TESTS` separately controls the shared library's tests. Neither
option is required to include the headers in another project. For example:

```sh
cmake --build build --target math_Maximum
ctest --test-dir build -R '^math_Maximum_(native|verilator)$' --output-on-failure
```

To convert an example's test configuration, use the Makefile's `generate`
target. It passes `-DMAINFILE` to select any example wrapper or concrete
instance and supplies the clock names for FpAdd and Fifo2clk. Host tests remain
excluded by `SYNTHESIS`, except for templates whose concrete types are selected
by their testbench: those use `--no-synthesis-flag` during conversion. CMake
uses the same settings. In a larger design, include the headers normally and
convert your own top module with its concrete child instances.
When passing a header directly to gate synthesis, also use `-- -x c++` to
select C++ parsing explicitly.

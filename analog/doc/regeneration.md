# Regenerating excluded artifacts

Keep authored C++ models, generators, tests, setup scripts and dependency pins
in Git. Generated RTL/Verilog-A, downloaded PDKs/tools, build trees and simulation
outputs can be recreated. Existing historical generated files may still be
tracked; regenerate after changing sources or checking out a source checkpoint.

## Source catalogs

From the cpphdl repository root, with the compiler dependencies described in
the root README installed:

```sh
cmake -S . -B build -DCPPHDL_BUILD_ANALOG=ON
cmake --build build --target generate-analog --parallel 1
```

`generate-analog` builds the model exporters and examples, builds the root
converter when it belongs to this CMake project, and regenerates:

| Output | Authored generator |
| --- | --- |
| `analog/basic/generated/` | `VerilogA/generate_models.cpp` and `VerilogA/generate.py` |
| `analog/examples/generated/` | Example `main.cpp` files and `VerilogA/generate.py` |
| `analog/1G/generated/*.sv`, `*.f` | `1G/generate_rtl.py` and C++RTL module entry points |
| `analog/1G/generated/*.va`, `analog/1G/IHPSG13G2/generated/*.va` | `1G/generate.cpp` |
| `analog/10G/generated/*.va` | `10G/generate.cpp` |

The RTL generator creates a complete file list for each clock-specialized
hierarchy and removes obsolete catalog names. It rejects unrelated RTL files
in its destination. Verilog-A export elaborates the real C++ components; it
does not need OpenVAF, ngspice or a PDK.

A standalone analog build needs an existing converter supplied explicitly:

```sh
cmake -S analog -B analog/build \
  -DCPPHDL_1G_CONVERTER="$PWD/build/cpphdl"
cmake --build analog/build --target generate-analog --parallel 1
```

To verify regeneration without touching source-tree catalogs or running
simulations, use a separate build and an initially empty output directory:

```sh
cmake -S analog -B analog/build/regeneration \
  -DBUILD_TESTING=OFF -DCPPHDL_BUILD_ANALOG_TESTS=OFF \
  -DCPPHDL_1G_CONVERTER="$PWD/build/cpphdl" \
  -DCPPHDL_ANALOG_GENERATED_ROOT="$PWD/analog/build/regeneration/catalog"
cmake --build analog/build/regeneration --target generate-analog --parallel 1
```

The normal `check-analog`, `check-veriloga` and `check-1g` build targets regenerate
the catalogs they check before invoking CTest. Direct `ctest` commands do not
build anything: run the generation target first. Generation-comparison tests
use `CPPHDL_ANALOG_GENERATED_ROOT` when an alternate destination is selected.
Standalone analog checks still work without the digital converter: RTL catalog
checks and their generation dependency are enabled when the converter is
available. The explicit `generate-analog` target requires it.

## Downloaded dependencies

Run these commands from the repository root as needed. Setup is explicit;
ordinary catalog generation does not download tools or run physical verification.

| Dependency | Reproduction entry point |
| --- | --- |
| Root C++RTL converter and runtime | Root `CMakeLists.txt`, `include/`, and root README compiler setup |
| OpenVAF and ngspice, including XSPICE headers | [Pinned toolchain recipe](../VerilogA/toolchain.md) |
| Public IHP SG13G2 and compiled PSP models | `bash analog/pdk/.prepare_IHPSG13G2.sh --openvaf /path/to/openvaf --ngspice /path/to/ngspice` |
| OpenROAD, KLayout, Yosys, Icarus, sv2v and RC extraction rules | `python3 analog/pdk/1G_repeater_IHPSG13G2/prepare_tools.py` |
| Magic with corrected sidewall extraction | `python3 analog/1G/IHPSG13G2/prepare_magic.py --full-sidewall --destination analog/out/magic-sidewall` |
| FasterCap binary | `python3 analog/1G/IHPSG13G2/prepare_fastercap.py --destination analog/out/fastercap-toolchain` |
| FasterCap source build with the matrix-offset correction | `python3 analog/1G/IHPSG13G2/prepare_fastercap_source.py --destination analog/out/fastercap-source-build` |
| KLayout-PEX Python environment | `python3 analog/1G/IHPSG13G2/prepare_kpex.py --destination analog/out/kpex-toolchain` |

The recipes retain revision/hash pins and reject mismatches. The PDK has its own
upstream Python requirements; `prepare_tools.py` uses the committed
`tool_packages.tsv`, and KLayout-PEX uses `kpex-requirements.txt`. Host packages,
network availability and the documented platform requirements still apply.

## Simulation and physical outputs

CMake rebuilds the benches and exporters. The individual CTest/physical-flow
scripts regenerate vectors, mapped netlists, host adapter libraries, simulator
decks, waveforms, reports, GDSII and extracted parasitics. Follow
[physical flow setup](../pdk/1G_repeater_IHPSG13G2/README.md) and
[extraction setup](../1G/IHPSG13G2/analog-extraction.md) for their stage inputs.

Qualification receipts are evidence from actual runs: regenerate their upstream
tests in order, rather than replacing missing receipts or copying a pass flag.
A regenerated artifact is not automatically a passing artifact. Full-chip
qualification remains incomplete, as recorded in
[the current status](../1G/IHPSG13G2/qualification-status.md). Historical failed
experiments and logs are not required to build the design, and no promise of
byte-identical results across different tool versions or hosts is made.

# CVA6 integer matrix multiply benchmark

Run from this directory on Linux:

```bash
./.build_all.sh
```

`./.build_all.sh` runs `./.build_cva6.sh` and then `./.compare.sh`. The build
fetches the pinned CVA6 revision and its simulation submodules into `./cva6/`
and builds the bare-metal RV32 program and upstream Verilator testbench.
The comparison converts the same RTL with HDLCPP and runs Verilator and the
C++HDL native graph with one and four simulation threads. The benchmark uses
`cv32a6_imac_sv32` and one 32×32 integer matrix multiply (32,768 MACs).

## Prerequisites

- Built `cpphdl` and `hdlcpp` from this repository. A Release build speeds
  up SystemVerilog conversion. [Project build instructions](../../../README.md#enable-hdlcpp-build)
- Clang with C++23 support, an RV32-capable bare-metal RISC-V GCC/G++ toolchain,
  Bash 5, Python 3, GNU make, GNU awk, GNU coreutils, Git, patch, gzip and util-linux
  (`flock`, `setsid` and `taskset`). Building the bundled FESVR also needs
  a host C compiler, binutils and the device-tree compiler (`dtc`).

The scripts look for Verilator **5.050**. If it is unavailable, they fetch and
build that release in the work root using Autoconf, GCC/G++, Flex, Bison and
Perl. An explicit `VERILATOR` points to an existing installation.

On the first run, Git needs network access. The pinned CVA6 clone contains
the Spike sources used to build the FESVR static library locally. Build and run
artifacts go under `/tmp/cva6-bash-<uid>-<fixture-id>`, where the fixture ID
depends on this directory's path. Use Linux paths without whitespace for the
upstream Makefile.

| Environment variable | Default |
| --- | --- |
| `CVA6_SRC` | `./cva6` relative to this fixture |
| `CVA6_WORK_ROOT` | Per-fixture directory under `/tmp` |
| `RISCV` | `$HOME/riscv` |
| `CVA6_CROSS_COMPILE` | Find `riscv32-unknown-elf-` or `riscv64-unknown-elf-` under `$RISCV/bin` or on `PATH` |
| `SPIKE_INSTALL_DIR` | FESVR built in the work root. An override must contain `include/fesvr/dtm.h` and `lib/libfesvr.a` |
| `HDLCPP` | `build/hdlcpp/Release/hdlcpp`, `build/hdlcpp/hdlcpp`, `hdlcpp/build/hdlcpp`, or `PATH` |
| `CPPHDL` | `build/Release/cpphdl`, `build/cpphdl`, or `PATH` |
| `CPPHDL_CXX` | `clang++` on `PATH`, with a fallback to the project's `.conda/bin` |
| `VERILATOR` | Verilator 5.050 on `PATH`, in the project's `.conda/bin`, or built under the work root |
| `CVA6_CPU`, `CVA6_CPUS4` | First one or up to four CPUs allowed by the caller's CPU affinity |

A cross-compiler prefix can be a name on `PATH` or an absolute path such as
`/opt/riscv/bin/riscv64-unknown-elf-`. The compiler must support
`-march=rv32imac_zicsr_zifencei -mabi=ilp32`.

## Build and run separately

```bash
./.build_cva6.sh
./.build_cva6_cpphdl-native-graph.sh
./.run_cva6_verilator.sh
./.run_cva6_cpphdl-native-graph.sh
```

The first entry point prepares the source, workload, FESVR and Verilator
baseline. The native graph build generates the shared HDLCPP conversion on
demand. `./.build_cva6_hdlcpp.sh` can also build that conversion separately.
Use the same overrides for each command.

`./.compare.sh --all` adds C++HDL with `--optimize-combs` and default C++HDL.
The standalone `.build_cva6_*.sh` and `.run_cva6_*.sh` entry points are also
available. `./.build_all.sh` forwards comparison flags.
`./.compare.sh --rebuild` regenerates the conversion and starts with empty
simulator object caches. Compiler jobs are serial. The four-thread mode
selection applies to simulation.

## Reading the results

The comparison writes `comparison.tsv` and `conversion-time.txt` under the
work root. The build times for individual simulators include backend lowering,
compilation and linking. The shared HDLCPP conversion time is recorded once.
Cached builds retain their recorded times, and a resumed build reports the
time of the successful attempt.

The host timing patch measures the work loop after reset and ELF loading.
Both simulators use the same ELF and must return checksum `0xd687fdc5`.
The Verilator run establishes the cycle count and ordered integer commit trace,
so a different supported compiler can produce a different instruction stream.
Each C++HDL validation run must match Verilator's cycle count, PCs, instruction
bits, and integer destination registers and values. The timed C++HDL run
checks completion and checksum again. Verilator still formats its RTL trace
during the timed run. C++HDL collects its comparison trace in a separate run.

The report's speedup is single-threaded Verilator work time divided by
backend work time.
Per-run results and diagnostic logs are under `runs/` and `logs/` in the
work root.

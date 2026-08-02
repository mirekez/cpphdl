# cpphdl

CppHDL Hardware Description Language

Slides: https://github.com/user-attachments/files/24297702/cpphdl.pdf

Spec: https://github.com/mirekez/cpphdl/blob/main/doc/cpphdl.pdf

## build

Win64 requires the following to be done:
 - Install msys2-x86_64-20240727.exe, Miniconda3-py39_24.7.1-0-Windows-x86_64.exe, run MSYS2 MSYS console
 - git clone https://github.com/mirekez/cpphdl; cd cpphdl

And for Linux:
 - git clone ssh://github.com/mirekez/cpphdl; cd cpphdl
 - wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh; chmod a+x Miniconda3-latest-Linux-x86_64.sh; ./Miniconda3-latest-Linux-x86_64.sh
 - source ~/miniconda3/bin/activate; conda init

Then for both Win&Lin:
 - conda create -p ./.conda; source activate base; conda activate ./.conda; conda env update --file requirements.yaml
 - mkdir build; cd build; cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ..; make

## tests

```
cd build
ctest
# be sure you provided RISCV_HOME=<> path to riscv-gnu-toolchain build if you want to run CPU tests
# you need also to run .load_sail_riscv_sim.sh and .load_spike.sh in tribe/tests/ if you want to run CPU tests
```

## optimized simulation

`--optimize-combs ROOT` generates a flattened `calc_all()` implementation for
the concrete hierarchy rooted at `ROOT`. `--optimize-combs-l1 ROOT` additionally
schedules cache-backed procedural comb methods once, resolves trivial port
wrappers across module boundaries, and merges type-compatible exact comb
expressions. The L1 mode avoids memoization calls and clock checks in the
generated comb schedule. You can use generated code instead of original
_work() and xxx_comb_func() functions to execute it up to 10 times faster.

`--optimize-math` enables exact scalar replacements for recognized bit-level
math networks, currently complete bit reversals, bit replications, and packed
sign extensions. It requires either `--optimize-combs` or
`--optimize-combs-l1`; unmatched expressions retain their normal comb
implementation.

```sh
build/cpphdl --optimize-combs-l1 Top --optimize-math \
  --generated-dir generated Top.h -Iinclude
```

## author

This software is developed by Mike Reznikov (https://www.linkedin.com/in/mike-reznikov) based on the results of own research.

This work is not subsidized or paid.

## development plan

- comb. hierarchy checks
- regs/luts usage estimation
- timing estimation

## verilog bugs

- this chapter is to collect verilog gotchas which are inexplicable for non verilog-guru and fixed for C++-native

1. In verilog {a*b} has size of a or b, not size(a)+size(b). In C++ this works correctly. Cpphdl will explicitly widen both operands to act as C++ and try to save result

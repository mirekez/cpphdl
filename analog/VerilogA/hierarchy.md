# Compile large electrical hierarchies

Partitioning divides an exported continuous model into small Verilog-A
compilation units. Every component still contributes to one shared SPICE
equation system. Loading, feedback and stored charge cross IC boundaries
through the original electrical nodes; this is not a sequence of isolated
component simulations.

From the package root, first export the IC with the normal converter, then:

```sh
./build/cpphdl --verilog-a --partition analog/out/Receiver.va \
    --output analog/out/receiver_hierarchy
```

The standalone `analog/build/VerilogA/cpphdl-veriloga` accepts the same
`--partition` arguments. When partitioning an exported simulation experiment,
also pass `--experiment path/to/bench.cir`. The output preserves the bench's
sources, initial conditions, saved observations and analysis settings, replacing
only the original model instance, definition and OSDI load.

The output directory contains:

- `AnalogLeaf_<hash>.va`: distinct canonical component models. Identical
  equations and node structure share a compiled model.
- `<Module>.cir`: a reusable SPICE subcircuit connecting those instances to
  their original shared wires.
- `<Module>.hierarchy.json`: source/model hashes, instance owners, contribution
  identities, node bindings and generated file names used by the test runners.
- `bench.cir`, when an experiment was supplied.

Compile each leaf with `openvaf leaf.va -o leaf.osdi`. The generated experiment
loads those OSDI files; run `ngspice -n -b bench.cir` from its output directory.
A reusable subcircuit needs the same leaf loads in the caller's control block.
Leaf load filenames are relative and unquoted because ngspice's `pre_osdi`
command retains quote characters. The library include uses an absolute path.

The partitioner checks that every identified current contribution occurs once
and expands its canonical leaf back to exactly the original equation and node
names. It rejects unsupported declarations, anonymous equations, duplicate
device identities, case-colliding SPICE nodes, voltage contributions,
cross-instance branch-current probes and helper functions with hidden
electrical or dynamic state. Scalar helper functions and explicit capacitor
`ddt(V(...))` currents are supported. An unsupported model fails conversion;
there is no substitution of devices or process models.

`analog.veriloga.hierarchy_structure` checks these restrictions and model
deduplication. `analog.veriloga.hierarchy_equations` checks a loaded, hierarchical
RC circuit against its analytic solution independently in native C++, flat
Verilog-A, a partitioned experiment and a partitioned reusable IC. Native/flat
differences must stay below 1 mV; both partitioned versions must match the flat
simulation within 10 µV. The sixteen-slice receiver additionally exercises
partitioned nonlinear transistor equations under shared sampling loads.

The root entry point partitions the complete sixteen-slice programmable receiver
into 2,403 contributions, 378 instances and 20 distinct models. All twenty
compile successfully with a sampled compiler peak below 58 MiB. The 1G catalog
compilation test uses this path for large continuous ICs and compiles smaller
models directly. Its checked-in exports remain ordinary reusable Verilog-A.

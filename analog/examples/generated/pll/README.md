# pll

[Original C++ model and explanation](../../pll/README.md).

`design.va` is the complete elaborated analog bench, including loads and identified simulation models. `bench.cir` supplies sources, initial capacitor voltages and transient analysis. `columns.csv` names voltage observations; `conditions.json` records timing and initialization. These are generated inputs, not waveform results or a claim that native purpose constraints passed.

Run `openvaf design.va -o design.osdi`, then `ngspice -n -b bench.cir` here. This writes simulation results; keep those files out of Git.

The nominal bench is here; `loaded/`, `refined/` hold the other native scenarios.

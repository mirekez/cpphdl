# Serializer, tap history and deserializer

[serializer.hpp](serializer.hpp) defines a four-input serializer. Three finite-drive
`sim_mux2` ICs select even/odd lanes using coherent half-rate and quarter-rate TX
clocks. Input order is DATA[0], DATA[1], DATA[2], DATA[3]. At 10.3125 Gb/s, the
external clock-tree outputs are 5.15625 and 2.578125 GHz. Those clocks belong to TX;
they are never wired into receive clock recovery.

`tap_history` clocks three `sim_dff` stages at the full-rate TX sampling clock. After
an edge, history[0] is the new precursor, history[1] is the main cursor, and
history[2] is the postcursor. This makes the three-tap filter causal, with one
symbol of buffering plus the initial half-UI sample placement.

[deserializer.hpp](deserializer.hpp) shifts decisions into four stages using the
recovered clock. DATA[0] is the newest stored symbol. Because all masters close
before any slave opens, downstream stages retain old-state pipeline latency.
The PHY's DFE has already sampled the current bit, so deserializer DATA[0] contains
its previous decision. A PCS/word-aligner selects every fourth sliding window;
this class does not invent a recovered word alignment.

The [basic storage models](../basic/sim_clocked_storage.hpp) use explicitly
buffered finite-conductance tracking paths, capacitor state and leakage. Buffering
prevents passive charge sharing from turning a register chain into an analog
averager. Master and slave switch thresholds provide a nonoverlap interval.
They model electrical loading at outputs, not transistor-level power or metastability.

Tests: `sample_hold`, `dff`, `mux2`, `xor`, `history`, `serializer`, `deserializer`.
Each has `.cpp`, `.veriloga` and `.compare` CTests. Both solvers must independently
recover every checked symbol, including held values while an input changes.
Reusable modules are in [generated/](generated/).

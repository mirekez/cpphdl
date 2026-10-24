# PMA transmitter test symbols

`PMATestPattern : Module` generates one lane's test symbols at 125 MHz. It
produces the PCS offset code: 0 through 4 represent −2 through +2. The current
`PAM5DriverControl` uses the opposite differential pin convention: translate
with `4 - symbol_out` when positive logical symbols must make P positive to N.
Supply the selected transmit clock; `slave_clock_out` requests
slave timing for mode 3 and does not implement a clock switch.

| Input | Purpose |
| --- | --- |
| `enable_in` | Enable generation; disabling clears the sequence and fault |
| `mode_in` | 0: quiet; 1: pulse/droop sequence; 2: alternating master-clock symbols; 3: alternating slave-clock symbols; 4: scrambled distortion symbols |
| `seed_in` | Nonzero eleven-bit mode-4 seed, sampled on entry or restart |
| `restart_in` | Begin the selected sequence again on this clock |

`symbol_out`, `valid_out`, `slave_clock_out` and `fault_out` are registered.
Reset, disable and mode 0 produce quiet code 2 with `valid_out = false`.
Modes 5–7 and a zero mode-4 seed produce a fault and suppress valid output.
A valid mode change or restart clears the fault. Changing the seed during an
active sequence does not perturb it.

Mode 1 repeats 2048 symbols: four isolated signed full/half-scale pulses,
four 128-symbol full-scale plateaus, then 1024 zero symbols. Mode 4 uses the
11-stage maximal-length recurrence and symbol mapping described in
[UNH-IOL's PMA tutorial, pages 47–53](https://www.iol.unh.edu/sites/default/files/knowledgebase/ge/1000BASE-T_PMA_July2004.pdf).
The counter and scrambler share one eleven-bit state register because the
modes are exclusive. This avoids a separate state machine for each test mode.

The independent test builds explicit pulse lists and a bit-array recurrence.
It checks 70,388 clocks including boundaries, repeated periods, four complete
scrambler seeds, seed changes, mode changes, restart, invalid modes and reset.
Native C++ and generated RTL both pass those expected outputs separately and
their full traces match exactly. IHP mapped-gate verification also passes the
same 70,388 expectations and exact trace comparison, using 208 mapped cells.

```sh
cmake --build analog/build --target analog_1g_pma_test_pattern
ctest --test-dir analog/build --output-on-failure \
    -R '^analog[.]1g[.]pma_test_pattern[.]' -j 1
```

Generated RTL resides in `generated/PMATestPattern.sv`. These are test symbols,
not a substitute for checking the loaded transmitter waveform. Management
access, physical master/slave clock selection, complete MDI integration and
the [electrical compliance measurements](line-compliance.md) remain required.

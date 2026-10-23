# Shared normal and test transmit lane

`PMATransmitLane : Module` connects the [PMA test-pattern generator](pma_test_pattern.md)
and the existing [main/history steering controller](pam5_driver_control.md).
Both normal traffic and test patterns use the same physical output controls;
the class does not introduce a separate test-only transmitter.

The parent supplies a selected 125 MHz clock and these inputs:

| Input | Purpose |
| --- | --- |
| `enable_in` | Enable the lane; disabling resets its controls and history |
| `symbol_in` | Normal PCS symbol, encoded as symbol + 2 |
| `test_mode_in` | 0 selects normal traffic; 1–4 select a test pattern |
| `test_seed_in` | Eleven-bit mode-4 seed |
| `restart_in` | Restart the selected stream and clear history/fault |

Connect `main_p_out`, `main_n_out`, `history_p_out` and `history_n_out` to the
normal voltage translators and weighted current banks. `valid_out` describes
active controls, not achieved electrical voltage. `fault_out` combines pattern
and driver faults. `slave_clock_out` requests mode-3 timing; the parent must
implement physical clock selection.

The wrapper explicitly translates PCS polarity: logical symbol +2 becomes
steering code 0, sinking current from N and making P positive relative to N.
Codes 5–7 on the normal input remain invalid rather than being complemented
into valid symbols. The prior symbol goes through the same translation and
retains its one-quarter contribution in the existing steering controller.

Reset, disable, restart or a mode change clears the driver for one clock and
sets its previous symbol to zero. On the following clock it accepts the first
registered test symbol, or the current normal symbol. This prevents a stale
symbol from the previous stream entering the new stream's history bank.
Normal traffic has one registered driver stage; a test symbol passes through
the pattern register and that same driver stage.

The independent test checks 75,453 clocks, including all test patterns, stream
boundaries, all adjacent normal symbol pairs, deterministic random traffic,
invalid symbols, faults and recovery. Native C++ and generated RTL pass the
same absolute control expectations separately, then match their complete
traces exactly. IHP mapped verification also passes the same expectations and
exact trace comparison, using 302 mapped cells for the complete lane wrapper.

```sh
cmake --build analog/build --target analog_1g_pma_transmit_lane
ctest --test-dir analog/build --output-on-failure \
    -R '^analog[.]1g[.]pma_transmit_lane[.]' -j 1
```

This is one reusable lane, not integrated MDIO or a completed four-pair PHY.
Electrical loading, MDI waveforms and physical clock selection require their
own connected verification.

# Analog pad normal-operation loading

[`tests/pad_operating.py`](tests/pad_operating.py) uses the pinned PDK's actual
`sg13g2_IOPadAnalog` SPICE hierarchy, including primary clamps, secondary
protection, the 586.899 Ω series resistor, MOS/diode intrinsic charge, and
substrate contacts. Common core and I/O ground follows the verified padframe.
The substrate connects through the PDK's tap resistances; no ideal substrate
voltage source replaces them.

The fixture drives the primary pad through 50 Ω. The protected `padres` node
has either 0.15 or 1 pF of external load. Both loads run at five MOS corners
and −40/27/125 °C: 30 cases at 3.3 V I/O and 1.2 V core supply. Resistor and
diode models use their declared typical corners. Each case checks the full
2.4–3.2 V DC input sweep and complex small-signal transfer at 1 and 125 MHz.

All 30 cases pass. Worst DC leakage is 5.895 nA. Effective primary input
capacitance, including the tested protected-node load, stays below 1.225 pF.
The report retains the actual primary and secondary gains, phase, decks,
logs and source/model hashes. A failed/missing analysis cannot pass merely
because ngspice exits with status zero.

```sh
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]pdk[.]1g_repeater[.]pad_operating$'
```

These measurements describe normal DC and small-signal loading. Connected
receiver/transmitter transient verification, package/magnetics and ESD stress
qualification require their corresponding experiments.

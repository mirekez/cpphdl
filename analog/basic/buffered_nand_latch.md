# A latch that isolates its command sources

[`buffered_nand_latch`](buffered_nand_latch.hpp) is an `IC` with active-low
`SET_N` and `RESET_N`, active-high `CLEAR`, complementary `Q`/`Q_N`, and the
inherited `Vdd`/`Vss` ports. Its storage is a
[`cleared_nand_latch`](nand_latch.hpp). Two CMOS inverter stages on each command
retain its polarity while reducing feedback from the storage gates into the
source circuit. Clear acts directly on the storage.

Each input inverter uses channel strength 0.001 A/V² and each following driver
0.004 A/V², with 0.7 V threshold. Explicit input, Miller, intermediate and
output capacitances load the actual wires. All inverter and NAND currents
return to the connected rails. A weak supply, added output load or fast command
can therefore change timing or prevent the intended state from being reached.

This circuit addresses a measured integration failure: the IHP receiver's
regenerative comparator passed standalone checks but made the wrong small
differential decision when directly loaded by the storage NAND gates. A PSP
diagnostic with the buffered latch and a 1 kΩ reference restored the correct
decision. The receiver's complete conversion and process qualification remain
separate from that diagnostic.

```cpp
buffered_nand_latch decision;
decision.SET_N = comparator.OUTN;
decision.RESET_N = comparator.OUTP;
decision.CLEAR = power_on_clear;
decision.Q = stored_decision;
decision.Vdd = supply;
decision.Vss = ground;
decision.elaborate();
```

The unit fixture reuses the existing clear/set/reset/retention, simultaneous
assertion, recovery and loaded-output contract. Native C++ and generated
Verilog-A must pass absolute checks separately before complete waveform
comparison. Its initial 2 ps experiment passed absolute checks but exceeded
the 20 mV RMS comparison limit by 0.388 mV. At 0.5 ps both unloaded and loaded cases pass, with worst waveform RMS
5.134 mV and unchanged 20 mV limits. The original unbuffered latch also
passes its existing unit contract.

```sh
cmake --build analog/build --target analog_buffered_nand_latch
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]basic[.]buffered_nand_latch$'
```

`basic/generated/buffered_nand_latch.va` is generated from this class.
The IHP receiver binder supplies its explicit four-terminal PSP device sizes;
the generic Verilog-A model uses the framework channel equations.
